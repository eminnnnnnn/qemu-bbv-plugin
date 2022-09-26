#include "common.h"

const char* option_names[] = { "interval", "outfile", "caches",
                               "l1i_cachesize", "l1i_blksize", "l1i_assoc",
                               "l1d_cachesize", "l1d_blksize", "l1d_assoc"
                               "l2_cachesize", "l2_blksize", "l2_assoc" };

uint64_t total_icount = 0UL;