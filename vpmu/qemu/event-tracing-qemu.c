#include "vpmu/include/vpmu.h"
#include "vpmu/include/vpmu-device.h"
#include "vpmu/include/event-tracing/event-tracing.h"
#include "vpmu/include/linux-mm.h"

uint64_t et_current_pid = 0;
// The global variable storing offsets of kernel struct types
LinuxStructOffset g_linux_offset;

// If you want to know more details about the following codes,
// please refer to `man syscall`, section "Architecture calling conventions"
// NOTE that num can not be zero!
static inline target_ulong get_input_arg(CPUArchState *env, int num)
{
#if defined(TARGET_ARM)
    if (num == 1)
        return env->regs[0];
    else if (num == 2)
        return env->regs[1];
    else if (num == 3)
        return env->regs[2];
    else if (num == 4)
        return env->regs[3];
    else if (num >= 5) {
        // Use stack pointer
        // Lower  address
        // [return address] <---- sp
        // [arg 5] <------------- sp + TARGET_LONG_SIZE
        // [arg 6] <------------- sp + TARGET_LONG_SIZE * 2
        // ...
        // Higher address
        return vpmu_read_uintptr_from_guest(
          env, env->regs[13], (num - 5) * TARGET_LONG_SIZE);
    }
#elif defined(TARGET_X86_64) || defined(TARGET_I386)
    if (num == 1)
        return env->regs[R_EDI];
    else if (num == 2)
        return env->regs[R_ESI];
    else if (num == 3)
        return env->regs[R_EDX];
    else if (num == 4)
        return env->regs[R_ECX];
    else if (num == 5)
        return env->regs[8];
    else if (num == 6)
        return env->regs[9];
    else if (num >= 7) {
        // Use stack pointer
        // Lower  address
        // [return address] <---- rsp
        // [arg 7] <------------- rsp + TARGET_LONG_SIZE
        // [arg 8] <------------- rsp + TARGET_LONG_SIZE * 2
        // ...
        // Higher address
        return vpmu_read_uintptr_from_guest(
          env, env->regs[R_ESP], (num - 7 + 1) * TARGET_LONG_SIZE);
    }
#endif
    return 0;
}

static inline target_ulong get_ret_addr(CPUArchState *env)
{
#if defined(TARGET_ARM)
    return (env->regs[14] / 2) * 2; // Clean LSB to 0
#elif defined(TARGET_X86_64) || defined(TARGET_I386)
    return vpmu_read_uintptr_from_guest(env, env->regs[4], 0 * TARGET_LONG_SIZE);
#endif
}

static inline target_ulong get_ret_value(CPUArchState *env)
{
#if defined(TARGET_ARM)
    return env->regs[0];
#elif defined(TARGET_X86_64) || defined(TARGET_I386)
    return env->regs[R_EAX];
#endif
}

static inline target_ulong get_syscall_num(CPUArchState *env)
{
#if defined(TARGET_ARM)
    return env->regs[7];
#elif defined(TARGET_X86_64) || defined(TARGET_I386)
    return env->regs[R_EAX];
#endif
}

static inline target_ulong get_syscall_input_arg(CPUArchState *env, int num)
{
#if defined(TARGET_ARM)
    if (num == 1)
        return env->regs[0];
    else if (num == 2)
        return env->regs[1];
    else if (num == 3)
        return env->regs[2];
    else if (num == 4)
        return env->regs[3];
    else if (num == 5)
        return env->regs[4];
    else if (num == 6)
        return env->regs[5];
#elif defined(TARGET_X86_64) || defined(TARGET_I386)
    if (num == 1)
        return env->regs[R_EDI];
    else if (num == 2)
        return env->regs[R_ESI];
    else if (num == 3)
        return env->regs[R_EDX];
    else if (num == 4)
        return env->regs[10];
    else if (num == 5)
        return env->regs[8];
    else if (num == 6)
        return env->regs[9];
#endif
    return 0;
}

void et_set_linux_struct_offset(uint64_t type, uint64_t value)
{
    switch (type) {
    case VPMU_MMAP_OFFSET_FILE_f_path_dentry:
        g_linux_offset.file.fpath.dentry = value;
        break;
    case VPMU_MMAP_OFFSET_DENTRY_d_iname:
        g_linux_offset.dentry.d_iname = value;
        break;
    case VPMU_MMAP_OFFSET_DENTRY_d_parent:
        g_linux_offset.dentry.d_parent = value;
        break;
    case VPMU_MMAP_OFFSET_THREAD_INFO_task:
        g_linux_offset.thread_info.task = value;
        break;
    case VPMU_MMAP_OFFSET_TASK_STRUCT_pid:
        g_linux_offset.task_struct.pid = value;
        break;
    default:
        ERR_MSG("Undefined type of struct offset %" PRIu64 "\n", type);
        break;
    }
}

