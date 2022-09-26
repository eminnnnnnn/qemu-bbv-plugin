#include "cache.h"

#include "common.h"

#include <string_view>
#include <mutex>
#include <unordered_map>

static std::mutex l1d_lock;
static std::mutex l1i_lock;
static std::mutex l2_lock;

static core_cache_t* core_cache;

extern std::unordered_map<addr_t, bb_stats_t> bb_map;
extern const char* option_names[];

cache_t::cache_t(int blksize, int cache_assoc, int cachesize)
    : assoc(cache_assoc), cache_size(cachesize),
      num_sets(cachesize / (blksize * cache_assoc)), accesses(0), misses(0), sets(num_sets, {cache_assoc})
{
    blksize_shift = __builtin_ctz(blksize);

    uint64_t blk_mask = blksize - 1;
    set_mask = ((num_sets - 1) << blksize_shift);
    tag_mask = ~(set_mask | blk_mask);
}

int cache_t::get_invalid_block_idx(uint64_t set_idx)
{
    for (int i = 0; i < assoc; i++) {
        if (!sets[set_idx].blocks[i].valid) {
            return i;
        }
    }

    return -1;
}

int cache_t::get_block_idx(uint64_t addr)
{

    uint64_t tag = extract_tag(addr);
    uint64_t set = extract_set(addr);

    for (int i = 0; i < assoc; i++) {
        if (sets[set].blocks[i].tag == tag && sets[set].blocks[i].valid) {
            return i;
        }
    }

    return -1;
}

bool cache_t::access(uint64_t addr)
{
    uint64_t tag = extract_tag(addr);
    uint64_t set_num = extract_set(addr);

    int hit_blk = get_block_idx(addr);
    if (hit_blk != -1) {
        // no update on hit for FIFO eviction policy
        accesses++;
        return true;
    }

    int replaced_blk = get_invalid_block_idx(set_num);

    if (replaced_blk == -1) {
        replaced_blk = sets[set_num].queue.back(); // get replaced block
        sets[set_num].queue.pop_back();  
    }

    // update miss
    sets[set_num].queue.push_front(replaced_blk);

    sets[set_num].blocks[replaced_blk].tag = tag;
    sets[set_num].blocks[replaced_blk].valid = true;

    misses++;

    return false;
}

void dcache_access(unsigned int vcpu_index, qemu_plugin_meminfo_t info,
                            uint64_t vaddr, void *userdata)
{
    bool hit_in_l1;
    cache_stat_t* cache_stats = (cache_stat_t*)userdata;

    struct qemu_plugin_hwaddr* hwaddr = qemu_plugin_get_hwaddr(info, vaddr);
    if (hwaddr && qemu_plugin_hwaddr_is_io(hwaddr)) {
        return;
    }

    uint64_t effective_addr = hwaddr ? qemu_plugin_hwaddr_phys_addr(hwaddr) : vaddr;

    l1d_lock.lock();
    hit_in_l1 = core_cache->access_l1d(effective_addr);
    if (!hit_in_l1) {
        __atomic_fetch_add(&(cache_stats->l1d_miss_count), 1, __ATOMIC_SEQ_CST);
    }
    l1d_lock.unlock();

    if (hit_in_l1) {
        return;
    }

    l2_lock.lock();
    if (!core_cache->access_l2(effective_addr)) {
        __atomic_fetch_add(&(cache_stats->l2_miss_count), 1, __ATOMIC_SEQ_CST);
    }
    l2_lock.unlock();
}

void icache_access(unsigned int vcpu_index, void *userdata)
{
    bool hit;

    qemu_plugin_insn* insn = (qemu_plugin_insn*)userdata;
    uint64_t insn_addr = qemu_plugin_insn_vaddr(insn);
    auto& cache_stats = bb_map[insn_addr].cache_stats;

    l1i_lock.lock();
    hit = core_cache->access_l1i(insn_addr);
    if (!hit) {
        __atomic_fetch_add(&(cache_stats.l1i_miss_count), 1, __ATOMIC_SEQ_CST);
    }
    l1i_lock.unlock();

    if (hit) {
        return;
    }

    l2_lock.lock();
    if (!core_cache->access_l2(insn_addr)) {
        __atomic_fetch_add(&(cache_stats.l2_miss_count), 1, __ATOMIC_SEQ_CST);
    }
    l2_lock.unlock();
}

