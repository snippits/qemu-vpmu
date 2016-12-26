// C headers
extern "C" {
#include "libs/efd.h" // Extracting information from binary file
}
#include <iostream> // Basic I/O related C++ header
#include <fstream>  // File I/O
#include <sstream>  // String buffer
#include <cerrno>   // Readable error messages
#include <vector>   // std::vector
#include <string>   // std::string
#include "json.hpp" // nlohmann::json

#include "vpmu.hpp"           // VPMU common header
#include "vpmu-stream.hpp"    // VPMUStream, VPMUStream_T
#include "vpmu-translate.hpp" // VPMUTranslate
#include "vpmu-inst.hpp"      // Inst_Stream
#include "vpmu-cache.hpp"     // Cache_Stream
#include "vpmu-branch.hpp"    // Branch_Stream

#include "json.hpp" // nlohmann::json
using json = nlohmann::json;

// The global variable that controls all the vpmu streams.
std::vector<VPMUStream *> vpmu_streams = {};
// File for VPMU to dump results
FILE *vpmu_log_file = NULL;
// The definition of the only one global variable passing data around.
struct VPMU_Struct VPMU;
// A pointer to current Extra TB Info
ExtraTBInfo *vpmu_current_extra_tb_info;
// QEMU log system use these two variables
#ifdef CONFIG_QEMU_VERSION_0_15
extern FILE *logfile;
extern int   loglevel;
#else
extern FILE *qemu_logfile;
extern int   qemu_loglevel;
#endif

static std::string get_file_contents(const char *filename)
{
    std::ifstream in(filename, std::ios::in);
    if (in) {
        std::string contents;
        in.seekg(0, std::ios::end);
        contents.resize(in.tellg());
        in.seekg(0, std::ios::beg);
        in.read(&contents[0], contents.size());
        in.close();
        return (contents);
    }
    ERR_MSG("File not found: %s\n", filename);
    exit(EXIT_FAILURE);
}

static json VPMU_load_json(const char *vpmu_config_file)
{
    // Read file in
    std::string vpmu_config_str = get_file_contents(vpmu_config_file);

    // Parse json
    auto j = json::parse(vpmu_config_str);
    // DBG("%s\n", j.dump(4).c_str());

    return j;
}

static inline void
attach_vpmu_stream(VPMUStream &s, nlohmann::json config, std::string name)
{
    // Check its existence and print helpful message to user
    vpmu::utils::json_check_or_exit(config, name);
    // Set the default implementation for each stream
    s.set_stream_impl();
    // Initialize all trace stream channels with its configuration
    s.build(config[name]);
    // Push pointers of all trace streams.
    // The pointer must point to bss section, i.e. global variable.
    // Thus, this is a safe pointer which would never be dangling.

    vpmu_streams.push_back(&s);
    return;
}

