#ifndef __BBV_H
#define __BBV_H

extern "C" {
    #include "qemu-plugin.h"
}

#include <string>

void update_bb_stats(qemu_plugin_id_t id, struct qemu_plugin_tb *tb);
int init(int argc, char** argv, std::string& err_msg);
void bbv_clean();

#endif