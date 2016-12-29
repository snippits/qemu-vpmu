#include "vpmu/include/vpmu.h"
#include "vpmu/include/event-tracing/event-tracing.h"

void et_check_function_call(CPUARMState *env, uint64_t target_addr, uint64_t return_addr)
{
    // TODO Move this to another place and thread safe and need to check branch!!!!!!!
    CPUState *cs                = CPU(ENV_GET_CPU(env));
    VPMU.cs                     = cs;
    static uint32_t current_pid = 0;

    switch (et_find_kernel_event(target_addr)) {
    case ET_KERNEL_FORK: {
        // Linux Kernel: Fork a process
        // DBG("fork from %d\n", current_pid);
        break;
    }
    case ET_KERNEL_WAKE_NEW_TASK: {
        // Linux Kernel: wake up the newly forked process
        // This is kernel v3.6.11
        // uint32_t target_pid = vpmu_read_uint32_from_guest(cs, env->regs[0], 204);
        // This is kernel v4.4.0
        uint32_t target_pid = vpmu_read_uint32_from_guest(cs, env->regs[0], 512);
        if (current_pid != 0 && et_find_traced_pid(current_pid)) {
            et_attach_to_parent_pid(current_pid, target_pid);
        }
        break;
    }
    case ET_KERNEL_EXECV: {
        // Linux Kernel: New process creation
        // This is kernel v3.6.11
        // char *filepath = (char *)vpmu_read_uintptr_from_guest(cs, env->regs[0], 0);
        // This is kernel v4.4.0
        uintptr_t name_addr =
          (uintptr_t)vpmu_read_uintptr_from_guest(cs, env->regs[0], 0);
        char *filepath = (char *)vpmu_read_ptr_from_guest(cs, name_addr, 0);
        if (et_find_traced_process(filepath)) {
            // if (VPMU.traced_process_name[0] != '\0'
            //     && strstr(filepath, VPMU.traced_process_name) != NULL) {
            // DBG("target_addr == %x from %x\n", target_addr, return_addr);
            // DBG("file: %s (pid=%d)\n", filepath, current_pid);
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
        uint32_t task_ptr = vpmu_read_uint32_from_guest(cs, env->regs[2], 12);
#else
#pragma message("VPMU SET: 64 bits Not supported!!")
#endif
        // current_pid = vpmu_read_uint32_from_guest(cs, task_ptr, 204); //This is kernel
        // v3.6.11
        current_pid =
          vpmu_read_uint32_from_guest(cs, task_ptr, 512); // This is kernel v4.4.0
        // ERR_MSG("pid = %x %d\n", env->regs[2], current_pid);

        // Switching VPMU when current process is traced
        if (et_find_traced_pid(current_pid))
            VPMU.enabled = true;
        else
            VPMU.enabled = false;

        break;
    }
    default:
        break;
    }
}
