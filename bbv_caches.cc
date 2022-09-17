extern "C" {
    #include <qemu-plugin.h>
}

#include <unordered_map>
#include <mutex>
#include <cstring>
#include <fstream>
#include <cstdint>
#include <limits>
#include <iostream>
#include <cstdlib>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

//////////////////////////
//
// typedefs and structs
//
//////////////////////////

using bb_uid_t = uint64_t;
using bb_addr_t = uint64_t;

struct bb_stats_t {
    bb_uid_t bb_uid = 0UL;
    uint64_t exec_count = 0UL;
    uint64_t insn_count = 0UL;

    bool operator<(const bb_stats_t& bb) { return this->exec_count < bb.exec_count; }
};

// command options enumeration
enum class options_t : int8_t {
    UNDEFINED = -1, // std::numeric_limits<uint8_t>::max(),
    INTERVAL_SIZE,
    OUTPUT_FILE,
    OPTIONS_SIZE
};

// inline static bool operator<(options_t opt_num, int num) { return int(opt_num) < num; }
inline static bool operator<(int num, options_t opt_num) { return int(opt_num) > num; }

///////////////////////////////////////////////
//
// global static variables with default values
//
///////////////////////////////////////////////

static std::mutex _mutex; // plugins need to take care of their own locking
static bb_uid_t next_uid = 1UL;
static uint64_t icount = 0UL;   // instruction counter
static uint64_t total_icount = 0UL;
static uint64_t interval_count = 0UL;
// variables dependent on command options
static uint64_t interval_size = 10'000'000UL;
static std::string outfile_name ("bbv_stats.bb");

static std::unordered_map<bb_addr_t, bb_stats_t> bb_map;
static std::ofstream bbv_file;

const char* option_names[] = { "interval", "outfile" };

// const char* help_msg = R"(
// USAGE:
//     qemu-aarch64 -d plugin -plugin path/to/libbbv_caches.so[,[OPTIONS]]

// OPTIONS:
//     interval - size of interval (instruction counter value)
//     outfile - name of the output file
// )";

/*const char* h = \
    "USAGE:\n"
    "   qemu-aarch64 -d plugin -plugin path/to/libbbv_caches.so[,[OPTIONS]]\n\n"
    "OPTIONS:\n"
    "   interval - size of interval (instruction counter value)\n"
    "   outfile - name of the output file\n";*/

inline static void print_usage(std::ostream& out = std::cout)
{
    out <<  "SYNOPSIS:\n"
            "   qemu-aarch64 -d plugin -plugin path/to/libbbv_caches.so[,[OPTIONS]]\n\n"
            "OPTIONS:\n"
            "   interval - size of interval (instruction counter value) [default 10000000]\n"
            "   outfile - name of the output file [default bbv_stats.bb]\n";
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    // _mutex.lock();
    // bbv_file.close();
    // _mutex.unlock();
    std::cout << "Emulation passed, " << total_icount << " instructions were executed\n";
    // std::cout << next_uid - 1 << " blocks were translated\n";
    // std::cout << "size of map is " << bb_map.size() << "\n";
}

static void dump_stats(unsigned int cpu_index, void *udata)
{
    // [[maybe unused]] static int interval_ct = 0;
    _mutex.lock();
    icount += ((bb_stats_t*)udata)->insn_count;
    total_icount += icount;
    if (icount >= interval_size) {
        bbv_file << "T";
        for (auto& bb_stat : bb_map) {
            auto& stat = bb_stat.second;
            if (stat.exec_count != 0)
            {
                bbv_file << ":" << stat.bb_uid << ":" << stat.exec_count * stat.insn_count << " ";
                stat.exec_count = 0;
            }
        }
        bbv_file << std::endl;
        icount = 0; // icount -= interval_size ???
        interval_count++;
    }
    _mutex.unlock();
}

static void update_stats(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    uint64_t pc = qemu_plugin_tb_vaddr(tb);
    size_t insns_count = qemu_plugin_tb_n_insns(tb);

    _mutex.lock();
    auto& bblock = bb_map[pc];
    if (bblock.bb_uid == 0) {
        bblock.bb_uid = next_uid++;
        bblock.insn_count = insns_count;
    }
    _mutex.unlock();

    qemu_plugin_register_vcpu_tb_exec_inline(tb, QEMU_PLUGIN_INLINE_ADD_U64,
                                             &bblock.exec_count, 1);
    qemu_plugin_register_vcpu_tb_exec_cb(tb, dump_stats,
                                         QEMU_PLUGIN_CB_NO_REGS, &bblock);
}

QEMU_PLUGIN_EXPORT
int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info, int argc, char **argv)
{
    for (int i = 0; i < argc; i++) {
        char* delim_pos = std::strchr(argv[i], '=');
        options_t opt_name = options_t::UNDEFINED;
        // std::strtok()
        // std::strcspn, std::strbrk
        if (delim_pos == NULL || *(delim_pos + 1) == 0) {
            std::cerr << "Error while parsing option " << argv[i] << std::endl;
            print_usage();
            return -1;
        }
        *delim_pos = 0;
        for (int8_t opt_num = 0; opt_num < (int8_t)options_t::OPTIONS_SIZE; opt_num++) {
            // opt_name = (strcmp(argv[i], option_names[opt_num]) == 0 ? (options_t)opt_num : options_t::UNDEFINED);
            // or strncmp(argv[i], option_names[opt_num], sizeof(option_names[opt_num]));
            if (strcmp(argv[i], option_names[opt_num]) == 0) {
                opt_name = (options_t)opt_num;
                break;
            }
        }
        const char* opt_value = delim_pos + 1;
        switch (opt_name) {
            case options_t::INTERVAL_SIZE:
                interval_size = std::strtoull(opt_value, NULL, 10);
                if (interval_size == 0) {
                    std::cerr << "The interval size should be greater than 0\n";
                    return -1;
                }
                break;
            case options_t::OUTPUT_FILE:
                outfile_name = opt_value;
                break;
            default:
            case options_t::UNDEFINED:
                std::cerr << "Unrecognized option " << argv[i] << std::endl;
                print_usage();
                return -1;                 
        }
    }

    bbv_file.open(outfile_name, std::ofstream::out);
    if (bbv_file.bad()) {
        std::cerr << "Error while opening file " << outfile_name << std::endl;
        return -1;
    }

    qemu_plugin_register_vcpu_tb_trans_cb(id, update_stats);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);

    return 0;
}
