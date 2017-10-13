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

void Phase::update_snapshot(VPMUSnapshot& process_snapshot)
{
    uint64_t core_id = vpmu::get_core_id();
    VPMU_async([this, core_id, &process_snapshot]() {
        VPMUSnapshot new_snapshot(true, core_id);
        this->snapshot += new_snapshot - process_snapshot;
        process_snapshot = new_snapshot;
    });
}

inline void Window::update_vector(uint64_t pc)
{
    // Get the hased index for current pc address
    uint64_t hashed_key = vpmu::math::simple_hash(pc / 4, branch_vector.size());
    branch_vector[hashed_key]++;
}

inline void Window::update_counter(const ExtraTBInfo* extra_tb_info)
{
    counters.insn += extra_tb_info->counters.total;
    counters.load += extra_tb_info->counters.load;
    counters.store += extra_tb_info->counters.store;
    counters.alu += extra_tb_info->counters.alu;
    counters.bit += extra_tb_info->counters.bit;
    counters.branch += extra_tb_info->has_branch;
}

inline void Window::update(const ExtraTBInfo* extra_tb_info)
{
    uint64_t pc     = extra_tb_info->start_addr;
    uint64_t pc_end = pc + extra_tb_info->counters.size_bytes;
    // Update timestamp if this window is cleared before,
    // which means this is a new window.
    if (timestamp == 0) timestamp = vpmu_get_timestamp_us();
    update_vector(pc);
    update_counter(extra_tb_info);
    instruction_count += extra_tb_info->counters.total;

    Pair_beg_end key = {pc, pc_end};
    code_walk_count[key] += 1;
}

static void
update_phase(uint64_t pc, std::shared_ptr<ET_Process>& process, Window& window)
{
    // Classify the window to phase
    auto& phase = phase_detect.classify(process->phase_list, window);
    if (phase == Phase::not_found) {
        uint64_t phase_num = process->phase_list.size();
        // Add a new phase to the process
        DBG(
          STR_PHASE "pid: %" PRIu64 ", name: %s\n", process->pid, process->name.c_str());
        DBG(STR_PHASE "Create new phase id %zu @ PC: %p and Timestamp: %lu ms\n",
            phase_num,
            (void*)pc,
            window.timestamp / 1000);
        // Construct the instance inside vector (save time)
        process->phase_list.push_back(window);
        // We must update the snapshot to get the correct result
        auto&& new_phase = process->phase_list.back();
        new_phase.update_snapshot(process->snapshot_phase);
        new_phase.id = phase_num;
        process->phase_history.push_back({{window.timestamp, phase_num}});
    } else {
        uint64_t phase_num = (&phase - &process->phase_list[0]);
        // DBG(STR_PHASE "Update phase id %zu\n", phase_num);
        phase.update(window);
        phase.update_snapshot(process->snapshot_phase);
        process->phase_history.push_back({{window.timestamp, phase_num}});
    }
}

/*
static void
push_new_sub_phase(uint64_t pc, std::shared_ptr<ET_Process>& process, Window& window)
{
    uint64_t phase_num = process->phase_list.size();
    // Empty list, create a new phase with sub phase
    // Construct the instance inside vector (save time)
    process->phase_list.push_back(window);
    // We must update the snapshot to get the correct result
    auto&& new_phase = process->phase_list.back();
    new_phase.update_snapshot(process->snapshot_phase);
    new_phase.id             = phase_num;
    new_phase.sub_phase_flag = true;
    // DBG(STR_PHASE "pid: %" PRIu64 ", name: %s\n", et_current_pid,
    // process->name.c_str());
    // DBG(STR_PHASE "Create new sub-phase @ PC: %p\n", (void*)pc);
}
*/

static void
update_window(uint64_t pid, const ExtraTBInfo* extra_tb_info, uint64_t stack_ptr)
{
#ifdef CONFIG_VPMU_DEBUG_MSG
    static uint64_t window_cnt = 0;
#endif
    uint64_t pc = extra_tb_info->start_addr;

    auto process = event_tracer.find_process(pid);
    if (process != nullptr) {
        // The process is being traced. Do phase detection.
        auto& current_window = process->current_window;
        // uint64_t last_sp        = process->stack_ptr;
        current_window.update(extra_tb_info);

        if (current_window.instruction_count > phase_detect.get_window_size()) {
#ifdef CONFIG_VPMU_DEBUG_MSG
            window_cnt++;
            if (window_cnt % 100 == 0)
                DBG(STR_PHASE "Timestamp (# windows): %'9" PRIu64 "\n", window_cnt);
#endif
            auto& last_phase = process->phase_list.back();
            if (process->phase_list.size() != 0 && last_phase.sub_phase_flag == true) {
                last_phase.sub_phase_flag = false;
            }
            update_phase(pc, process, current_window);
            // Reset all counters and vars of current window
            current_window.reset();
        }
        /*
        else {
            if (stack_ptr < last_sp) {
                auto& last_phase = process->phase_list.back();
                if (process->phase_list.size() == 0
                    || last_phase.sub_phase_flag == false) {
                    // Create a new sub-phase
                    push_new_sub_phase(pc, process, current_window);
                } else {
                    last_phase.update(current_window);
                    if (last_phase.get_insn_count() > phase_detect.get_window_size()) {
#ifdef CONFIG_VPMU_DEBUG_MSG
                        window_cnt++;
                        if (window_cnt % 100 == 0)
                            DBG(STR_PHASE "Timestamp (# windows): %'9" PRIu64 "\n",
                                window_cnt);
#endif
                        last_phase.update_snapshot(process->snapshot_phase);
                        // Promote the last sub phase to a regular phase
                        last_phase.sub_phase_flag = false;
                        auto& phase =
                          phase_detect.classify(process->phase_list, last_phase);
                        if (phase == Phase::not_found) {
                            DBG(STR_PHASE "pid: %" PRIu64 ", name: %s\n",
                                process->pid,
                                process->name.c_str());
                            DBG(
                              STR_PHASE
                              "Create new phase id %zu @ PC: %p and Timestamp: %lu ms\n",
                              last_phase.id,
                              (void*)pc,
                              current_window.timestamp / 1000);
                            process->phase_history.push_back(
                              [current_window.timestamp, last_phase.id]);
                        } else {
                            // Merge two phases
                            phase.update(last_phase);
                            process->phase_list.pop_back();
                            process->phase_history.push_back(
                              [current_window.timestamp, phase.id]);
                        }
                    }
                }
                // Reset all counters and vars of current window
                current_window.reset();
            }
        }
    */

        process->stack_ptr = stack_ptr;
    }
}

void phasedet_ref(bool               user_mode,
                  const ExtraTBInfo* extra_tb_info,
                  uint64_t           stack_ptr,
                  uint64_t           core_id)
{
    // TODO This is not good for coding style
    static bool last_tb_is_user[VPMU_MAX_CPU_CORES] = {false};
    if (!user_mode) {
        // kernel_phasedet_ref((last_tb_is_user == true), extra_tb_info);
    } else {
        uint64_t pid = VPMU.core[vpmu::get_core_id()].current_pid;
        if (last_tb_is_user[core_id] == false) {
            // Kernel IRQ to User mode
            // auto process = event_tracer.find_process((uint64_t)0);
            // if (process != nullptr)
            //    process->phase_history.push_back([vpmu_get_timestamp_us(), -1]);
        }
        update_window(pid, extra_tb_info, stack_ptr);
    }
    last_tb_is_user[core_id] = user_mode;

    return;
}
