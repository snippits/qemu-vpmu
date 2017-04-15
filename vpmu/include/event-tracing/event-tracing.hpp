#ifndef __VPMU_EVENT_TRACING_HPP_
#define __VPMU_EVENT_TRACING_HPP_

extern "C" {
#include "config-target.h" // Target configuration
#include "vpmu-common.h"   // Common headers and macros
#include "event-tracing.h" // C header
#include "vpmu.h"          // vpmu_clone_qemu_cpu_state
}

#include <string>          // std::string
#include <vector>          // std::vector
#include <utility>         // std::forward
#include <map>             // std::map
#include <algorithm>       // std::remove_if
#include "vpmu-log.hpp"    // Log system
#include "vpmu-utils.hpp"  // Misc. functions
#include "phase/phase.hpp" // Phase class

#include "vpmu-snapshot.hpp" // VPMUSanpshot

// TODO Use weak_ptr to implement a use_count() tester to check
// if all programs, processes are free normally

// TODO The current implementation of name comparison does not apply well
// when the number of monitored/traced binary is large
// Maybe use formally two names and two paths
class ET_Path
{
public:
    // This function return true to all full_path containing the name,
    // expecially when name is assigned as relative path.
    bool fuzzy_compare_name(std::string full_path)
    {
        // Compare the path first
        if (compare_path(full_path) == true) return true;
        // Compare bash name first
        if (compare_bash_name(full_path) == true) return true;
        // Compare file name latter
        if (compare_file_name(full_path) == true) return true;
        return false;
    }

    bool compare_name(std::string full_path)
    {
        if (path.size() != 0) { // Match full path if the path exists
            return compare_path(full_path);
        } else { // Match only the file name if path does not exist
            // Compare bash name first
            if (compare_bash_name(full_path) == true) return true;
            // Compare file name latter
            if (compare_file_name(full_path) == true) return true;
        }
        return false;
    }

    // Compare only the bash name of the name
    bool compare_bash_name(std::string name_or_path)
    {
        return compare_file_name_and_path(name, name_or_path);
    }

    // Compare only the filename of the name
    bool compare_file_name(std::string name_or_path)
    {
        return compare_file_name_and_path(filename, name_or_path);
    }

    // Compare only the full path
    inline bool compare_path(std::string path_only) { return (path == path_only); }

    void set_name_or_path(std::string& new_name)
    {
        int index = vpmu::utils::get_index_of_file_name(new_name.c_str());
        if (index < 0) return; // Maybe we should throw?
        filename             = new_name.substr(index);
        name                 = new_name.substr(index);
        if (index != 0) path = new_name;
    }

    void set_name(std::string& new_name) { name = new_name; }

public:
    // Used too often, make it public for speed and convenience.
    // if bash_name and its binary name are not the same, bash name would be used
    std::string name;
    // This is the file name in the path
    std::string filename;
    // This is always the true (not symbolic link) path to the file
    std::string path;

private:
    inline bool compare_file_name_and_path(std::string& name, std::string& path)
    {
        int i = name.size() - 1, j = path.size() - 1;
        // Empty string means no file name, return false in this case.
        if (i < 0 || j < 0) return false;
        for (; i >= 0 && j >= 0; i--, j--) {
            if (name[i] != path[j]) return false;
        }
        return true;
    }

    // This compares partial_path to full_path, with relative path awareness
    // Ex:  ../../test_set/matrix and /root/test_set/matrix are considered as true.
    // Ex:  /bin/bash and /usr/bin/bash are considered as false.
    inline bool compare_partial_path(std::string& partial_path, std::string& full_path)
    {
        int first_slash_index = 0;
        int i = partial_path.size() - 1, j = full_path.size() - 1, k = 0;

        // Empty string means no file name, return false in this case.
        if (i < 0 || j < 0) return false;
        // Find the first slash if the partial path is a relative path
        if (partial_path[0] == '.') {
            for (k = 0; k < partial_path.size(); k++) {
                if (partial_path[k] != '/' && partial_path[k] != '.')
                    break; // Break when hit the first character of non relative path
                if (partial_path[k] == '/') {
                    first_slash_index = k;
                }
            }
        } else {
            // If both are absolute path, it should be the same
            if (partial_path.size() != full_path.size()) return false;
        }
        // Compare the path to the root slash of partial_path
        for (; i >= first_slash_index && j >= 0; i--, j--) {
            if (partial_path[i] != full_path[j]) return false;
        }
        return true;
    }
};

