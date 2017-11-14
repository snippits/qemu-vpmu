#include "vpmu.hpp"                   // Include common C and C++ headers
#include "phase/phase.hpp"            // Phase class
#include "phase/phase-detect.hpp"     // PhaseDetect class
#include "phase/phase-classifier.hpp" // PhaseClassfier
#include "event-tracing.hpp"          // EventTracer
#include "vpmu-insn.hpp"              // InsnStream
#include "vpmu-cache.hpp"             // CacheStream
#include "vpmu-branch.hpp"            // BranchStream
#include "vpmu-snapshot.hpp"          // VPMUSanpshot
#include "vpmu-log.hpp"               // CONSOLE_LOG, VPMULog

// Put your own phase classifier below
#include "phase/phase-classifier-nn.hpp" // NearestCluster classifier

// Put you own phase classifier above

Phase Phase::not_found = Phase();

PhaseDetect phase_detect(DEFAULT_WINDOW_SIZE, std::make_unique<NearestCluster>());

// TODO Output better features for machine learning
nlohmann::json Phase::json_fingerprint(void)
{
    nlohmann::json j;

    // Use a separate array to prevent the ordering problem in json
    j["keys"]   = {"aluOp", "bitOp", "load", "store", "branch"};
    j["values"] = {(double)counters.alu / counters.insn,
                   (double)counters.bit / counters.insn,
                   (double)counters.load / counters.insn,
                   (double)counters.store / counters.insn,
                   (double)counters.branch / counters.insn};

    return j;
}

static void update_phase(std::shared_ptr<ET_Process>& process, const Window& window)
{
    // Classify the window to phase
    auto& res = phase_detect.classify(process->phase_list, window);
    if (res == Phase::not_found) {
        process->phase_list.push_back(process->next_phase_id());
    }
    // Referencing the existing or the newly created phase
    auto& phase = (res != Phase::not_found) ? res : process->phase_list.back();

    phase.update(window);
    uint64_t core_id = vpmu::get_core_id();
    VPMU_async([core_id, &phase, process, window]() {
        VPMUSnapshot new_snapshot(true, core_id);
        // Update the counter values of this phase
        phase.update(new_snapshot - process->snapshot_phase);
        // Update the last checkpoint of this process (for phase detection only)
        process->snapshot_phase = new_snapshot;
        // Update process phase history
        process->phase_history.push_back({{window.timestamp,        // Construct an array
                                           window.target_timestamp, // with {{ ... }}
                                           phase.id}});
    });

    if (res == Phase::not_found) {
        DBG(STR_PHASE "pid: %" PRIu64 ", name: %s\n" STR_PHASE
                      "Create new phase id %zu @ Timestamp: %lu ms\n",
            process->pid,
            process->name.c_str(),
            phase.id,
            window.timestamp / 1000);
    }
}

static inline bool update_window(Window& window, const ExtraTBInfo* extra_tb_info)
{
    window.update(extra_tb_info);
    if (window.instruction_count > phase_detect.get_window_size()) {
        return true;
    }
    return false;
}

void phasedet_ref(bool               last_tb_user_mode,
                  bool               user_mode,
                  const ExtraTBInfo* extra_tb_info,
                  uint64_t           stack_ptr,
                  uint64_t           core_id)
{
#ifdef CONFIG_VPMU_DEBUG_MSG
    static uint64_t window_cnt = 0;
#endif

    uint64_t pid     = (user_mode) ? VPMU.core[vpmu::get_core_id()].current_pid : 0;
    auto     process = event_tracer.find_process(pid);
    if (process != nullptr) {
        if (!user_mode) return; // Currently we do not support kernel mode phase profiling
        if (user_mode && !last_tb_user_mode) {
            // Change from kernel mode to user mode
        } else if (!user_mode && last_tb_user_mode) {
            // Change from user mode to kernel mode
        }
        bool flag_w = update_window(process->current_window, extra_tb_info);
        if (flag_w) update_phase(process, process->current_window);
        if (flag_w) process->current_window.reset();
        process->stack_ptr = stack_ptr;
#ifdef CONFIG_VPMU_DEBUG_MSG
        if (flag_w) {
            window_cnt++;
            if (window_cnt % 100 == 0)
                DBG(STR_PHASE "Timestamp (# windows): %'9" PRIu64 "\n", window_cnt);
        }
#endif
    }
    return;
}