static const char *get_config_error(int blksize, int assoc, int cachesize)
{
    if (cachesize % blksize != 0) {
        return "Cache size must be divisible by block size";
    } else if (cachesize % (blksize * assoc) != 0) {
        return "Cache size must be divisible by set size (assoc * block size)";
    } else {
        return NULL;
    }
}

void free_caches()
{
    delete core_cache;
}

int init_caches(int argc, char** argv, std::string& err_msg)
{
    // default size of the L1 Data Cache is 16K
    int l1_dassoc = 8;
    int l1_dblksize = 64;
    int l1_dcachesize = l1_dblksize * l1_dassoc * 32;

    // same size for the L1 Instruction Cache
    int l1_iassoc = 8;
    int l1_iblksize = 64;
    int l1_icachesize = l1_iblksize * l1_iassoc * 32;

    // default size of the L2 Cache is 2M
    int l2_assoc = 16;
    int l2_blksize = 64;
    int l2_cachesize = l2_assoc * l2_blksize * 2048;

    for (int i = 0; i < argc; i++) {
        std::string_view opt { argv[i] };
        auto delim_pos = opt.find('=');
        options_t opt_name = options_t::UNDEFINED;

        if (delim_pos == std::string_view::npos || opt.length() - 1 == delim_pos) {
            err_msg = "Error while parsing option " + std::string(opt);
            return INVALID_OPTION_FORMAT;
        }

        for (int8_t opt_num = 0; opt_num < (int8_t)options_t::OPTIONS_SIZE; opt_num++) {
            if (opt.compare(0, delim_pos, option_names[opt_num]) == 0) {
                opt_name = (options_t)opt_num;
                break;
            }
        }

        auto opt_value = opt.substr(delim_pos + 1);
        switch (opt_name) {
            case options_t::L1I_CACHE_SIZE:
                l1_icachesize = std::atoi(opt_value.data());
                break;
            case options_t::L1I_BLOCK_SIZE:
                l1_iblksize = std::atoi(opt_value.data());
                break;
            case options_t::L1I_ASSOCIATIVITY:
                l1_iassoc = std::atoi(opt_value.data());
                break;
            case options_t::L1D_CACHE_SIZE:
                l1_dcachesize = std::atoi(opt_value.data());
                break;
            case options_t::L1D_BLOCK_SIZE:
                l1_dblksize = std::atoi(opt_value.data());
                break;
            case options_t::L1D_ASSOCIATIVITY:
                l1_dassoc = std::atoi(opt_value.data());
                break;
            case options_t::L2_CACHE_SIZE:
                l2_cachesize = std::atoi(opt_value.data());
                break;
            case options_t::L2_BLOCK_SIZE:
                l2_blksize = std::atoi(opt_value.data());
                break;
            case options_t::L2_ASSOCIATIVITY:
                l2_assoc = std::atoi(opt_value.data());
                break;
            case options_t::UNDEFINED:
                err_msg = "Unrecognized option " + std::string(argv[i]);
                return UNRECOGNIZED_OPTION;                 
        }
    }
    
    const char* err = get_config_error(l1_dblksize, l1_dassoc, l1_dcachesize); 
    if (err != NULL) {
        err_msg = "L1 ICache cannot be constructed from given parameters\n" + std::string(err);
        return INVALID_OPTION_VALUE;
    }

    err = get_config_error(l1_iblksize, l1_iassoc, l1_icachesize);
    if (err != NULL) {
        err_msg = "L1 ICache cannot be constructed from given parameters\n" + std::string(err);
        return INVALID_OPTION_VALUE;
    }

    err = get_config_error(l2_blksize, l2_assoc, l2_cachesize);
    if (err != NULL) {
        err_msg = "L2 cache cannot be constructed from given parameters\n" + std::string(err);
        return INVALID_OPTION_VALUE;
    }

    core_cache = new core_cache_t(l1_iblksize, l1_iassoc, l1_icachesize,
                                  l1_dblksize, l1_dassoc, l1_dcachesize,
                                  l2_blksize, l2_assoc, l2_cachesize);

    return INIT_SUCCESS;
}
