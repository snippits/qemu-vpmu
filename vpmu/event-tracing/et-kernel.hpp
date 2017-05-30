#ifndef __VPMU_ET_KERNEL_HPP_
#define __VPMU_ET_KERNEL_HPP_

extern "C" {
#include "kernel-event-cb.h"      // Kernel related header
#include "event-tracing-helper.h" // et_get_ret_addr()
}

#include "vpmu.hpp"       // for vpmu-qemu.h and VPMU struct
#include "vpmu-utils.hpp" // miscellaneous functions
#include "et-program.hpp" // ET_Program class

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
        if (sym_name.find("do_execveat_common") != std::string::npos
            || sym_name.find("do_execve_common") != std::string::npos) {
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

#endif
