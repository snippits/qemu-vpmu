#ifndef __VPMU_EVENT_TRACING_HPP_
#define __VPMU_EVENT_TRACING_HPP_

extern "C" {
#include "config-target.h" // Target configuration
#include "vpmu-common.h"   // Common headers and macros
#include "event-tracing.h" // C header
#include "vpmu.h"          // vpmu_clone_qemu_cpu_state
}

#include <string>         // std::string
#include <vector>         // std::vector
#include <utility>        // std::forward
#include <map>            // std::map
#include <algorithm>      // std::remove_if
#include "vpmu-log.hpp"   // Log system
#include "vpmu-utils.hpp" // Misc. functions

class ET_Path
{
public:
    // This function return true to all full_path containing the name,
    // expecially when name is assigned as relative path.
    bool compare_name(std::string full_path)
    {
        std::size_t found = full_path.find(name);
        return (found != std::string::npos);
    }

    // Compare only the filename of the name
    bool compare_file_name(std::string file_name)
    {
        int index = vpmu::utils::get_index_of_file_name(name.c_str());
        if (index < 0) return false;
        std::size_t found = file_name.find(name.substr(index));
        return (found != std::string::npos);
    }

public:
    // Used too often, make it public for speed and convenience.
    std::string name;
};

// TODO VPMU timing model switch
class ET_Program : public ET_Path
{
public:
    ET_Program(std::string new_name) { name = new_name; }
    ~ET_Program() {}

    inline bool operator==(const ET_Program& rhs) { return (this == &rhs); }
    inline bool operator!=(const ET_Program& rhs) { return !(this == &rhs); }

    void add_symbol(std::string name, uint64_t address)
    {
        sym_table.insert(std::pair<std::string, uint64_t>(name, address));
    }

public:
    // The timing model bind to this program
    uint64_t timing_model;
    // The function address table
    std::map<std::string, uint64_t> sym_table;
    // Lists of shared pointer objects of dependent libraries
    std::vector<std::shared_ptr<ET_Program>> library_list;
};

class ET_Process : public ET_Path
{
public:
    // This program should be the main program
    ET_Process(std::shared_ptr<ET_Program>& program, uint64_t new_pid)
    {
        name = program->name;
        pid  = new_pid;
        binary_list.push_back(program);
        timing_model   = program->timing_model;
        is_top_process = true;
    }
    ET_Process(std::string new_name, uint64_t new_pid)
    {
        name           = new_name;
        pid            = new_pid;
        is_top_process = true;
    }
    ET_Process(ET_Process* target_process, uint64_t new_pid)
    {
        name         = target_process->name;
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
        if (program != nullptr) binary_list.push_back(program);
    }

    void push_child_process(std::shared_ptr<ET_Process>& process)
    {
        if (process != nullptr) child_list.push_back(process);
    }

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
            if (kernel_event_table[i] == vaddr) return (ET_KERNEL_EVENT_TYPE)i;
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

    inline void add_program(std::string name)
    {
        log_debug("Add new binary %s", name.c_str());
        program_list.push_back(std::make_shared<ET_Program>(name));
        debug_dump_program_map();
    }

    inline void remove_program(std::string name)
    {
        if (program_list.size() == 0) return;
        log_debug("Try remove program %s", name.c_str());
        // The STL way cost a little more time than hand coding, but it's much more SAFE!
        program_list.erase(std::remove_if(program_list.begin(),
                                          program_list.end(),
                                          [&](std::shared_ptr<ET_Program>& p) {
                                              return p->compare_name(name);
                                          }),
                           program_list.end());
        debug_dump_program_map();
    }

    inline void add_new_process(const char* path, uint64_t pid)
    {
        auto program = find_program(path);
        // Push the program info into the list
        if (program != nullptr) {
            log_debug("Start new process %s, pid:%5" PRIu64, path, pid);
            auto&& process      = std::make_shared<ET_Process>(program, pid);
            process_id_map[pid] = process;
            debug_dump_process_map();
        }
        return;
    }

    std::shared_ptr<ET_Program> find_program(const char* path)
    {
        if (path == nullptr) return nullptr;
        if (path[0] == '/') {
            // It's an absolute path
            for (auto& p : program_list) {
                if (p->compare_name(path)) return p;
            }
        } else {
            // It's an relative path
            int index = vpmu::utils::get_index_of_file_name(path);

            for (auto& p : program_list) {
                if (p->compare_file_name(&path[index])) return p;
            }
        }

        return nullptr;
    }

