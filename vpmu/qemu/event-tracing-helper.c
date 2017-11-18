#include "vpmu/vpmu.h"
#include "vpmu/qemu/vpmu-device.h"
#include "vpmu/event-tracing/event-tracing.h"
#include "vpmu/event-tracing/event-tracing-helper.h"
#include "vpmu/linux-mm.h"

/***  This is where QEMU helpers of event tracing are implemented
 * in order to retrieve architecture specific/dependent values.
 *
 * The implementation of helper functions should always check the type since
 * there's no compile-time guarantees on the types of variables.
 *
 */

#define LINUX_KERNEL_STACK_OFFSET (5 * (TARGET_LONG_BITS / 8))

// If you want to know more details about the following codes,
// please refer to `man syscall`, section "Architecture calling conventions"
// NOTE that num can not be zero!
// Follow ARM (32bits) calling convension:
// http://infocenter.arm.com/help/topic/com.arm.doc.ihi0042f/IHI0042F_aapcs.pdf
// Follow ARM-64 calling convension:
// http://infocenter.arm.com/help/topic/com.arm.doc.ihi0055c/IHI0055C_beta_aapcs64.pdf
// Follow AMD64 calling convension:
// User mode and kernel mode interface differs
// http://chamilo2.grenet.fr/inp/courses/ENSIMAG3MM1LDB/document/doc_abi_ia64.pdf

// Use of stack pointer
// Lower  address
// [return address]     <---- sp
// [arg #]     <------------- sp + TARGET_LONG_SIZE
// [arg # + 1] <------------- sp + TARGET_LONG_SIZE * 2
// ...
// Higher address

#if defined(TARGET_ARM) && TARGET_LONG_BITS == 64
#error "Many of these functions are not ready for ARM-64"
#endif
static inline target_ulong get_input_arg(CPUArchState *env, int num)
{
#if defined(TARGET_ARM)
#if TARGET_LONG_BITS == 32
    if (num > 0 && num < 5)
        return env->regs[num - 1];
    else if (num >= 5) {
        return vpmu_read_uintptr_from_guest(
          env, env->regs[13], (num - 5) * TARGET_LONG_SIZE);
    }
#else
    if (num > 0 && num < 9)
        return env->xregs[num - 1];
    else if (num >= 9) {
        return vpmu_read_uintptr_from_guest(
          env, env->xregs[], (num - 9) * TARGET_LONG_SIZE);
    }
#endif
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
        return vpmu_read_uintptr_from_guest(
          env, env->regs[R_ESP], (num - 7 + 1) * TARGET_LONG_SIZE);
    }
#endif
    return 0;
}

