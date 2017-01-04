#ifndef __VPMU_EVENT_TRACING_HPP_
#define __VPMU_EVENT_TRACING_HPP_

extern "C" {
#include "config-target.h" // Target configuration
#include "vpmu-common.h"   // Common headers and macros
#include "event-tracing.h" // C header
}

#include <string>    // std::string
#include <vector>    // std::vector
#include <utility>   // std::forward
#include <map>       // std::map
#include <algorithm> // std::remove_if

#include "vpmu-log.hpp" // Log system

// TODO Make child forked child traced and could be found!!!!!!!!!!
// TODO VPMU timing model switch
class ET_Process
{
public:
    ET_Process(std::string&& new_name) { name = new_name; }
    ET_Process(std::string&& new_name, uint64_t new_pid)
    {
        name = new_name;
        pid  = new_pid;
    }
    ~ET_Process() {}

    void attach_child_pid(uint64_t child_pid) { children_pid.push_back(child_pid); }

    // TODO, the input might be a path while the name might be jsut a name
    // Also, try to remove all arguments in case there is one.
    // So, compare name backword.....
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
    // These two are used too often, make it public for speed and convenience.
    std::string name;
    uint64_t    pid;

private:
    std::vector<uint64_t> children_pid;

    uint64_t timing_model;
    std::map<std::string, uint64_t> sym_table;
};

class ET_Kernel : public ET_Process
{
public:
    ET_Kernel() : ET_Process("kernel", 0) {}
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
        program_list.push_back(ET_Process(std::forward<std::string>(name)));
    }

    inline void remove_program(std::string name)
    {
        // The STL way cost a little more time than hand coding, but it's much more SAFE!
        program_list.erase(
          std::remove_if(program_list.begin(),
                         program_list.end(),
                         [&](ET_Process& p) { return p.compare_name(name); }),
          program_list.end());
    }

    inline void add_new_process(std::string name, uint64_t pid)
    {
        for (auto& program : program_list) {
            if (program.compare_name(name)) {
                processes.push_back(program); // Copy Constructor
            }
        }
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

    inline bool find_traced_process(uint64_t pid)
    {
        for (auto& p : processes) {
            if (p.pid == pid) return true;
        }
        return false;
    }

    inline bool find_traced_process(const char* path)
    {
        // Match full path first
        for (auto& p : processes) {
            if (p.compare_name(path)) return true;
        }
        const char* name = nullptr;
        for (int i = 0; path[i] != '\0'; i++) {
            if (path[i] == '/') {
                name = &path[i + 1];
            }
        }
        // Match program name
        for (auto& p : processes) {
            if (p.compare_name(name)) return true;
        }
        return false;
    }

    inline void attach_to_parent(uint64_t parent_pid, uint64_t child_pid)
    {
        for (auto& p : processes) {
            if (p.pid == parent_pid) {
                p.attach_child_pid(child_pid);
            }
        }
    }

    ET_Kernel& get_kernel(void) { return kernel; }

    void parse_and_set_kernel_symbol(const char* filename);

private:
    void*                   cs;
    ET_Kernel               kernel;
    std::vector<ET_Process> processes;
    std::vector<ET_Process> program_list;
};

extern EventTracer event_tracer;

#endif // __VPMU_EVENT_TRACING_HPP_
