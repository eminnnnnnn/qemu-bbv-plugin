extern "C" {
    #include "qemu-plugin.h"
}

#include "bbv.h"
#include "common.h"

#include <iostream>
#include <string>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

bool use_caches = true;

inline static void print_usage(std::ostream& out = std::cout)
{
    out <<  "SYNOPSIS:\n"
            "   qemu-aarch64 -d plugin -plugin path/to/libbbv_caches.so[,[OPTIONS]]\n\n"
            "OPTIONS:\n"
            "   interval - size of interval (instruction counter value) [default 10000000]\n"
            "   outfile - name of the output file [default bbv_stats.bb]\n"
            "   caches=[on|off] - cache on/off [default on]"
            "   l1i_cachesize - total cache size in bytes (cachesize % (blksize * assoc) == 0) [default 16K]\n"
            "   l1i_blksize - size of block in bytes (cache line) [default 64]\n"
            "   l1i_assoc - cache associativity [default 8]\n"
            "   l1d_cachesize [default 16K]\n"
            "   l1d_blksize [default 64]\n"
            "   l1d_assoc [default 8]\n"
            "   l2_cachesize [default 2M]\n"
            "   l2_blksize [default 64]\n"
            "   l2_assoc [default 16]\n";
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    bbv_clean();
    std::cout << "Emulation passed, " << total_icount << " instructions were executed\n";
}

QEMU_PLUGIN_EXPORT
int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info, int argc, char **argv)
{
    std::string init_err;
    if (init(argc, argv, init_err) != INIT_SUCCESS) {
        std::cerr << init_err << std::endl;
        print_usage();
        return -1;
    }

    qemu_plugin_register_vcpu_tb_trans_cb(id, update_bb_stats);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);

    return 0;
}