    inline void remove_process(uint64_t pid)
    {
        if (process_id_map.size() == 0) return;
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
        if (path[0] == '/') {
            // It's an absolute path
            for (auto& p_pair : process_id_map) {
                auto& p = p_pair.second;
                if (p->compare_name(path)) return p;
            }
        } else {
            // It's an relative path
            int index = vpmu::utils::get_index_of_file_name(path);

            for (auto& p_pair : process_id_map) {
                auto& p = p_pair.second;
                if (p->compare_file_name(&path[index])) return p;
            }
        }

        return nullptr;
    }

    inline void attach_to_parent(uint64_t parent_pid, uint64_t child_pid)
    {
        log_debug("Attach process %5" PRIu64 " to %5" PRIu64, child_pid, parent_pid);
        for (auto& p : process_id_map) {
            if (p.first == parent_pid) {
                p.second->attach_child_pid(child_pid);
            }
        }
        debug_dump_process_map();
    }

    void set_process_cpu_state(uint64_t pid, void* cs)
    {
        auto process = find_process(pid);
        if (process != nullptr) process->set_cpu_state(cs);
    }

    ET_Kernel& get_kernel(void) { return kernel; }

    void parse_and_set_kernel_symbol(const char* filename);

    void debug_dump_process_map(void)
    {
#ifdef CONFIG_VPMU_DEBUG_MSG
        log_debug("Printing all traced processes with its information "
                  "(first level of children only)");
        for (auto& p_pair : process_id_map) {
            auto& p = p_pair.second;
            if (p_pair.first != p->pid)
                log_fatal("PID not match!! map(%p) != p(%p)", p_pair.first, p->pid);
            log_debug("    pid:%5" PRIu64 ", Pointer %p Name:%s",
                      p->pid,
                      p.get(),
                      p->name.c_str());
            debug_dump_child_list(*p);
            log_debug("    Binaries loaded into this process");
            debug_dump_binary_list(*p);
            log_debug("    ==============================================");
        }
#endif
    }

    void debug_dump_program_map(void)
    {
#ifdef CONFIG_VPMU_DEBUG_MSG
        log_debug("Printing all traced program with its information"
                  "(first level of dependent libraries only)");
        for (auto& program : program_list) {
            log_debug("    Pointer %p, Name:%s, size of sym_table:%d"
                      ", num libraries loaded:%d",
                      program.get(),
                      program->name.c_str(),
                      program->sym_table.size(),
                      program->library_list.size());
            debug_dump_library_list(*program);
            log_debug("    ============================================");
        }
#endif
    }

    void debug_dump_child_list(const ET_Process& process)
    {
#ifdef CONFIG_VPMU_DEBUG_MSG
        for (auto& child : process.child_list) {
            log_debug("        pid:%5" PRIu64 ", Pointer %p, Name:%s",
                      child->pid,
                      child.get(),
                      child->name.c_str());
        }
#endif
    }

    void debug_dump_binary_list(const ET_Process& process)
    {
#ifdef CONFIG_VPMU_DEBUG_MSG
        for (auto& binary : process.binary_list) {
            log_debug("        Pointer %p, Name:%s, size of sym_table:%d"
                      ", num libraries loaded:%d",
                      binary.get(),
                      binary->name.c_str(),
                      binary->sym_table.size(),
                      binary->library_list.size());
        }
#endif
    }

    void debug_dump_library_list(const ET_Program& program)
    {
#ifdef CONFIG_VPMU_DEBUG_MSG
        for (auto& binary : program.library_list) {
            log_debug("        Pointer %p, Name:%s, size of sym_table:%d",
                      binary.get(),
                      binary->name.c_str(),
                      binary->sym_table.size());
        }
#endif
    }

private:
    ET_Kernel kernel;
    std::map<uint64_t, std::shared_ptr<ET_Process>> process_id_map;
    std::vector<std::shared_ptr<ET_Program>> program_list;
};

extern EventTracer event_tracer;

#endif // __VPMU_EVENT_TRACING_HPP_
