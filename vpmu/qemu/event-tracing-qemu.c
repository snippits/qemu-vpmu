#include "vpmu/include/vpmu.h"
#include "vpmu/include/vpmu-device.h"
#include "vpmu/include/event-tracing/event-tracing.h"
#include "vpmu/include/linux-mm.h"

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

uint64_t et_get_input_arg(void *env, int num)
{
    return get_input_arg(env, num);
}
uint64_t et_get_ret_addr(void *env)
{
    return get_ret_addr(env);
}
uint64_t et_get_ret_value(void *env)
{
    return get_ret_value(env);
}
uint64_t et_get_syscall_num(void *env)
{
    return get_syscall_num(env);
}
uint64_t et_get_syscall_input_arg(void *env, int num)
{
    return get_syscall_input_arg(env, num);
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

void et_parse_dentry_path(void *    env,
                          uintptr_t dentry_addr,
                          char *    buff,
                          int *     position,
                          int       size_buff,
                          int       max_levels)
{
    parse_dentry_path(env, dentry_addr, buff, position, size_buff, max_levels);
}

static inline void print_mode(uint64_t mode, uint64_t mask, const char *message)
{
    if (mode & mask) {
        CONSOLE_LOG("%s", message);
    }
}

void et_check_function_call(CPUArchState *env, uint64_t target_addr)
{
    CPUState *cs = CPU(ENV_GET_CPU(env));
    // TODO make this thread safe and need to check branch!!!!!!!
    VPMU.cpu_arch_state = env;
    et_kernel_call_event(target_addr, env, cs->cpu_index);
    return;
}
