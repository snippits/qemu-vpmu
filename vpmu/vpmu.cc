// Libraries
#include <boost/filesystem.hpp> // boost::filesystem
#include "json.hpp"             // nlohmann::json
// VPMU headers
extern "C" {
#include "qemu/vpmu-device.h" // Timing model definition
}
#include "vpmu.hpp"           // VPMU common header
#include "vpmu-stream.hpp"    // VPMUStream, VPMUStream_T
#include "vpmu-translate.hpp" // VPMUTranslate
#include "vpmu-insn.hpp"      // InsnStream
#include "vpmu-cache.hpp"     // CacheStream
#include "vpmu-branch.hpp"    // BranchStream
#include "event-tracing.hpp"  // EventTracer event_tracer
#include "kernel-event-cb.h"  // et_register_callbacks_kernel_events()
#include "phase-detect.hpp"   // phase_detect

// The global variable that controls all the vpmu streams.
std::vector<VPMUStream *> vpmu_streams = {};
// File for VPMU to dump results
FILE *vpmu_log_file = nullptr;
// The definition of the only one global variable passing data around.
struct VPMU_Struct VPMU = {};
// A thread local storage for saving the running core id of each thread
thread_local uint64_t vpmu_running_core_id = 0;
// QEMU log system use these two variables
#ifdef CONFIG_QEMU_VERSION_0_15
extern FILE *logfile;
extern int   loglevel;
#else
extern FILE *qemu_logfile;
extern int   qemu_loglevel;
#endif
// Pointer to argv[0] for modifying process name in htop
char *global_argv_0 = NULL;

static inline void
attach_vpmu_stream(VPMUStream &s, nlohmann::json config, std::string name)
{
    // Check its existence and print helpful message to user
    vpmu::utils::json_check_or_exit(config, name);
    // Set the default implementation for each stream
    s.set_default_stream_impl();
    // Initialize all trace stream channels with its configuration
    s.bind(config[name]);
    // Push pointers of all trace streams.
    // The pointer must point to bss section, i.e. global variable.
    // Thus, this is a safe pointer which would never be dangling.

    vpmu_streams.push_back(&s);
    return;
}

static void init_simulators(const char *vpmu_config_file)
{
    try {
        nlohmann::json vpmu_config = vpmu::utils::load_json(vpmu_config_file);

        attach_vpmu_stream(vpmu_insn_stream, vpmu_config, "cpu_models");
        attach_vpmu_stream(vpmu_branch_stream, vpmu_config, "branch_models");
        attach_vpmu_stream(vpmu_cache_stream, vpmu_config, "cache_models");

    } catch (std::invalid_argument e) {
        ERR_MSG("%s\n", e.what());
        exit(EXIT_FAILURE);
    } catch (std::domain_error e) {
        ERR_MSG("%s\n", e.what());
        exit(EXIT_FAILURE);
    }

    { // Example of changing the implementation of VPMU stream
        auto impl = std::make_unique<VPMUStreamMultiProcess<VPMU_Cache>>(
          "C_Strm", 1024 * 64); // 64K elements
        vpmu_cache_stream.set_stream_impl(std::move(impl));
    }
    // Allocate and build resources.
    for (auto vs : vpmu_streams) {
        vs->build();
    }
    // TODO remove this test code
    // sleep(2);
    // for (auto vs: vpmu_streams) {
    //     vs->destroy();
    // }
    // vpmu_insn_stream.build(vpmu_config["cpu_models"]);
    // vpmu_branch_stream.build(vpmu_config["branch_models"]);
    // vpmu_cache_stream.build(vpmu_config["cache_models"]);
    // sleep(2);

    CacheStream::Model       cache_model  = vpmu_cache_stream.get_model(0);
    InstructionStream::Model cpu_model    = vpmu_insn_stream.get_model(0);
    BranchStream::Model      branch_model = vpmu_branch_stream.get_model(0);
    VPMU.platform.cpu.frequency           = cpu_model.frequency;
    std::function<void(std::string)> func(
      [=](auto i) { std::cout << i << cpu_model.name << std::endl; });
    vpmu_insn_stream.async(func, "hello async callback\n");
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
    // Showing message for one second.
    sleep(1);
    // exit(0);
}

// TODO This would make escape word not functional
static void remove_qemu_quotation(char *path)
{
    int len = strlen(path);
    for (int i = 0; i < len - 1; i++) {
        path[i] = path[i + 1];
    }
    path[len - 2] = '\0';
}

