#ifndef __VPMU_EVENT_TRACING_HPP_
#define __VPMU_EVENT_TRACING_HPP_

extern "C" {
#include "config-target.h" // Target configuration
#include "vpmu-common.h"   // Common headers and macros
#include "event-tracing.h" // C header
#include "vpmu.h"          // vpmu_clone_qemu_cpu_state
}

#include <string>       // std::string
#include <vector>       // std::vector
#include <utility>      // std::forward
#include <map>          // std::map
#include <algorithm>    // std::remove_if
#include "vpmu-log.hpp" // Log system

// TODO Make child forked child traced and could be found!!!!!!!!!!
// TODO VPMU timing model switch
class ET_Program
{
public:
    ET_Program(std::string&& new_name) { name = new_name; }
    ~ET_Program() {}

    inline bool operator==(const ET_Program& rhs) { return (this == &rhs); }
    inline bool operator!=(const ET_Program& rhs) { return !(this == &rhs); }

    bool compare_name(std::string full_path)
    {
        std::size_t found = full_path.find(name);
        return (found != std::string::npos);
    }

    void add_symbol(std::string name, uint64_t address)
    {
        sym_table.insert(std::pair<std::string, uint64_t>(name, address));
    }

public:
    // Used too often, make it public for speed and convenience.
    std::string name;
    // The not found instance for find and iterator
    static ET_Program not_found;
    // The timing model bind to this program
    uint64_t timing_model;
    // The function address table
    std::map<std::string, uint64_t> sym_table;
};

class ET_Process
{
public:
    ET_Process(ET_Program& program, uint64_t new_pid)
    {
        process_name = program.name;
        pid          = new_pid;
        binary_list.push_back(program);
        timing_model = program.timing_model;
    }
    ET_Process(std::string&& new_name, uint64_t new_pid)
    {
        process_name = new_name;
        pid          = new_pid;
    }
    ~ET_Process()
    {
        // TODO this is not free yet
        if (cpu_state != nullptr) free(cpu_state);
    }

    inline bool operator==(const ET_Process& rhs) { return (this == &rhs); }
    inline bool operator!=(const ET_Process& rhs) { return !(this == &rhs); }

    bool compare_name(std::string full_path)
    {
        std::size_t found = full_path.find(process_name);
        return (found != std::string::npos);
    }

    void attach_child_pid(uint64_t child_pid) { children_pid.push_back(child_pid); }

    // CPUState *
    inline void set_cpu_state(void* cs)
    {
        if (cpu_state == nullptr) {
            cpu_state = vpmu_clone_qemu_cpu_state(cs);
        } else {
            vpmu_update_qemu_cpu_state(cs, cpu_state);
        }
    }

public:
    // Used too often, make it public for speed and convenience.
    std::string process_name;
    // The root pid
    uint64_t pid = 0;
    // The not found instance for find and iterator
    static ET_Process not_found;
    // The timing model bind to this program
    uint64_t timing_model = 0;

private:
    std::vector<ET_Program> binary_list;
    std::vector<uint64_t>   children_pid;
    void*                   cpu_state = nullptr; // CPUState *
};

class ET_Kernel : public ET_Program
{
public:
    ET_Kernel() : ET_Program("kernel") {}
    ~ET_Kernel() {}

    ET_KERNEL_EVENT_TYPE find_event(uint64_t vaddr)
    {
        for (int i = 0; i < ET_KERNEL_EVENT_COUNT; i++) {
            if (kernel_event_table[i] == vaddr) return (ET_KERNEL_EVENT_TYPE)i;
        }
        return ET_KERNEL_NONE;
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
    ~EventTracer() {}
    EventTracer(const char* module_name) : VPMULog(module_name) {}
    EventTracer(std::string module_name) : VPMULog(module_name) {}
    // EventTracer is neither copyable nor movable.
    EventTracer(const EventTracer&) = delete;
    EventTracer& operator=(const EventTracer&) = delete;

    inline void add_program(std::string&& name)
    {
        program_list.push_back(ET_Program(std::forward<std::string>(name)));
    }

    inline void remove_program(std::string name)
    {
        // The STL way cost a little more time than hand coding, but it's much more SAFE!
        program_list.erase(
          std::remove_if(program_list.begin(),
                         program_list.end(),
                         [&](ET_Program& p) { return p.compare_name(name); }),
          program_list.end());
    }

    inline void add_new_process(const char* path, uint64_t pid)
    {
        auto& program = find_program(path);
        // Push the program info into the list even it's not found
        processes.push_back({program, pid});
        return;
    }

    ET_Program& find_program(const char* path)
    {
        // Match full path first
        for (auto& p : program_list) {
            if (p.compare_name(path)) return p;
        }
        const char* name = nullptr;
        for (int i = 0; path[i] != '\0'; i++) {
            if (path[i] == '/') {
                name = &path[i + 1];
            }
        }
        if (name == nullptr) name = path;
        // Match program name
        for (auto& p : program_list) {
            if (p.compare_name(name)) return p;
        }
        return ET_Program::not_found;
    }

    // TODO find child, remove child pid.
    // And... only remove parent process when all child processes are leaved
    inline void remove_process(uint64_t pid)
    {
        // The STL way cost a little more time than hand coding, but it's much more SAFE!
        processes.erase(std::remove_if(processes.begin(),
                                       processes.end(),
                                       [&](ET_Process& p) { return p.pid == pid; }),
                        processes.end());
    }

    inline ET_Process& find_process(uint64_t pid)
    {
        for (auto& p : processes) {
            if (p.pid == pid) return p;
        }
        return ET_Process::not_found;
    }

    inline ET_Process& find_process(const char* path)
    {
        // Match full path first
        for (auto& p : processes) {
            if (p.compare_name(path)) return p;
        }
        const char* name = nullptr;
        for (int i = 0; path[i] != '\0'; i++) {
            if (path[i] == '/') {
                name = &path[i + 1];
            }
        }
        if (name == nullptr) name = path;
        // Match program name
        for (auto& p : processes) {
            if (p.compare_name(name)) return p;
        }
        return ET_Process::not_found;
    }

    inline void attach_to_parent(uint64_t parent_pid, uint64_t child_pid)
    {
        for (auto& p : processes) {
            if (p.pid == parent_pid) {
                p.attach_child_pid(child_pid);
            }
        }
    }

    void set_process_cpu_state(uint64_t pid, void* cs)
    {
        auto& process = find_process(pid);
        process.set_cpu_state(cs);
    }

    ET_Kernel& get_kernel(void) { return kernel; }

    void parse_and_set_kernel_symbol(const char* filename);

private:
    ET_Kernel               kernel;
    std::vector<ET_Process> processes;
    std::vector<ET_Program> program_list;
};

extern EventTracer event_tracer;

#endif // __VPMU_EVENT_TRACING_HPP_