// TODO VPMU timing model switch
class ET_Program : public ET_Path
{
public:
    ET_Program(std::string new_name) { set_name_or_path(new_name); }
    ~ET_Program() {}

    inline bool operator==(const ET_Program& rhs) { return (this == &rhs); }
    inline bool operator!=(const ET_Program& rhs) { return !(this == &rhs); }

    void add_symbol(std::string name, uint64_t address)
    {
        sym_table.insert(std::pair<std::string, uint64_t>(name, address));
    }

    void push_binary(std::shared_ptr<ET_Program>& program)
    {
        if (program.get() == this) return; // No self include
        // Check repeated pointer
        for (auto& binary : library_list) {
            if (binary == program) return;
        }
        if (program != nullptr) library_list.push_back(program);
    }

    std::string find_code_line_number(uint64_t pc);

    // TODO These two are run time info, should be moved out of here.
    void set_mapped_address(uint64_t start_addr, uint64_t end_addr)
    {
        address_start = start_addr;
        address_end   = end_addr;
        if (address_end < address_start) {
            ERR_MSG("Address range format incorrect\n");
        }
        walk_count_vector.resize(address_end - address_start);
    }

    void reset_walk_count(void)
    {
        // Reset all elements to zeros
        std::fill(walk_count_vector.begin(), walk_count_vector.end(), 0);
    }

public:
    // Caution! All the data should be process independent!!!
    // An ET_Program instance could be shared by multiple processes.
    // All process dependent information should be stored in ET_Process
    struct beg_end_pair {
        uint64_t beg, end;
    };
    // The timing model bind to this program
    uint64_t timing_model;
    // The section address table
    std::map<std::string, struct beg_end_pair> section_table;
    // The function address table
    std::map<std::string, uint64_t> sym_table;
    // The dwarf file and line table
    std::map<uint64_t, std::string> line_table;
    // Lists of shared pointer objects of dependent libraries
    std::vector<std::shared_ptr<ET_Program>> library_list;
    // Used to identify the top process parent
    bool is_shared_library = false;
    // TODO These two are run time info, should be moved out of here.
    // Used to count the walk count
    std::vector<uint32_t> walk_count_vector;
    // Used to identify the mapped virtual address of this program
    uint64_t address_start, address_end;
};

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
    // The root pid
    uint64_t pid = 0;
    // The timing model bind to this program
    uint64_t timing_model = 0;
    // Lists of shared pointer objects
    std::vector<std::shared_ptr<ET_Program>> binary_list;
    std::vector<std::shared_ptr<ET_Process>> child_list;

    std::vector<Phase> phase_list;
    // History records
    std::vector<std::pair<uint64_t, uint64_t>> phase_history;
    Window       current_window;
    VPMUSnapshot snapshot;
    uint64_t     stack_ptr = 0;

private:
    void* cpu_state = nullptr; // CPUState *
};

class ET_Kernel : public ET_Program
{
public:
    ET_Kernel() : ET_Program("kernel") {}

    ET_KERNEL_EVENT_TYPE find_event(uint64_t vaddr)
    {
        for (int i = 0; i < ET_KERNEL_EVENT_COUNT; i++) {
            if (kernel_event_table[i] == vaddr) {
                // DBG(STR_VPMU "Found event-%d \n",i);
                return (ET_KERNEL_EVENT_TYPE)i;
            }
        }
        return ET_KERNEL_NONE;
    }

    uint64_t find_vaddr(ET_KERNEL_EVENT_TYPE event)
    {
        if (event < ET_KERNEL_EVENT_COUNT)
            return kernel_event_table[event];
        else
            return 0;
    }

