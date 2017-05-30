#include "vpmu/vpmu.h"
#include "vpmu/event-tracing/event-tracing.h"

void et_check_function_call(CPUArchState *env, uint64_t target_addr)
{
    CPUState *cs = CPU(ENV_GET_CPU(env));
    et_kernel_call_event(target_addr, env, cs->cpu_index);
    return;
}
