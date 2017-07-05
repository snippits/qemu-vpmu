#ifndef __VPMU_PROCESS_HPP_
#define __VPMU_PROCESS_HPP_

#include <string>    // std::string
#include <vector>    // std::vector
#include <utility>   // std::forward
#include <map>       // std::map
#include <algorithm> // std::remove_if

#include "vpmu.hpp"        // VPMU common headers
#include "vpmu-utils.hpp"  // miscellaneous functions
#include "et-program.hpp"  // ET_Program class
#include "phase/phase.hpp" // Phase class

class ET_Process : public ET_Path
{
public:
    // This program should be the main program
    ET_Process(std::shared_ptr<ET_Program>& program, uint64_t new_pid)
    {
        name           = program->name;
        filename       = program->filename;
        path           = program->path;
        pid            = new_pid;
        is_top_process = true;

        binary_list.push_back(program);
        timing_model = program->timing_model;
    }
    ET_Process(std::string new_name, uint64_t new_pid)
    {
        set_name_or_path(new_name);
        pid            = new_pid;
        is_top_process = true;
    }
    ET_Process(ET_Process* target_process, uint64_t new_pid)
    {
        name         = target_process->name;
        filename     = target_process->filename;
        path         = target_process->path;
        pid          = new_pid;
        binary_list  = target_process->binary_list;
        timing_model = target_process->timing_model;
    }
    ~ET_Process() { vpmu_qemu_free_cpu_arch_state(cpu_state); }

    inline bool operator==(const ET_Process& rhs) { return (this == &rhs); }
    inline bool operator!=(const ET_Process& rhs) { return !(this == &rhs); }

    void attach_child_pid(uint64_t child_pid)
    {
        child_list.push_back(std::make_shared<ET_Process>(this, child_pid));
    }

    // CPUState *
    inline void set_cpu_state(void* cs)
    {
        if (cpu_state == nullptr) {
            cpu_state = vpmu_qemu_clone_cpu_arch_state(cs);
        } else {
            // TODO Do we need to update this? It's heavy (about 100 KB)
            // vpmu_qemu_update_cpu_arch_state(cs, cpu_state);
        }
    }

    void push_binary(std::shared_ptr<ET_Program>& program)
    {
        // Check repeated pointer
        for (auto& binary : binary_list) {
            if (binary == program) return;
        }
        if (program != nullptr) binary_list.push_back(program);
    }

    void push_child_process(std::shared_ptr<ET_Process>& process)
    {
        if (process != nullptr) child_list.push_back(process);
    }

    inline std::shared_ptr<ET_Program> get_main_program(void) { return binary_list[0]; }

    std::string find_code_line_number(uint64_t pc);

    void dump_phase_history(void);
    void dump_phase_result(void);
    void dump_process_info(void);
    void dump_phase_code_mapping(FILE* fp, const Phase& phase);

public:
    // Used to identify the top process parent
    bool is_top_process;
    bool mmap_updated_flag = true;
    // The root pid
    uint64_t pid = 0;
    // The timing model bind to this program
    uint64_t timing_model = 0;
    // Lists of shared pointer objects
    std::vector<std::shared_ptr<ET_Program>> binary_list;
    std::vector<std::shared_ptr<ET_Process>> child_list;
    // Remember the pointer to lastest mapped file for updating its address
    std::weak_ptr<ET_Program> last_mapped_file;

    std::vector<Phase> phase_list;
    // History records
    std::vector<std::pair<uint64_t, uint64_t>> phase_history;
    Window       current_window;
    VPMUSnapshot snapshot  = VPMUSnapshot(true); /// Take a snapshot at creation
    uint64_t     stack_ptr = 0;
    // Used for debugging log
    std::string debug_log;

private:
    void* cpu_state = nullptr; // CPUState *
};

#endif
