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
#include <mutex>           // Mutex
#include "vpmu-log.hpp"    // Log system
#include "vpmu-utils.hpp"  // Misc. functions
#include "phase/phase.hpp" // Phase class
#include "et-path.hpp"     // ET_Path class
#include "et-program.hpp"  // ET_Program class
#include "et-process.hpp"  // ET_Process class

#include "vpmu-snapshot.hpp" // VPMUSanpshot

// TODO Use weak_ptr to implement a use_count() tester to check
// if all programs, processes are free normally

class ET_Kernel : public ET_Program
{
    using fun_callback = std::function<void(void* env)>;

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

    bool call_event(uint64_t vaddr, void* env, uint64_t core_id)
    {
        for (int i = 0; i < ET_KERNEL_EVENT_COUNT; i++) {
            if (kernel_event_table[i] == vaddr) {
                // DBG(STR_VPMU "Found event-%d \n",i);
                cb[i].fun(env);
                if (cb[i].fun_ret)
                    event_return_table[core_id].push_back(
                      {et_get_ret_addr(env), cb[i].fun_ret});
                return true;
            }
            if (event_return_table[core_id].size() > 0
                && event_return_table[core_id].back().first == vaddr) {
                auto& r = event_return_table[core_id].back();
                r.second(env);
                event_return_table[core_id].pop_back();
            }
        }
        return false;
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

    void register_callback(ET_KERNEL_EVENT_TYPE event, fun_callback f)
    {
        cb[event].fun = f;
    }

    void register_return_callback(ET_KERNEL_EVENT_TYPE event, fun_callback f)
    {
        cb[event].fun_ret = f;
    }

    uint64_t get_running_pid() { return VPMU.core[vpmu::get_core_id()].current_pid; }

    uint64_t get_running_pid(uint64_t core_id)
    {
        return VPMU.core[vpmu::get_core_id()].current_pid;
    }

private:
    uint64_t kernel_event_table[ET_KERNEL_EVENT_COUNT] = {0};
    std::vector<std::pair<uint64_t, fun_callback>> event_return_table[VPMU_MAX_CPU_CORES];
    struct {
        fun_callback fun;
        fun_callback fun_ret;
    } cb[ET_KERNEL_EVENT_COUNT] = {};
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
        auto                        p = std::make_shared<ET_Program>(name);
        std::lock_guard<std::mutex> lock(program_list_lock);

        log_debug("Add new binary '%s'", name.c_str());
        program_list.push_back(p);
        // debug_dump_program_map();
        return p;
    }

    inline std::shared_ptr<ET_Program> add_library(std::string name)
    {
        auto                        p = std::make_shared<ET_Program>(name);
        std::lock_guard<std::mutex> lock(program_list_lock);

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
        std::lock_guard<std::mutex> lock(program_list_lock);

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
        auto                        program = find_program(name);
        std::lock_guard<std::mutex> lock(process_id_map_lock);

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
            std::lock_guard<std::mutex> lock(program_list_lock);

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
        std::lock_guard<std::mutex> lock(process_id_map_lock);

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
        std::lock_guard<std::mutex> lock(process_lock);

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
        std::lock_guard<std::mutex> lock(program_list_lock);

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
        std::lock_guard<std::mutex> lock(process_lock);
        auto                        process = find_process(pid);
        if (process != nullptr) process->set_cpu_state(cs);
    }

    ET_Kernel& get_kernel(void) { return kernel; }

    // Return 0 when parse fail, return linux version number when succeed
    uint64_t parse_and_set_kernel_symbol(const char* filename);

    void clear_shared_libraries(void)
    {
        std::lock_guard<std::mutex> lock(program_list_lock);
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
    // This mutex protects: process_id_map
    std::mutex process_id_map_lock;
    // This mutex protects: process
    std::mutex process_lock;
    // This mutex protects: program_list
    std::mutex program_list_lock;
};

extern EventTracer event_tracer;

#endif // __VPMU_EVENT_TRACING_HPP_
