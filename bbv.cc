#include "bbv.h"

#include "cache.h"
#include "common.h"

#include <unordered_map>
#include <mutex>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <iostream>


static enum qemu_plugin_mem_rw rw = QEMU_PLUGIN_MEM_RW;

static std::mutex _mutex; // plugins need to take care of their own locking

static uint64_t next_uid = 1UL;

static uint64_t icount = 0UL;   // instruction counter

static uint64_t interval_count = 0UL;

static std::ofstream bbv_file;

std::unordered_map<addr_t, bb_stats_t> bb_map;

// variables dependent on command options
static uint64_t interval_size = 10'000'000UL;
static std::string outfile_name ("bbv_stats.bb");

extern const char* option_names[];
extern bool use_caches;

void bbv_clean() {
    free_caches();
}

static void dump_stats(unsigned int cpu_index, void *udata)
{
    _mutex.lock();
    icount += ((bb_stats_t*)udata)->insn_count;
    total_icount += ((bb_stats_t*)udata)->insn_count;
    if (icount >= interval_size) {
        bbv_file << "T";
        for (auto& bb_stat : bb_map) {
            auto& stat = bb_stat.second;
            if (stat.exec_count != 0)
            {
                uint64_t total_stat = stat.exec_count * stat.insn_count + stat.cache_stats.total_miss_count();
                bbv_file << ":" << stat.bb_uid << ":" << total_stat << " ";
                // bbv_file << ":" << stat.bb_uid << ":" << total_stat << stat.cache_stats.l1d_miss_count << ":" << stat.cache_stats.l1i_miss_count << ":" << stat.cache_stats.l2_miss_count << " ";
                stat.exec_count = 0;
                stat.cache_stats.reset();
            }
        }
        bbv_file << std::endl;
        icount = 0;
        interval_count++;
    }
    _mutex.unlock();
}

void update_bb_stats(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    uint64_t pc = qemu_plugin_tb_vaddr(tb);
    size_t n_insns = qemu_plugin_tb_n_insns(tb);

    _mutex.lock();
    auto& bblock = bb_map[pc];
    if (bblock.bb_uid == 0) {
        bblock.bb_uid = next_uid++;
        bblock.insn_count = n_insns;
    }
    _mutex.unlock();

    if (use_caches) {
        for (size_t i = 0; i < n_insns; i++) {
            struct qemu_plugin_insn* insn = qemu_plugin_tb_get_insn(tb, i);

            qemu_plugin_register_vcpu_mem_cb(insn, dcache_access,
                                             QEMU_PLUGIN_CB_NO_REGS,
                                             rw, &bblock.cache_stats);
            qemu_plugin_register_vcpu_insn_exec_cb(insn, icache_access,
                                                   QEMU_PLUGIN_CB_NO_REGS, insn);
        }
    }
    qemu_plugin_register_vcpu_tb_exec_inline(tb, QEMU_PLUGIN_INLINE_ADD_U64,
                                             &bblock.exec_count, 1);
    qemu_plugin_register_vcpu_tb_exec_cb(tb, dump_stats,
                                         QEMU_PLUGIN_CB_NO_REGS, &bblock);
}

int init(int argc, char** argv, std::string& err_msg) {
    for (int i = 0; i < argc; i++) {
        char* delim_pos = std::strchr(argv[i], '=');
        options_t opt_name = options_t::UNDEFINED;

        if (delim_pos == NULL || *(delim_pos + 1) == 0) {
            err_msg = "Error while parsing option " + std::string(argv[i]);
            return INVALID_OPTION_FORMAT;
        }

        for (int8_t opt_num = 0; opt_num < (int8_t)options_t::OPTIONS_SIZE; opt_num++) {
            if (std::strncmp(argv[i], option_names[opt_num], std::strlen(option_names[opt_num])) == 0) {
                opt_name = (options_t)opt_num;
                break;
            }
        }

        const char* opt_value = delim_pos + 1;
        switch (opt_name) {
            case options_t::INTERVAL_SIZE:
                interval_size = std::strtoull(opt_value, NULL, 10);
                if (interval_size == 0) {
                    err_msg = "The interval size should be greater than 0";
                    return INVALID_OPTION_VALUE;
                }
                break;

            case options_t::OUTPUT_FILE:
                outfile_name = opt_value;
                break;

            case options_t::USE_CACHES:
                if (std::strcmp("on", opt_value) == 0) {
                    use_caches = true;
                } else if (std::strcmp("off", opt_value) == 0) {
                    use_caches = false;
                } else {
                    err_msg = "Invalid value for bool option";
                    return INVALID_OPTION_VALUE;
                }
                break;

            case options_t::UNDEFINED:
                err_msg = "Unrecognized option " + std::string(argv[i]);
                return UNRECOGNIZED_OPTION;
        }
    }

    bbv_file.open(outfile_name, std::ofstream::out);
    if (bbv_file.bad()) {
        err_msg = "Error while opening file " + std::string(outfile_name);
        return INVALID_OPTION_VALUE;
    }

    if (use_caches)
        return init_caches(argc, argv, err_msg);
    else
        return INIT_SUCCESS;
}