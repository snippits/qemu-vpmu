#include "function-tracing.hpp" // Tracing user functions and custom callbacks
#include "event-tracing.hpp"    // Event tracing functions

void ft_register_callbacks(std::shared_ptr<ET_Process> process)
{
    uint64_t addr = process->get_symbol_addr("mmap");
    process->functions.register_call(addr, [](void* env, ET_Process* self) {
        // NOTE: Do NOT use frame pointer here since compiler flag can turn that off.
        self->pc_called_mmap = et_get_ret_addr(env);
        DBG(STR_PROC "'%s'(pid %lu) called mmap from PC: %lx\n",
            self->name.c_str(),
            self->pid,
            self->pc_called_mmap);
    });
}
