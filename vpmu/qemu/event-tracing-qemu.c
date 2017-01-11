#include "vpmu/include/vpmu.h"
#include "vpmu/include/vpmu-device.h"
#include "vpmu/include/event-tracing/event-tracing.h"
#include "vpmu/include/linux-mm.h"

#if defined(TARGET_ARM)
static inline void __append_str(char *buff, int *position, int size_buff, const char *str)
{
    int i = 0;
    for (i = 0; str[i] != '\0'; i++) {
        if (*position >= size_buff) break;
        buff[*position] = str[i];
        *position += 1;
    }
}

static void parse_dentry_path(CPUArchState *env,
                              uintptr_t     dentry_addr,
                              char *        buff,
                              int *         position,
                              int           size_buff,
                              int           max_levels)
{
    uintptr_t parent_dentry_addr = 0;
    char *    name;

    // Safety checks
    if (env == NULL || buff == NULL || position == NULL || size_buff == 0) return;
    // Stop condition 1 (reach user defined limition or null pointer)
    if (max_levels == 0 || dentry_addr == 0) return;
    name = (char *)vpmu_read_ptr_from_guest(env, dentry_addr, 44);
    // Stop condition 2 (reach root directory)
    if (name[0] == '\0') return;
    if (name[0] == '/' && name[1] == '\0') return;

    // Find parent node (dentry->d_parent)
    parent_dentry_addr = vpmu_read_uintptr_from_guest(env, dentry_addr, 16);
    parse_dentry_path(env, parent_dentry_addr, buff, position, size_buff, max_levels - 1);
    // Append path/name
    name = (char *)vpmu_read_ptr_from_guest(env, dentry_addr, 44);
    __append_str(buff, position, size_buff, "/");
    __append_str(buff, position, size_buff, name);

    return;
}

static inline void print_mode(uint64_t mode, uint64_t mask, const char *message)
{
    if (mode & mask) {
        CONSOLE_LOG("%s", message);
    }
}

void et_check_function_call(CPUArchState *env, uint64_t target_addr, uint64_t return_addr)
{
    // TODO make this thread safe and need to check branch!!!!!!!
    VPMU.cpu_arch_state         = env;
    static uint64_t current_pid = 0;

    switch (et_find_kernel_event(target_addr)) {
    case ET_KERNEL_MMAP: {
        // Linux Kernel: Mmap a file or shared library
        // DBG(STR_VPMU "fork from %lu\n", current_pid);
        char      fullpath[1024] = {0};
        int       position       = 0;
        uintptr_t dentry_addr    = 0;
        uintptr_t mode           = 0;

        if (env->regs[0] == 0) break; // vaddr is zero
        dentry_addr = vpmu_read_uintptr_from_guest(env, env->regs[0], 12);
        if (dentry_addr == 0) break; // pointer to dentry is zero
        parse_dentry_path(env, dentry_addr, fullpath, &position, sizeof(fullpath), 64);
        mode = env->regs[3];
        /*
        DBG(STR_VPMU "mmap file: %s mode: (%lx) ", fullpath, mode);
#ifdef CONFIG_VPMU_DEBUG_MSG
        print_mode(mode, VM_READ, " READ");
        print_mode(mode, VM_WRITE, " WRITE");
        print_mode(mode, VM_EXEC, " EXEC");
        print_mode(mode, VM_SHARED, " SHARED");
        print_mode(mode, VM_IO, " IO");
        print_mode(mode, VM_HUGETLB, " HUGETLB");
        print_mode(mode, VM_DONTCOPY, " DONTCOPY");
#endif
        DBG("\n");
        */
        et_add_process_mapped_file(current_pid, fullpath, mode);
        break;
    }
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