void et_set_default_linux_struct_offset(uint64_t version)
{
#if defined(TARGET_ARM)
    if (version == KERNEL_VERSION(4, 4, 0)) {
        // This is kernel v4.4.0
        et_set_linux_struct_offset(VPMU_MMAP_OFFSET_FILE_f_path_dentry, 12);
        et_set_linux_struct_offset(VPMU_MMAP_OFFSET_DENTRY_d_iname, 44);
        et_set_linux_struct_offset(VPMU_MMAP_OFFSET_DENTRY_d_parent, 16);
        et_set_linux_struct_offset(VPMU_MMAP_OFFSET_THREAD_INFO_task, 12);
        et_set_linux_struct_offset(VPMU_MMAP_OFFSET_TASK_STRUCT_pid, 512);
        return;
    }
#elif defined(TARGET_X86_64)
    if (version == KERNEL_VERSION(4, 4, 0)) {
        // This is kernel v4.4.0
        et_set_linux_struct_offset(VPMU_MMAP_OFFSET_FILE_f_path_dentry, 24);
        et_set_linux_struct_offset(VPMU_MMAP_OFFSET_DENTRY_d_iname, 24);
        et_set_linux_struct_offset(VPMU_MMAP_OFFSET_DENTRY_d_parent, 24);
        et_set_linux_struct_offset(VPMU_MMAP_OFFSET_THREAD_INFO_task, 0);
        et_set_linux_struct_offset(VPMU_MMAP_OFFSET_TASK_STRUCT_pid, 1040);
        return;
    }
#endif
    ERR_MSG("This kernel version is not supported for boot time profiling");
}

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
    char *    name               = NULL;

    // Safety checks
    if (env == NULL || buff == NULL || position == NULL || size_buff == 0) return;
    // Stop condition 1 (reach user defined limition or null pointer)
    if (max_levels == 0 || dentry_addr == 0) return;
    name =
      (char *)vpmu_read_ptr_from_guest(env, dentry_addr, g_linux_offset.dentry.d_iname);
    // Stop condition 2 (reach root directory)
    if (name[0] == '\0') return;
    if (name[0] == '/' && name[1] == '\0') return;

    // Find parent node (dentry->d_parent)
    parent_dentry_addr =
      vpmu_read_uintptr_from_guest(env, dentry_addr, g_linux_offset.dentry.d_parent);
    parse_dentry_path(env, parent_dentry_addr, buff, position, size_buff, max_levels - 1);
    // Append path/name
    name =
      (char *)vpmu_read_ptr_from_guest(env, dentry_addr, g_linux_offset.dentry.d_iname);
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

// TODO Find a better way
static uint64_t mmap_ret_addr = 0, last_mmap_len = 0;
static bool     mmap_update_flag = false;

void et_check_mmap_return(CPUArchState *env, uint64_t start_addr)
{
    if (mmap_update_flag && start_addr == mmap_ret_addr) {
        /*
        DBG(STR_VPMU "Mapped Address: 0x%lx to 0x%lx\n",
            (uint64_t)get_ret_value(env),
            (uint64_t)get_ret_value(env) + last_mmap_len);
        */
        // TODO Find a better way
        et_update_last_mmaped_binary(et_current_pid, get_ret_value(env), last_mmap_len);
    }
}