    void set_event_address(ET_KERNEL_EVENT_TYPE event, uint64_t address)
    {
        kernel_event_table[event] = address;
    }

    void set_symbol_address(std::string sym_name, uint64_t address)
    {
        DBG(STR_VPMU "Set Linux symbol %s @ %lx\n", sym_name.c_str(), address);
        if (sym_name.find("do_execveat_common") != std::string::npos) {
            set_event_address(ET_KERNEL_EXECV, address);
        } else if (sym_name == "__switch_to") {
            set_event_address(ET_KERNEL_CONTEXT_SWITCH, address);
        } else if (sym_name == "do_exit") {
            set_event_address(ET_KERNEL_EXIT, address);
        } else if (sym_name == "wake_up_new_task") {
            set_event_address(ET_KERNEL_WAKE_NEW_TASK, address);
        } else if (sym_name == "_do_fork" || sym_name == "do_fork") {
            set_event_address(ET_KERNEL_FORK, address);
        } else if (sym_name == "mmap_region") {
            set_event_address(ET_KERNEL_MMAP, address);
        }
    }

private:
    uint64_t kernel_event_table[ET_KERNEL_EVENT_COUNT] = {0};
};

class EventTracer : VPMULog
{
public:
    EventTracer() : VPMULog("ET") {}
    EventTracer(const char* module_name) : VPMULog(module_name) {}
    EventTracer(std::string module_name) : VPMULog(module_name) {}
    // EventTracer is neither copyable nor movable.
    EventTracer(const EventTracer&) = delete;
    EventTracer& operator=(const EventTracer&) = delete;

    void update_elf_dwarf(std::shared_ptr<ET_Program> program, const char* file_name);

    inline std::shared_ptr<ET_Program> add_program(std::string name)
    {
        auto p = std::make_shared<ET_Program>(name);
        log_debug("Add new binary '%s'", name.c_str());
        program_list.push_back(p);
        // debug_dump_program_map();
        return p;
    }

    inline std::shared_ptr<ET_Program> add_library(std::string name)
    {
        auto p = std::make_shared<ET_Program>(name);
        // Set this flag to true if it's a library
        p->is_shared_library = true;
        log_debug("Add new library '%s'", name.c_str());
        program_list.push_back(p);
        // debug_dump_program_map();
        return p;
    }

    inline void remove_program(std::string name)
    {
        if (program_list.size() == 0) return;
        log_debug("Try remove program '%s'", name.c_str());
        // The STL way cost a little more time than hand coding,
        // but it's much more SAFE!
        program_list.erase(std::remove_if(program_list.begin(),
                                          program_list.end(),
                                          [&](std::shared_ptr<ET_Program>& p) {
                                              return p->fuzzy_compare_name(name);
                                          }),
                           program_list.end());
        debug_dump_program_map();
    }

    inline std::shared_ptr<ET_Process> add_new_process(const char* name, uint64_t pid)
    {
        auto program = find_program(name);
        // Check if the target program is in the monitoring list
        if (program != nullptr) {
            log_debug("Trace new process %s, pid:%5" PRIu64, name, pid);
            auto&& process      = std::make_shared<ET_Process>(program, pid);
            process->name       = name;
            process_id_map[pid] = process;
            debug_dump_process_map();
            return process;
        } else {
            log_debug("Trace new process %s, pid:%5" PRIu64, name, pid);
            auto&& process      = std::make_shared<ET_Process>(name, pid);
            process->name       = name;
            process_id_map[pid] = process;
            debug_dump_process_map();
            return process;
        }
        return nullptr;
    }

    inline std::shared_ptr<ET_Process>
    add_new_process(const char* path, const char* name, uint64_t pid)
    {
        auto process = add_new_process(name, pid);
        // Check if the target program is in the monitoring list
        if (process != nullptr) {
            auto program  = process->get_main_program();
            auto filename = vpmu::utils::get_file_name_from_path(path);
            if (name != program->filename) {
                log_debug(
                  "Rename program '%s' attributes to: real path '%s', filename '%s'",
                  program->name.c_str(),
                  path,
                  filename.c_str());
                // The program is main program, rename the real path and filename
                program->filename = filename;
                program->path     = path;
            }
        }
        return nullptr;
    }