static inline target_ulong get_syscall_arg(CPUArchState *env, int num)
{
#if defined(TARGET_ARM)
    return get_input_arg(env, num);
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
    else if (num >= 7) {
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
    uint32_t d_flags;

    uintptr_t parent_dentry_addr = 0;
    char *    name               = NULL;

    // Safety checks
    if (env == NULL || buff == NULL || position == NULL || size_buff == 0) return;
    // Stop condition 1 (reach user defined limition or null pointer)
    if (max_levels == 0 || dentry_addr == 0) return;
    name =
      (char *)vpmu_read_ptr_from_guest(env, dentry_addr, g_linux_offset.dentry.d_iname);
    // Faulty condition, stop and return
    if (name[0] == '\0') return;

    // Find parent node (dentry->d_parent)
    parent_dentry_addr =
      vpmu_read_uintptr_from_guest(env, dentry_addr, g_linux_offset.dentry.d_parent);
    // TODO mount point will stop here and we need to further trace it.
    // Maybe try dentry->d_flags & DCACHE_MOUNTED ?? The following is its experiment code.
    {
        #define DCACHE_MOUNTED 0x00010000 /* is a mountpoint */
        d_flags = vpmu_read_uint32_from_guest(env, dentry_addr, 0);
        // All the dirs are mounted. Only the fake root is not mounted.
        if (dentry_addr == parent_dentry_addr && !(d_flags & DCACHE_MOUNTED)) {
            //CONSOLE_LOG(STR_KERNEL "Warning: Tracing mount point is not implemented yet. "
            //                       "Use ':' as its root.\n");
            __append_str(buff, position, size_buff, ":");
        }
    }
    // Is ROOT (include/linux/dcache.h). Return!
    if (dentry_addr == parent_dentry_addr) return;
    parse_dentry_path(env, parent_dentry_addr, buff, position, size_buff, max_levels - 1);
    // Append path/name
    name =
      (char *)vpmu_read_ptr_from_guest(env, dentry_addr, g_linux_offset.dentry.d_iname);
    __append_str(buff, position, size_buff, "/");
    __append_str(buff, position, size_buff, name);

    return;
}

// Return an address of pointer type "struct task_struct *"
static inline target_ulong get_syscall_user_thread(CPUArchState *env)
{
    if (g_linux_size.stack_thread_size == 0) return 0;
#if defined(TARGET_ARM)
    // arch/arm/include/asm/thread_info.h:current_thread_info()
    // return (struct thread_info *) (current_stack_pointer & ~(THREAD_SIZE - 1));
    target_ulong current_thread_ptr =
      (env->regs[13] & ~(g_linux_size.stack_thread_size - 1));
    target_ulong current_task_ptr = vpmu_read_uintptr_from_guest(
      env, current_thread_ptr, g_linux_offset.thread_info.task);
    return current_task_ptr;
#elif defined(TARGET_X86_64) || defined(TARGET_I386)
    // NOTE: g_linux_offset.thread_info.task is 0 on x86_64.
    // Thus 0 check (initialization check) is not needed.

    target_ulong current_task_ptr = 0;
    if (VPMU.platform.linux_version < KERNEL_VERSION(3, 15, 0)) {
        target_ulong current_thread_ptr =
          (env->regs[R_ESP] & ~(g_linux_size.stack_thread_size - 1));
        current_task_ptr = vpmu_read_uintptr_from_guest(
          env, current_thread_ptr, g_linux_offset.thread_info.task);
    } else if (VPMU.platform.linux_version < KERNEL_VERSION(4, 1, 0)) {
        // Although it uses this_cpu_read_stable(kernel_stack), x86_tss_sp0 seems to be
        // the same thing. https://patchwork.kernel.org/patch/6365671/
        target_ulong x86_tss_sp0 = vpmu_read_uintptr_from_guest(env, env->tr.base, 4);
        target_ulong current_thread_ptr =
          (x86_tss_sp0 + LINUX_KERNEL_STACK_OFFSET - g_linux_size.stack_thread_size);
        current_task_ptr = vpmu_read_uintptr_from_guest(
          env, current_thread_ptr, g_linux_offset.thread_info.task);
    } else {
        // Offset is 4 bytes (uint32_t)
        // arch/x86/include/asm/processor.h:current_top_of_stack()
        target_ulong x86_tss_sp0 = vpmu_read_uintptr_from_guest(env, env->tr.base, 4);
        // arch/x86/include/asm/thread_info.h:current_thread_info()
        x86_tss_sp0 -= g_linux_size.stack_thread_size; // THREAD_SIZE
        // include/asm-generic/current.h
        current_task_ptr =
          vpmu_read_uintptr_from_guest(env, x86_tss_sp0, g_linux_offset.thread_info.task);
    }
    return current_task_ptr;
#endif
}

static uint64_t get_switch_to_pid(CPUArchState *env)
{
    if (g_linux_offset.task_struct.pid == 0) return -1;
    uint64_t pid        = -1;
    uint64_t pid_offset = g_linux_offset.task_struct.pid;

#if defined(TARGET_ARM) && !defined(TARGET_AARCH64)
    // Only ARM 32 bits has a different arrangement of arguments
    // Refer to "arch/arm/include/asm/switch_to.h"
    uint64_t task_offset = g_linux_offset.thread_info.task;
    uint64_t ptr         = et_get_input_arg(env, 3);
    uint32_t task_ptr    = vpmu_read_uint32_from_guest(env, ptr, task_offset);
    pid                  = vpmu_read_uint32_from_guest(env, task_ptr, pid_offset);
#elif defined(TARGET_X86_64) || defined(TARGET_I386)
    // Refer to "arch/x86/include/asm/switch_to.h"
    uint64_t ptr = et_get_input_arg(env, 2);
    pid          = vpmu_read_uint32_from_guest(env, ptr, pid_offset);
#endif
    return pid;
}

static uint64_t get_switch_to_prev_pid(CPUArchState *env)
{
    if (g_linux_offset.task_struct.pid == 0) return -1;
    uint64_t prev_pid   = -1;
    uint64_t pid_offset = g_linux_offset.task_struct.pid;

#if defined(TARGET_ARM) && !defined(TARGET_AARCH64)
    // Only ARM 32 bits has a different arrangement of arguments
    // Refer to "arch/arm/include/asm/switch_to.h"
    uint64_t task_offset = g_linux_offset.thread_info.task;
    uint64_t prev_ptr    = et_get_input_arg(env, 2);
    uint32_t task_ptr    = vpmu_read_uint32_from_guest(env, prev_ptr, task_offset);
    prev_pid             = vpmu_read_uint32_from_guest(env, task_ptr, pid_offset);
#elif defined(TARGET_X86_64) || defined(TARGET_I386)
    // Refer to "arch/x86/include/asm/switch_to.h"
    uint64_t prev_ptr = et_get_input_arg(env, 1);
    prev_pid          = vpmu_read_uint32_from_guest(env, prev_ptr, pid_offset);
#endif
    return prev_pid;
}

uint64_t et_get_switch_to_pid(void *env)
{
    return get_switch_to_pid(env);
}

uint64_t et_get_switch_to_prev_pid(void *env)
{
    return get_switch_to_prev_pid(env);
}

uint64_t et_get_syscall_user_thread_id(void *env)
{
    if (g_linux_offset.task_struct.pid == 0) return -1;
    uint64_t thread_ptr = get_syscall_user_thread(env);
    if (thread_ptr == 0) return -1;
    uint64_t pid =
      vpmu_read_uint32_from_guest(env, thread_ptr, g_linux_offset.task_struct.pid);
    return pid;
}

uint64_t et_get_syscall_user_thread(void *env)
{
    return get_syscall_user_thread(env);
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

uint64_t et_get_syscall_arg(void *env, int num)
{
    return get_syscall_arg(env, num);
}

void et_parse_dentry_path(void *env, uint64_t dentry_addr, char *buff, int buff_size)
{
    int position = 0; // The buffer position index for recursive function
    parse_dentry_path(env, dentry_addr, buff, &position, buff_size, 64);
}