void et_check_function_call(CPUArchState *env, uint64_t target_addr)
{
    // TODO make this thread safe and need to check branch!!!!!!!
    VPMU.cpu_arch_state = env;
    // TODO Maybe there's a better way?
    static uint64_t    exec_event_pid = -1;
    static const char *bash_path      = NULL;

    switch (et_find_kernel_event(target_addr)) {
    // Linux Kernel: Mmap a file or shared library
    case ET_KERNEL_MMAP: {
        // Do nothing if the value is not initialized
        if (g_linux_offset.dentry.d_iname == 0) break;
        // DBG(STR_VPMU "fork from %lu\n", et_current_pid);
        char      fullpath[1024] = {0};
        int       position       = 0;
        uintptr_t dentry_addr    = 0;
        uintptr_t mode           = 0;
        uintptr_t vaddr          = 0;

        if (get_input_arg(env, 1) == 0) break; // vaddr is zero
        dentry_addr = vpmu_read_uintptr_from_guest(
          env, get_input_arg(env, 1), g_linux_offset.file.fpath.dentry);
        if (dentry_addr == 0) break; // pointer to dentry is zero
        parse_dentry_path(env, dentry_addr, fullpath, &position, sizeof(fullpath), 64);
        if (VPMU.platform.linux_version < KERNEL_VERSION(3, 9, 0)) {
            mode = vpmu_read_uintptr_from_guest(env, get_input_arg(env, 5), 0);
        } else {
            mode = get_input_arg(env, 4);
        }
        vaddr = get_input_arg(env, 2);

        mmap_ret_addr    = get_ret_addr(env);
        last_mmap_len    = get_input_arg(env, 3);
        mmap_update_flag = false;
        /*
        DBG(STR_VPMU "mmap file: %s @ %lx mode: (%lx) ", fullpath, vaddr, mode);
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

        if (et_current_pid == exec_event_pid && (mode & VM_READ)) {
            // Mapping executable page for main program
            if (et_find_program_in_list(bash_path)) {
                et_add_new_process(fullpath, bash_path, et_current_pid);
                DBG(STR_VPMU "Start tracing %s, File: %s (pid=%lu)\n",
                    bash_path,
                    fullpath,
                    et_current_pid);
                tic(&(VPMU.start_time));
                VPMU_reset();
                vpmu_print_status(&VPMU);
                VPMU.enabled = 1;
            }

            // The current mapped file is the main program, push it to process anyway
            et_add_process_mapped_file(et_current_pid, fullpath, mode);
            exec_event_pid   = -1;
            mmap_update_flag = true;
        } else {
            // Records all mapped files, including shared library
            if (et_find_traced_pid(et_current_pid)) {
                // Update the mapped vadder for only exec pages
                if (mode & VM_EXEC) mmap_update_flag = true;
                et_add_process_mapped_file(et_current_pid, fullpath, mode);
            }
        }

        (void)vaddr;
        break;
    }
    // Linux Kernel: Fork a process
    case ET_KERNEL_FORK: {
        // DBG(STR_VPMU "fork from %lu\n", et_current_pid);
        break;
    }
    // Linux Kernel: wake up the newly forked process
    case ET_KERNEL_WAKE_NEW_TASK: {
        // Do nothing if the value is not initialized
        if (g_linux_offset.task_struct.pid == 0) break;
        uint32_t target_pid = vpmu_read_uint32_from_guest(
          env, get_input_arg(env, 1), g_linux_offset.task_struct.pid);
        if (et_current_pid != 0 && et_find_traced_pid(et_current_pid)) {
            et_attach_to_parent_pid(et_current_pid, target_pid);
        }
        break;
    }
    // Linux Kernel: New process creation
    case ET_KERNEL_EXECV: {
        if (VPMU.platform.linux_version < KERNEL_VERSION(3, 14, 0)) {
            // Old linux pass filename directly as a char*
            bash_path =
              (const char *)vpmu_read_ptr_from_guest(env, get_input_arg(env, 2), 0);
        } else {
            // Newer linux pass filename as a struct file *, containing char*
            uintptr_t name_addr =
              vpmu_read_uintptr_from_guest(env, get_input_arg(env, 2), 0);
            // Remember this pointer for mmap()
            bash_path = (const char *)vpmu_read_ptr_from_guest(env, name_addr, 0);
        }

        // DBG(STR_VPMU "Exec file: %s (pid=%lu)\n", bash_path, et_current_pid);
        // Let another kernel event handle. It can find the absolute path.
        exec_event_pid = et_current_pid;
        break;
    }
    // Linux Kernel: Process End
    case ET_KERNEL_EXIT: {
        // Do nothing if the value is not initialized
        if (g_linux_offset.task_struct.pid == 0) break;
        et_remove_process(et_current_pid);
        break;
    }
    // Linux Kernel: Context switch
    case ET_KERNEL_CONTEXT_SWITCH: {
        // Do nothing if the value is not initialized
        if (g_linux_offset.task_struct.pid == 0) break;
#if defined(TARGET_ARM) && (TARGET_LONG_BITS == 32)
        // Only ARM 32 bits has a different arrangement of arguments
        // Refer to "arch/arm/include/asm/switch_to.h"
        uint32_t task_ptr = vpmu_read_uint32_from_guest(
          env, get_input_arg(env, 3), g_linux_offset.thread_info.task);
        et_current_pid =
          vpmu_read_uint32_from_guest(env, task_ptr, g_linux_offset.task_struct.pid);
#else
        // Refer to "arch/x86/include/asm/switch_to.h"
        et_current_pid = vpmu_read_uint32_from_guest(
          env, get_input_arg(env, 2), g_linux_offset.task_struct.pid);
#endif
        // ERR_MSG("switch pid to %lu\n", et_current_pid);

        if (et_find_traced_pid(et_current_pid)) {
            et_set_process_cpu_state(et_current_pid, env);
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