    std::shared_ptr<ET_Program> find_program(const char* path)
    {
        if (path == nullptr) return nullptr;

        for (auto& p : program_list) {
            if (p->fuzzy_compare_name(path)) return p;
        }
        return nullptr;
    }

    inline void remove_process(uint64_t pid)
    {
        if (process_id_map.size() == 0) return;
#ifdef CONFIG_VPMU_DEBUG_MSG
        // It's not necessary to find, use it in debug only
        if (process_id_map.find(pid) != process_id_map.end()) {
            auto& process = process_id_map[pid];
            debug_dump_program_map(process->binary_list[0]);
            debug_dump_process_map(process);
        }
#endif
        log_debug("Try remove process %5" PRIu64, pid);
        process_id_map.erase(pid);
        debug_dump_process_map();
    }

    inline std::shared_ptr<ET_Process> find_process(uint64_t pid)
    {
        for (auto& p_pair : process_id_map) {
            auto& p = p_pair.second;
            if (p->pid == pid) return p;
        }
        return nullptr;
    }

    inline std::shared_ptr<ET_Process> find_process(const char* path)
    {
        if (path == nullptr) return nullptr;

        for (auto& p_pair : process_id_map) {
            auto& p = p_pair.second;
            if (p->fuzzy_compare_name(path)) return p;
        }
        return nullptr;
    }

    inline void attach_to_parent(std::shared_ptr<ET_Process> parent, uint64_t child_pid)
    {
        if (parent == nullptr) return;

        log_debug("Attach process %5" PRIu64 " to %5" PRIu64, child_pid, parent->pid);
        parent->attach_child_pid(child_pid);
        // debug_dump_process_map();
    }

    inline void attach_to_parent(uint64_t parent_pid, uint64_t child_pid)
    {
        attach_to_parent(find_process(parent_pid), child_pid);
    }

    inline void attach_to_program(std::shared_ptr<ET_Program>  target_program,
                                  std::shared_ptr<ET_Program>& program)
    {
        if (target_program == nullptr) return;

        log_debug("Attach program '%s' to binary '%s'",
                  program->name.c_str(),
                  target_program->name.c_str());
        target_program->push_binary(program);
        // debug_dump_process_map();
    }

    inline void attach_to_program(std::string                  target_program_name,
                                  std::shared_ptr<ET_Program>& program)
    {
        attach_to_program(find_program(target_program_name.c_str()), program);
    }

    void set_process_cpu_state(uint64_t pid, void* cs)
    {
        auto process = find_process(pid);
        if (process != nullptr) process->set_cpu_state(cs);
    }

    ET_Kernel& get_kernel(void) { return kernel; }

    // Return 0 when parse fail, return linux version number when succeed
    uint64_t parse_and_set_kernel_symbol(const char* filename);

    void clear_shared_libraries(void)
    {
        // The STL way cost a little more time than hand coding, but it's much more SAFE!
        program_list.erase(std::remove_if(program_list.begin(),
                                          program_list.end(),
                                          [&](std::shared_ptr<ET_Program>& p) {
                                              return p->is_shared_library;
                                          }),
                           program_list.end());
    }

    void debug_dump_process_map(void);

    void debug_dump_process_map(std::shared_ptr<ET_Process> marked_process);

    void debug_dump_program_map(void);

    void debug_dump_program_map(std::shared_ptr<ET_Program> marked_program);

    void debug_dump_child_list(const ET_Process& process);

    void debug_dump_binary_list(const ET_Process& process);

    void debug_dump_library_list(const ET_Program& program);

private:
    ET_Kernel kernel;
    std::map<uint64_t, std::shared_ptr<ET_Process>> process_id_map;
    std::vector<std::shared_ptr<ET_Program>> program_list;
};

extern EventTracer event_tracer;
extern uint64_t    et_current_pid;

#endif // __VPMU_EVENT_TRACING_HPP_