static void vpmu_core_init(const char *vpmu_config_file)
{
    try {
        json vpmu_config = VPMU_load_json(vpmu_config_file);

        attach_vpmu_stream(vpmu_inst_stream, vpmu_config, "cpu_models");
        attach_vpmu_stream(vpmu_branch_stream, vpmu_config, "branch_models");
        attach_vpmu_stream(vpmu_cache_stream, vpmu_config, "cache_models");

        // TODO remove this test code
        // sleep(2);
        // for (auto vs: vpmu_streams) {
        //     vs->destroy();
        // }
        // vpmu_inst_stream.build(vpmu_config["cpu_models"]);
        // vpmu_branch_stream.build(vpmu_config["branch_models"]);
        // vpmu_cache_stream.build(vpmu_config["cache_models"]);
        // sleep(2);
    } catch (std::invalid_argument e) {
        ERR_MSG("%s\n", e.what());
        exit(EXIT_FAILURE);
    } catch (std::domain_error e) {
        ERR_MSG("%s\n", e.what());
        exit(EXIT_FAILURE);
    }

#ifdef CONFIG_VPMU_SET
// vpmu_process_tracking_init();
#endif

    // TODO try to move JIT modular and not requiring this!!!
    CacheStream::Model       cache_model  = vpmu_cache_stream.get_model(0);
    InstructionStream::Model cpu_model    = vpmu_inst_stream.get_model(0);
    BranchStream::Model      branch_model = vpmu_branch_stream.get_model(0);
    VPMU.platform.cpu.frequency           = cpu_model.frequency;
    std::function<void(std::string)> func(
      [=](auto i) { std::cout << i << cpu_model.name << std::endl; });
    vpmu_inst_stream.async(func, "hello async callback\n");
    // After this line, the configs from simulators are synced!!!
    CONSOLE_LOG(STR_VPMU "VPMU configurations:\n");
    CONSOLE_LOG(STR_VPMU "    CPU Model    : %s\n", cpu_model.name);
    CONSOLE_LOG(STR_VPMU "    # cores      : %d\n", VPMU.platform.cpu.cores);
    CONSOLE_LOG(STR_VPMU "    frequency    : %" PRIu64 " MHz\n", cpu_model.frequency);
    CONSOLE_LOG(STR_VPMU "    dual issue   : %s\n", cpu_model.dual_issue ? "y" : "n");
    CONSOLE_LOG(STR_VPMU "    branch lat   : %u\n", branch_model.latency);
    CONSOLE_LOG(STR_VPMU "    Cache model  : %s\n", cache_model.name);
    CONSOLE_LOG(STR_VPMU "    # levels     : %d\n", cache_model.levels);
    CONSOLE_LOG(STR_VPMU "    latency (exclusive) :\n");
    for (int i = cache_model.levels; i > 0; i--) {
        CONSOLE_LOG(STR_VPMU "\t    L%d  : %d\n", i, cache_model.latency[i]);
    }
    // sleep(2);
    // exit(0);
}

