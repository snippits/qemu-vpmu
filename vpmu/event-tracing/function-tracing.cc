#include "function-tracing.hpp" // Tracing user functions and custom callbacks
#include "event-tracing.hpp"    // Event tracing functions
#include "config-target.h"      // Include the target architecture definitions

// Register callbacks per-process whenever a new library is loadded
void ft_load_callbacks(std::shared_ptr<ET_Process> process,
                       std::shared_ptr<ET_Program> program)
{
    for (auto& cb : event_tracer.func_callbacks) {
        const std::string& key  = cb.first;
        const auto&        funs = cb.second;
        uint64_t           addr = process->get_symbol_addr(key);
#ifdef TARGET_ARM
        // ARM use the last 2 bits to indicate the mode of instruction sets (Thumb/ARM)
        addr &= 0xFFFFFFFFFFFFFFFC;
#endif
        process->functions.register_all(addr, // Register all callbacks
                                        funs.pre_call,
                                        funs.on_call,
                                        funs.on_return);
    }
}

// Register callbacks globally which works on every process
void ft_register_callbacks(void)
{
    auto& all_process = event_tracer.func_callbacks;

    all_process.register_precall("mmap", [](void* env, ET_Process* self) {
        // NOTE: Do NOT use frame pointer here since compiler flag can turn that off.
        self->pc_called_mmap = et_get_ret_addr(env);
        DBG(STR_PROC "'%s'(pid %lu) called mmap from PC: %lx\n",
            self->name.c_str(),
            self->pid,
            self->pc_called_mmap);
    });
}
