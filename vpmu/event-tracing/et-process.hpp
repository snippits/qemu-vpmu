#ifndef __VPMU_PROCESS_HPP_
#define __VPMU_PROCESS_HPP_
#pragma once

#include <string>    // std::string
#include <vector>    // std::vector
#include <utility>   // std::forward
#include <map>       // std::map
#include <algorithm> // std::remove_if

#include "vpmu.hpp"             // VPMU common headers
#include "vpmu-utils.hpp"       // miscellaneous functions
#include "et-program.hpp"       // ET_Program class
#include "et-memory-region.hpp" // ET_MemoryRegion class for linux/mm
#include "phase/phase.hpp"      // Phase class
#include "function-map.hpp"     // FunctionMap class
#include "json.hpp"             // nlohmann::json

typedef struct {
    uint64_t vaddr;
    uint64_t len;
    uint64_t mode;
    char     fullpath[1024];
} MMapInfo;

// NOTE that binary_list[0] always exists and is the main program
//
class ET_Process : public ET_Path
{
public:
    ET_Process() = delete; // A process can not be null initialized

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

        binary_list.push_back(std::make_shared<ET_Program>(new_name));
    }
    ET_Process(ET_Process& target_process, uint64_t new_pid)
    {
        name         = target_process.name;
        filename     = target_process.filename;
        path         = target_process.path;
        pid          = new_pid;
        binary_list  = target_process.binary_list;
        timing_model = target_process.timing_model;
    }
    ~ET_Process() { vpmu_qemu_free_cpu_arch_state(cpu_state); }

    inline bool operator==(const ET_Process& rhs) { return (this == &rhs); }
    inline bool operator!=(const ET_Process& rhs) { return !(this == &rhs); }

    // CPUState *
    inline void set_cpu_state(void* cs)
    {
        // Update the core number that runs this process
        core_id = vpmu::get_core_id();
        if (cpu_state == nullptr) {
            cpu_state = vpmu_qemu_clone_cpu_arch_state(cs);
        } else {
            // TODO Do we need to update this? It's heavy (about 100 KB)
            // vpmu_qemu_update_cpu_arch_state(cs, cpu_state);
        }
    }

    uint64_t get_symbol_addr(std::string name);

    bool call_event(void* env, uint64_t vaddr);

    inline std::shared_ptr<ET_Program> get_main_program(void) { return binary_list[0]; }

    inline void set_last_mapped_info(MMapInfo new_info)
    {
        last_mapped_addr[vpmu::get_core_id()] = new_info;
    }

    inline MMapInfo& get_last_mapped_info(void)
    {
        return last_mapped_addr[vpmu::get_core_id()];
    }

    inline void clear_last_mapped_info(void)
    {
        last_mapped_addr[vpmu::get_core_id()] = {};
    }

    inline uint64_t next_phase_id(void) { return unique_phase_id++; }

    void dump_vm_map(std::string path);

    void dump(void);
    void dump_process_info(std::string path);
    void dump_phases(std::string path);
    void dump_timeline(std::string path);
    void dump_phase_similarity(std::string path);
    // Use std::map to sort the output
    std::map<std::string, uint64_t> get_code_mapping(const Phase& phase);

    std::string find_code_line_number(uint64_t pc);

    void push_child_process(std::shared_ptr<ET_Process>& process);
    void push_binary(std::shared_ptr<ET_Program>& program);

    void append_debug_log(std::string mesg);

    const std::string& get_debug_log(void) { return debug_log; }

public:
    bool     is_running     = false; ///< Set to true if it is currently running
    bool     is_top_process = false; ///< Identify if this is the top parent
    uint64_t pid            = 0;     ///< The pid of this process
    uint64_t stack_ptr      = 0;     ///< The current stack pointer of this process
    uint64_t timing_model   = 0;     ///< The timing model bound to this program
    bool     binary_loaded  = false; ///< Flag to indicate whether main program is set
    uint64_t core_id        = 0;     ///< Identify the core it runs on
    uint64_t pc_called_mmap = 0;     ///< Remember the PC of calling mmap
    /// Phases of this process
    uint64_t host_launchtime  = vpmu::host::timestamp_us();
    uint64_t guest_launchtime = 0;

    /// \brief Snapshot of timing counters
    //
    /// When a process object is created, a snapshot will be taken in order to
    /// snapshot the start time of this process.
    VPMUSnapshot snapshot = {};
    /// Snapshot of timing counters for phase detection
    VPMUSnapshot snapshot_phase = {};
    /// Remember the profiling counters of this process
    VPMUSnapshot prof_counters = {};
    /// Process memory map
    ET_MemoryRegion vm_maps = {};
    /// Process memory map without unmap (just for showing to users)
    ET_MemoryRegion max_vm_maps = {};
    /// Binaries bound to this process. (vector of shared pointers)
    std::vector<std::shared_ptr<ET_Program>> binary_list = {};
    /// Processes forked by this process. (vector of shared pointers)
    std::vector<std::shared_ptr<ET_Process>> child_list = {};

    /// Phases of this process
    std::vector<Phase> phase_list = {};
    /// The current window of this process
    Window current_window = {};
    /// History records of phase ID with a timestamp. pair<hosttime, guesttime, phase ID>
    std::vector<std::array<uint64_t, 3>> phase_history = {};
    /// History records of events with a timestamp. pair<hosttime, guesttime, event ID>
    std::vector<std::array<uint64_t, 3>> event_history = {};
    // The monitored functions of this process
    FunctionMap<uint64_t, void*, ET_Process*> functions;

private:
    /// \brief A pointer of type "CPUState *" from QEMU.
    //
    /// This pointer can be used to translate guest VA to host VA.
    /// i.e. accessing guest data from host.
    void* cpu_state = nullptr;
    /// Used for debugging (logging) things happened on this process
    std::string debug_log = "";
    /// Remember the pointer to lastest mapped region for updating its address at return
    MMapInfo last_mapped_addr[VPMU_MAX_CPU_CORES] = {};
    /// Unique phase id counting from 1
    uint64_t unique_phase_id = 1;
};

#endif
