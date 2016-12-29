#include "event-tracing.hpp" // Header

EventTracer tracer;

enum ET_KERNEL_EVENT_TYPE et_find_kernel_event(uint64_t vaddr)
{
    return ET_KERNEL_NONE;
}

void et_add_program_to_list(const char* name)
{
    tracer.add_program(name);
}

void et_remove_program_from_list(const char* name)
{
    tracer.remove_program(name);
}

bool et_find_traced_pid(uint64_t pid)
{
    return tracer.find_traced_process(pid);
}

bool et_find_traced_process(const char* name)
{
    return tracer.find_traced_process(name);
}

void et_attach_to_parent_pid(uint64_t parent_pid, uint64_t child_pid)
{
    return tracer.attach_to_parent(parent_pid, child_pid);
}

void et_add_new_process(const char* name, uint64_t pid)
{
    tracer.add_new_process(name, pid);
}

void et_remove_process(uint64_t pid)
{
    tracer.remove_process(pid);
    /*
    int flag_hit = 0;
    // Loop through pid list to find if hit
    for (int i = 0; traced_pid[i] != 0; i++) {
        if (current_pid == traced_pid[i]) {
            flag_hit = 1;
            break;
        }
    }
    if (flag_hit) VPMU.num_traced_threads--;
    if (flag_hit && VPMU.num_traced_threads == 0) {
        VPMU_sync();
        toc(&(VPMU.start_time), &(VPMU.end_time));
        VPMU_dump_result();
        memset(traced_pid, 0, sizeof(VPMU.traced_process_pid));
        VPMU.enabled = 0;
        flag_tracing = 0;
    }*/
}

