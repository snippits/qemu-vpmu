#include "vpmu/include/vpmu.h"
#include "vpmu/include/vpmu-device.h"
#include "vpmu/include/event-tracing/event-tracing.h"

#if defined(TARGET_ARM)
void et_check_function_call(CPUArchState *env, uint64_t target_addr, uint64_t return_addr)
{
    // TODO make this thread safe and need to check branch!!!!!!!
    VPMU.cpu_arch_state         = env;
    static uint64_t current_pid = 0;

    switch (et_find_kernel_event(target_addr)) {
    case ET_KERNEL_FORK: {
        // Linux Kernel: Fork a process
        // DBG(STR_VPMU "fork from %lu\n", current_pid);
        break;
    }
    case ET_KERNEL_WAKE_NEW_TASK: {
        // Linux Kernel: wake up the newly forked process
        // This is kernel v3.6.11
        // uint32_t target_pid = vpmu_read_uint32_from_guest(env, env->regs[0], 204);
        // This is kernel v4.4.0
        uint32_t target_pid = vpmu_read_uint32_from_guest(env, env->regs[0], 512);
        if (current_pid != 0 && et_find_traced_pid(current_pid)) {
            et_attach_to_parent_pid(current_pid, target_pid);
        }
        break;
    }
    case ET_KERNEL_EXECV: {
        // Linux Kernel: New process creation
        // This is kernel v3.6.11
        // char *filepath = (char *)vpmu_read_uintptr_from_guest(env, env->regs[0], 0);
        // This is kernel v4.4.0
        uintptr_t name_addr =
          (uintptr_t)vpmu_read_uintptr_from_guest(env, env->regs[0], 0);
        char *filepath = (char *)vpmu_read_ptr_from_guest(env, name_addr, 0);

        if (et_find_program_in_list(filepath)) {
            DBG(STR_VPMU "target_addr == %lx from %lx\n", target_addr, return_addr);
            DBG(STR_VPMU "file: %s (pid=%lu)\n", filepath, current_pid);
            tic(&(VPMU.start_time));
            VPMU_reset();
            vpmu_simulator_status(&VPMU);
            et_add_new_process(filepath, current_pid);
            VPMU.enabled = 1;
        }
        break;
    }
    case ET_KERNEL_EXIT: {
        // Linux Kernel: Process End
        et_remove_process(current_pid);
        break;
    }
    case ET_KERNEL_CONTEXT_SWITCH: {
// Linux Kernel: Context switch
#if TARGET_LONG_BITS == 32
        uint32_t task_ptr = vpmu_read_uint32_from_guest(env, env->regs[2], 12);
#else
#pragma message("VPMU SET: 64 bits Not supported!!")
#endif
        // This is kernel v3.6.11
        // current_pid = vpmu_read_uint32_from_guest(env, task_ptr, 204);
        // This is kernel v4.4.0
        current_pid = vpmu_read_uint32_from_guest(env, task_ptr, 512);
        // ERR_MSG("pid = %lx %lu\n", (uint64_t)env->regs[2], current_pid);

        if (et_find_traced_pid(current_pid)) {
            et_set_process_cpu_state(current_pid, env);
            VPMU.enabled = true;
        } else {
            // Switching VPMU when current process is traced
            if (vpmu_model_has(VPMU_WHOLE_SYSTEM, VPMU))
                VPMU.enabled = true;
            else
                VPMU.enabled = false;
        }

        break;
    }
    default:
        break;
    }
}

#else
void et_check_function_call(CPUArchState *env, uint64_t target_addr, uint64_t return_addr)
{
    // TODO try to integrate this function cross-architecture
    // by separating magic read_uint32... from this function to other functions
}
#endif
