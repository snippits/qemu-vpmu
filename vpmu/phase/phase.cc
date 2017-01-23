#include "vpmu.hpp"                   // Include common C and C++ headers
#include "phase/phase.hpp"            // Phase class
#include "phase/phase-classifier.hpp" // NearestCluster, and other classifiers
#include "event-tracing.hpp"          // EventTracer
#include "vpmu-insn.hpp"              // InsnStream
#include "vpmu-cache.hpp"             // CacheStream
#include "vpmu-branch.hpp"            // BranchStream
#include "vpmu-snapshot.hpp"          // VPMUSanpshot

Phase Phase::not_found = Phase();

PhaseDetect phase_detect(DEFAULT_WINDOW_SIZE, std::make_unique<NearestCluster>());

void Phase::update_performance_counters(void)
{
    static VPMUSnapshot snapshot;

    // TODO use async call back, the current counters are out of date
    {
        VPMUSnapshot new_snapshot;

        snapshot.accumulate(new_snapshot, insn_data, branch_data, cache_data);
        snapshot = new_snapshot;
    }
}

inline void Window::update_vector(uint64_t pc)
{
    // Get the hased index for current pc address
    uint64_t hashed_key = vpmu::math::simple_hash(pc / 4, branch_vector.size());
    branch_vector[hashed_key]++;
}

void phasedet_ref(bool user_mode, uint64_t pc, const Insn_Counters counters)
{
#ifdef CONFIG_VPMU_DEBUG_MSG
    static uint64_t window_cnt = 0;
#endif
    // Only detect user mode program, exclude the kernel behavior
    if (user_mode == false) return;
    auto process = event_tracer.find_process(et_current_pid);
    if (process != nullptr) {
        // The process is being traced. Do phase detection.
        auto& current_window = process->current_window;
        current_window.update_vector(pc);
        current_window.instruction_count += counters.total;
        if (current_window.instruction_count > phase_detect.get_window_size()) {
#ifdef CONFIG_VPMU_DEBUG_MSG
            window_cnt++;
            if (window_cnt % 100 == 0)
                DBG(STR_PHASE "Timestamp (# windows): %'9" PRIu64 "\n", window_cnt);
#endif
            // Classify the window to phase
            auto& phase = phase_detect.classify(process->phase_list, current_window);
            if (phase == Phase::not_found) {
                // Add a new phase to the process
                DBG(STR_PHASE "pid: %" PRIu64 ", name: %s\n",
                    et_current_pid,
                    process->name.c_str());
                DBG(STR_PHASE "Create new phase id %zu\n", process->phase_list.size());
                process->phase_list.push_back(Phase(current_window));
            } else {
                // DBG(STR_PHASE "Update phase id %zu\n",
                //     (&phase - &process->phase_list[0]));
                phase.update(current_window);
                phase.update_performance_counters();
            }

            // Reset all counters and vars of current window
            current_window.reset();
        }
    }
    return;
}