static void prepare_for_logs(void)
{
    if (VPMU.output_path[0] == '\'') {
        remove_qemu_quotation(VPMU.output_path);
    }
    std::string output_path   = std::string(VPMU.output_path);
    std::string log_file_path = output_path + "/vpmu.log";

    // Remove logs from last execution and create folders for current log
    if (boost::filesystem::exists(output_path)) {
        boost::filesystem::remove_all(output_path);
    }
    boost::filesystem::create_directories(output_path);
    boost::filesystem::create_directories(output_path + "/phase");

    if (vpmu_log_file == NULL) vpmu_log_file = fopen(log_file_path.c_str(), "w");
    CONSOLE_LOG(STR_VPMU "Output path for logs and files: %s\n\n", VPMU.output_path);
}

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
    VPMU.cpu_idle_time_ns = 0;
    VPMU.ticks            = 0;
    VPMU.iomem_count      = 0;
    memset(VPMU.modelsel, 0, sizeof(VPMU.modelsel));

    for (auto s : vpmu_streams) {
        s->reset();
    }
}

static inline void print_pass_or_fail(const char *prefix, bool flag)
{
    const char *postfix = (flag) ? "passed" : "failed";
    DBG("%-60s" BASH_COLOR_GREEN "%s" BASH_COLOR_NONE "\n", prefix, postfix);
}

static void vpmu_check_and_print_funs(void)
{
#define PPF(_S_, _V_) print_pass_or_fail(_S_, (_V_ > 0))
#define vaddr(_V_) event_tracer.get_kernel().find_vaddr(_V_)
    DBG("\nImportant variables of Snippits:\n");

    DBG("\nEvent Tracing:\n");
    DBG("------------------------------------------------------------\n");
    PPF("(Kernel) Offsets of file.fpath.dentry", g_linux_offset.file.fpath.dentry);
    PPF("(Kernel) Offsets of dentry.d_iname", g_linux_offset.dentry.d_iname);
    PPF("(Kernel) Offsets of dentry.d_parent", g_linux_offset.dentry.d_parent);
#ifdef TARGET_ARM
    // g_linux_offset.thread_info.task is zero on x86 mode, thus no need to check that.
    PPF("(Kernel) Offsets of thread_info.task", g_linux_offset.thread_info.task);
#endif
    PPF("(Kernel) Offsets of task_struct.pid", g_linux_offset.task_struct.pid);
    PPF("(Kernel) THREAD_SIZE", g_linux_size.stack_thread_size);

    DBG("\nKernel Symbol Addresses:\n");
    DBG("------------------------------------------------------------\n");
    PPF("MMap (mmap_region)", vaddr(ET_KERNEL_MMAP));
    PPF("Fork (do_fork, _do_fork)", vaddr(ET_KERNEL_FORK));
    PPF("Wake New Task (wake_up_new_task)", vaddr(ET_KERNEL_WAKE_NEW_TASK));
    PPF("Execv (do_execveat_common, do_execve_common)", vaddr(ET_KERNEL_EXECV));
    PPF("Exit (do_exit)", vaddr(ET_KERNEL_EXIT));
    PPF("Context Switch (__switch_to)", vaddr(ET_KERNEL_CONTEXT_SWITCH));

    DBG("\n\n");
#undef vaddr
#undef PPF
}