extern "C" {

void VPMU_sync_non_blocking(void)
{
    for (auto s : vpmu_streams) {
        s->sync_none_blocking();
    }
}

void VPMU_sync(void)
{
    for (auto s : vpmu_streams) {
        s->sync();
    }
}

void VPMU_reset(void)
{
    VPMU.cpu_idle_time_ns                = 0;
    VPMU.ticks                           = 0;
    VPMU.iomem_count                     = 0;
    VPMU.modelsel.total_tb_visit_count   = 0;
    VPMU.modelsel.cold_tb_visit_count    = 0;
    VPMU.modelsel.hot_tb_visit_count     = 0;
    VPMU.modelsel.hot_dcache_read_count  = 0;
    VPMU.modelsel.hot_dcache_write_count = 0;
    VPMU.modelsel.hot_icache_count       = 0;

    for (auto s : vpmu_streams) {
        s->reset();
    }
}

void VPMU_dump_result(void)
{
    VPMU_sync();

    CONSOLE_LOG("==== Program Profile ====\n\n");
    CONSOLE_LOG("   === QEMU/ARM ===\n");
    vpmu_dump_readable_message();
}

void enable_QEMU_log()
{
    int mask = (1 << 1); // This means CPU_LOG_TB_IN_ASM from cpu-all.h

#ifdef CONFIG_QEMU_VERSION_0_15
    loglevel |= mask;
    logfile = vpmu_log_file;
#else
    qemu_loglevel |= mask;
    qemu_logfile = vpmu_log_file;
#endif
}

void disable_QEMU_log()
{
    int mask = (1 << 1); // This means CPU_LOG_TB_IN_ASM from cpu-all.h

#ifdef CONFIG_QEMU_VERSION_0_15
    loglevel &= ~mask;
    logfile = NULL;
#else
    qemu_loglevel &= ~mask;
    qemu_logfile = NULL;
#endif
}

void VPMU_init(int argc, char **argv)
{
    char vpmu_config_file[1024] = {0};

    for (int i = 0; i < (argc - 1); i++) {
        if (std::string(argv[i]) == "-vpmu-config") strcpy(vpmu_config_file, argv[i + 1]);
        if (std::string(argv[i]) == "-smp") VPMU.platform.cpu.cores = atoi(argv[i + 1]);
    }

    // Set to 1 if (1) no -smp presents. (2) the argument after smp is not a number.
    if (VPMU.platform.cpu.cores == 0) VPMU.platform.cpu.cores = 1;
    if (strlen(vpmu_config_file) == 0) {
        ERR_MSG("VPMU Config File Path is not set!!\n"
                "\tPlease specify '-vpmu-config <PATH>' when executing QEMU\n\n");
        exit(EXIT_FAILURE);
    }

    if (vpmu_log_file == NULL) vpmu_log_file = fopen("/tmp/vpmu.log", "w");

    // this would let print system support comma.
    setlocale(LC_NUMERIC, "");
    // enable_QEMU_log();
    // VPMU.thpool = thpool_init(1);
    DBG(STR_VPMU "Thread Pool Initialized\n");
    vpmu_core_init(vpmu_config_file);
    CONSOLE_LOG(STR_VPMU "Initialized\n");
}

uint64_t vpmu_target_time_ns(void)
{
    return vpmu::target::time_ns();
}

void vpmu_dump_readable_message(void)
{
    using namespace vpmu::host;
    using namespace vpmu::target;

// These two are for making info formatable and maintainable
#define CONSOLE_U64(str, val) CONSOLE_LOG(str " %'" PRIu64 "\n", (uint64_t)val)
#define CONSOLE_TME(str, val) CONSOLE_LOG(str " %'lf sec\n", (double)val / 1000000000.0)
    CONSOLE_LOG("Instructions:\n");
    vpmu_inst_stream.dump();
    CONSOLE_LOG("Branch:\n");
    vpmu_branch_stream.dump();
    CONSOLE_LOG("CACHE:\n");
    vpmu_cache_stream.dump();

    CONSOLE_U64("HOT TB      :", VPMU.modelsel.hot_tb_visit_count);
    CONSOLE_U64("COLD TB     :", VPMU.modelsel.cold_tb_visit_count);
    CONSOLE_LOG("\n");
    CONSOLE_LOG("Timing Info:\n");
    CONSOLE_TME("  ->CPU                        :", cpu_time_ns());
    CONSOLE_TME("  ->Branch                     :", branch_time_ns());
    CONSOLE_TME("  ->Cache                      :", cache_time_ns());
    CONSOLE_TME("  ->System memory              :", memory_time_ns());
    CONSOLE_TME("  ->I/O memory                 :", io_time_ns());
    CONSOLE_TME("  ->Idle                       :", VPMU.cpu_idle_time_ns);
    CONSOLE_TME("Estimated execution time       :", time_ns());

    CONSOLE_LOG("\n");
    CONSOLE_TME("Emulation Time :", wall_clock_period());
    CONSOLE_LOG("MIPS           : %'0.2lf\n\n",
                (double)vpmu_total_inst_count() / (wall_clock_period() / 1000.0));

#if 0
    CONSOLE_LOG("Memory:\n");
    CONSOLE_U64("  ->System memory access       :", (vpmu_L1_dcache_miss_count()
                                                 + vpmu_L1_icache_miss_count()));
    CONSOLE_U64("  ->System memory cycles       :", vpmu_sys_mem_access_cycle_count());
    CONSOLE_U64("  ->I/O memory access          :", vpmu->iomem_count);
    CONSOLE_U64("  ->I/O memory cycles          :", vpmu_io_mem_access_cycle_count());
    CONSOLE_U64("Total Cycle count              :", vpmu_cycle_count());
    //Remember add these infos into L1I READ
    CONSOLE_LOG("Model Selection:\n");
    CONSOLE_U64("  ->JIT icache access          :", (vpmu->hot_icache_count));
    CONSOLE_U64("  ->JIT dcache access          :", (vpmu->hot_dcache_read_count + vpmu->hot_dcache_write_count));
    CONSOLE_U64("  ->VPMU icache access         :", vpmu_L1_icache_access_count());
    CONSOLE_U64("  ->VPMU icache misses         :", vpmu_L1_icache_miss_count());
    CONSOLE_U64("  ->VPMU dcache access         :", vpmu_L1_dcache_access_count());
    CONSOLE_U64("  ->VPMU dcache read misses    :", vpmu_L1_dcache_read_miss_count());
    CONSOLE_U64("  ->VPMU dcache write misses   :", vpmu_L1_dcache_write_miss_count());
    CONSOLE_U64("  ->hotTB                      :", VPMU.hot_tb_visit_count);
    CONSOLE_U64("  ->coldTB                     :", VPMU.cold_tb_visit_count);
    */
#endif
#undef CONSOLE_TME
#undef CONSOLE_U64
}

} // End of extern "C"
