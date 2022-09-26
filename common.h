#ifndef __COMMON_H
#define __COMMON_H

#include <cstdint>

using addr_t = uint64_t;

// command options
enum class options_t : int8_t {
    UNDEFINED = -1,
    INTERVAL_SIZE,
    OUTPUT_FILE,
    USE_CACHES,
    L1I_CACHE_SIZE,
    L1I_BLOCK_SIZE,
    L1I_ASSOCIATIVITY,
    L1D_CACHE_SIZE,
    L1D_BLOCK_SIZE,
    L1D_ASSOCIATIVITY,
    L2_CACHE_SIZE,
    L2_BLOCK_SIZE,
    L2_ASSOCIATIVITY,
    OPTIONS_SIZE
};

enum init_status_t : int8_t {
    UNRECOGNIZED_OPTION,
    INVALID_OPTION_VALUE,
    INVALID_OPTION_FORMAT,
    INIT_SUCCESS
};

struct cache_stat_t {
    uint64_t l1i_miss_count = 0UL;
    uint64_t l1d_miss_count = 0UL;
    uint64_t l2_miss_count = 0UL;

    uint64_t total_miss_count() { return (l1d_miss_count + l1i_miss_count)*2 + l2_miss_count*4; }
    void reset() { l1d_miss_count = l1i_miss_count = l2_miss_count = 0; }
};

struct bb_stats_t {
    uint64_t bb_uid = 0UL;
    uint64_t exec_count = 0UL;
    uint64_t insn_count = 0UL;

    cache_stat_t cache_stats;

    bool operator<(const bb_stats_t& bb) { return this->exec_count < bb.exec_count; }
};

inline static bool operator<(int num, options_t opt_num) { return int(opt_num) > num; }

extern const char* options[];
extern uint64_t total_icount;

#endif