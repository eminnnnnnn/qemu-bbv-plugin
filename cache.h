#ifndef __CACHES_H
#define __CACHES_H

extern "C" {
    #include "qemu-plugin.h"
}

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

struct cache_t {
    cache_t() = delete;
    cache_t(int blksize, int cache_assoc, int cachesize);
    ~cache_t() = default;

    int get_invalid_block_idx(uint64_t set_idx);
    int get_block_idx(uint64_t addr);
    bool access(uint64_t addr);

    inline uint64_t extract_tag(uint64_t addr)
    {
        return addr & tag_mask;
    }

    inline uint64_t extract_set(uint64_t addr)
    {
        return (addr & set_mask) >> blksize_shift;
    }

 private:
    struct cache_set_t {
        cache_set_t(int assoc) : blocks(assoc) {}
        ~cache_set_t() = default;

        struct cache_block_t {
            uint64_t tag = 0UL;
            bool valid = false;
        };

        std::vector<cache_block_t> blocks;
        std::deque<int> queue;
    };

    int assoc;
    int cache_size;
    int num_sets;
    int blksize_shift;
    uint64_t set_mask;
    uint64_t tag_mask;
    uint64_t accesses;
    uint64_t misses;
    std::vector<cache_set_t> sets;
};

struct core_cache_t {
    core_cache_t() = delete;
    ~core_cache_t() = default;
    core_cache_t(int l1i_blksize, int l1i_assoc, int l1i_cachesize,
                 int l1d_blksize, int l1d_assoc, int l1d_cachesize,
                 int l2_blksize, int l2_assoc, int l2_cachesize)
        : l1_dcache(l1d_blksize, l1d_assoc, l1d_cachesize),
          l1_icache(l1i_blksize, l1i_assoc, l1i_cachesize),
          l2_cache(l2_blksize, l2_assoc, l2_cachesize) {}

    bool access_l1d(uint64_t effective_addr) { return l1_dcache.access(effective_addr); }
    bool access_l1i(uint64_t effective_addr) { return l1_icache.access(effective_addr); }
    bool access_l2(uint64_t effective_addr) { return l2_cache.access(effective_addr); }

private:
    cache_t l1_dcache;
    cache_t l1_icache;
    cache_t l2_cache;
};

int init_caches(int argc, char** argv, std::string& err_msg);
void icache_access(unsigned int vcpu_index, void *userdata);
void dcache_access(unsigned int vcpu_index, qemu_plugin_meminfo_t info, uint64_t vaddr, void *userdata);
void free_caches();

#endif