void VPMU_dump_result(void)
{
    VPMU_sync();

    vpmu_check_and_print_funs();

    CONSOLE_LOG("==== Program Profile ====\n\n");
#if defined(TARGET_ARM)
    CONSOLE_LOG("   === QEMU/ARM ===\n");
#elif defined(TARGET_X86_64) || defined(TARGET_I386)
    CONSOLE_LOG("   === QEMU/X86 ===\n");
#else
    CONSOLE_LOG("   === QEMU/NOARCH ===\n");
#endif

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
    char config_file[1024] = {0};
    char kernel_file[1024] = {0};

    global_argv_0 = argv[0];
    // Initialize the path to empty string
    VPMU.output_path[0] = 0;

    tic(&VPMU.program_start_time);
    // Parse arguments
    for (int i = 0; i < argc; i++) {
        if (std::string(argv[i]) == "-vpmu-config") strcpy(config_file, argv[i + 1]);
        if (std::string(argv[i]) == "-smp") VPMU.platform.cpu.cores = atoi(argv[i + 1]);
        if (std::string(argv[i]) == "-enable-kvm") VPMU.platform.kvm_enabled = true;
        if (std::string(argv[i]) == "-vpmu-kernel-symbol")
            strcpy(kernel_file, argv[i + 1]);
        if (std::string(argv[i]) == "-vpmu-output") strcpy(VPMU.output_path, argv[i + 1]);
    }

    // Set default path if it's not set
    if (strlen(VPMU.output_path) == 0) {
        strcpy(VPMU.output_path, "/tmp/snippit");
    }

    // Set arguments
    // Set cores to 1 if (1)no -smp presents. (2)the argument after smp is not a number.
    if (VPMU.platform.cpu.cores == 0) VPMU.platform.cpu.cores = 1;
    if (strlen(config_file) == 0) {
        ERR_MSG("VPMU Config File Path is not set!!\n"
                "\tPlease specify '-vpmu-config <PATH>' when executing QEMU\n\n");
        exit(EXIT_FAILURE);
    }

    // Create folders and files for logging and output
    prepare_for_logs();

#ifdef CONFIG_VPMU_SET
    if (strlen(kernel_file) > 0) {
        VPMU.platform.linux_version =
          event_tracer.parse_and_set_kernel_symbol(kernel_file);
        DBG(STR_VPMU "Running with Linux version %lu.%lu.%lu\n",
            (VPMU.platform.linux_version >> 16) & 0xff,
            (VPMU.platform.linux_version >> 8) & 0xff,
            (VPMU.platform.linux_version >> 0) & 0xff);
    } else {
        CONSOLE_LOG(
          "Path to vmlinux is not set. Boot time kernel tracking will be disabled.\n"
          "\tPlease specify '-vpmu-kernel-symbol <PATH>' for boot time tracking.\n\n");
    }
#endif
    char *env_window_size_str = getenv("PHASE_WINDOW_SIZE");
    if (env_window_size_str != nullptr) {
        int env_window_size = atoi(env_window_size_str);
        if (env_window_size != 0) {
            phase_detect.set_window_size(env_window_size * 1000);
        }
    }

    // this would let print system support comma.
    setlocale(LC_NUMERIC, "");
    // TODO add qemu_log_in_addr_range
    enable_QEMU_log();
    // TODO thread pools and callbacks
    // VPMU.thpool = thpool_init(1);
    DBG(STR_VPMU "Thread Pool Initialized\n");
    // Initialize simulators for each stream
    init_simulators(config_file);
    // Register callbacks for kernel events
    et_register_callbacks_kernel_events();

    // Done
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
    vpmu_insn_stream.dump();
    CONSOLE_LOG("Branch:\n");
    vpmu_branch_stream.dump();
    CONSOLE_LOG("CACHE:\n");
    vpmu_cache_stream.dump();

    if (vpmu_model_has(VPMU_JIT_MODEL_SELECT, VPMU)) {
        int i;
        CONSOLE_LOG("\n\nJIT Model Selection:\n");
        CONSOLE_LOG("  HOT TB       : ");
        for (i = 0; i < VPMU.platform.cpu.cores - 1; i++) {
            CONSOLE_LOG("%" PRIu64 ", ", VPMU.modelsel[i].hot_tb_visit_count);
        }
        CONSOLE_LOG("%" PRIu64 "\n", VPMU.modelsel[i].hot_tb_visit_count);
        CONSOLE_LOG("  COLD TB      : ");
        for (i = 0; i < VPMU.platform.cpu.cores - 1; i++) {
            CONSOLE_LOG("%" PRIu64 ", ", VPMU.modelsel[i].cold_tb_visit_count);
        }
        CONSOLE_LOG("%" PRIu64 "\n", VPMU.modelsel[i].cold_tb_visit_count);
    }
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
                (double)vpmu_total_insn_count() / (wall_clock_period() / 1000.0));
#undef CONSOLE_TME
#undef CONSOLE_U64
}

void vpmu_print_status(VPMU_Struct *vpmu)
{
    vpmu->timing_model &VPMU_WHOLE_SYSTEM ? CONSOLE_LOG("o : ") : CONSOLE_LOG("x : ");
    CONSOLE_LOG("Whole System Profiling\n");

    vpmu->timing_model &VPMU_INSN_COUNT_SIM ? CONSOLE_LOG("o : ") : CONSOLE_LOG("x : ");
    CONSOLE_LOG("Instruction Simulation\n");

    vpmu->timing_model &VPMU_DCACHE_SIM ? CONSOLE_LOG("o : ") : CONSOLE_LOG("x : ");
    CONSOLE_LOG("Data Cache Simulation\n");

    vpmu->timing_model &VPMU_ICACHE_SIM ? CONSOLE_LOG("o : ") : CONSOLE_LOG("x : ");
    CONSOLE_LOG("Insn Cache Simulation\n");

    vpmu->timing_model &VPMU_BRANCH_SIM ? CONSOLE_LOG("o : ") : CONSOLE_LOG("x : ");
    CONSOLE_LOG("Branch Predictor Simulation\n");

    vpmu->timing_model &VPMU_PIPELINE_SIM ? CONSOLE_LOG("o : ") : CONSOLE_LOG("x : ");
    CONSOLE_LOG("Pipeline Simulation\n");

    vpmu->timing_model &VPMU_JIT_MODEL_SELECT ? CONSOLE_LOG("o : ") : CONSOLE_LOG("x : ");
    CONSOLE_LOG("JIT Model Selection\n");

    vpmu->timing_model &VPMU_EVENT_TRACE ? CONSOLE_LOG("o : ") : CONSOLE_LOG("x : ");
    CONSOLE_LOG("VPMU Event Trace mechanism\n");

    vpmu->timing_model &VPMU_PHASEDET ? CONSOLE_LOG("o : ") : CONSOLE_LOG("x : ");
    CONSOLE_LOG("Phase Detection and Profiling\n");
}

uint64_t vpmu_get_core_id(void)
{
    return vpmu_running_core_id;
}

void vpmu_set_core_id(uint64_t core_id)
{
    vpmu_running_core_id = core_id;
}
