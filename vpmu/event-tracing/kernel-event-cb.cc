extern "C" {
#include "config-target.h"
#include "vpmu-common.h"   // Include common C headers
#include "vpmu/linux-mm.h" // VM_EXEC and other mmap() mode states
}
#include "event-tracing.hpp" // EventTracer
#include "phase/phase.hpp"   // Phase class
#include "json.hpp"          // nlohmann::json
#include "vpmu-device.h"     // VPMU related definitions

// The global variable storing offsets of kernel struct types
LinuxStructOffset g_linux_offset;
LinuxStructSize   g_linux_size;

// A helper to print message of mmap
#define print_mode(_mode, _mask, _message)                                               \
    if (_mode & _mask) CONSOLE_LOG("%s", _message);

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

void et_set_linux_thread_struct_size(uint64_t value)
{
    g_linux_size.stack_thread_size = value;
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

void register_callbacks_kernel_events(void)
{
// A lambda can only be converted to a function pointer if it does not capture
#define _CB(_EVENT_NAME_, cb)                                                            \
    event_tracer.get_kernel().register_callback(_EVENT_NAME_, [](void* env) { cb });

#define _CR(_EVENT_NAME_, cb)                                                            \
    event_tracer.get_kernel().register_return_callback(_EVENT_NAME_,                     \
                                                       [](void* env) { cb });

    // TODO Make this call back variable length of args in order to have
    // compile time check of the number of input args

    // Linux Kernel: New process creation
    _CB(ET_KERNEL_EXECV, {
        uint64_t    syscall_pid = et_get_syscall_user_thread_id(env);
        const char* bash_path   = nullptr;

        if (VPMU.platform.linux_version < KERNEL_VERSION(3, 14, 0)) {
            // Old linux pass filename directly as a char*
            bash_path =
              (const char*)vpmu_read_ptr_from_guest(env, et_get_input_arg(env, 2), 0);
        } else {
            // Newer linux pass filename as a struct file *, containing char*
            uintptr_t name_addr =
              vpmu_read_uintptr_from_guest(env, et_get_input_arg(env, 2), 0);
            // Remember this pointer for mmap()
            bash_path = (const char*)vpmu_read_ptr_from_guest(env, name_addr, 0);
        }
        /*
        DBG(STR_VPMU "Exec file: %s on core %lu (pid=%lu)\n",
            bash_path,
            vpmu::get_core_id(),
            syscall_pid);
        */

        if (et_find_program_in_list(bash_path)) {
            event_tracer.add_new_process(bash_path, syscall_pid);
            // DBG(STR_VPMU "Start tracing %s (pid=%lu)\n", bash_path, syscall_pid);
            tic(&(VPMU.start_time));
            VPMU_reset();
            vpmu_print_status(&VPMU);
            vpmu::enable_vpmu_on_core();
        }
    });

    // Linux Kernel: Context switch
    _CB(ET_KERNEL_CONTEXT_SWITCH, {
        uint64_t pid = -1;

        // Do nothing if the value is not initialized
        if (g_linux_offset.task_struct.pid == 0) return;
#if defined(TARGET_ARM) && !defined(TARGET_AARCH64)
        // Only ARM 32 bits has a different arrangement of arguments
        // Refer to "arch/arm/include/asm/switch_to.h"
        uint32_t task_ptr = vpmu_read_uint32_from_guest(
          env, et_get_input_arg(env, 3), g_linux_offset.thread_info.task);
        pid = vpmu_read_uint32_from_guest(env, task_ptr, g_linux_offset.task_struct.pid);
#elif defined(TARGET_X86_64) || defined(TARGET_I386)
            // Refer to "arch/x86/include/asm/switch_to.h"
            pid = vpmu_read_uint32_from_guest(
              env, et_get_input_arg(env, 2), g_linux_offset.task_struct.pid);
#endif
        VPMU.core[vpmu::get_core_id()].current_pid = pid;
        // ERR_MSG("core %2lu switch pid to %lu\n", vpmu::get_core_id(), pid);

        if (et_find_traced_pid(pid)) {
            et_set_process_cpu_state(pid, env);
            vpmu::enable_vpmu_on_core();
        } else {
            // Switching VPMU when current process is traced
            if (vpmu_model_has(VPMU_WHOLE_SYSTEM, VPMU))
                vpmu::enable_vpmu_on_core();
            else
                vpmu::disable_vpmu_on_core();
        }
        return;
    });

    // Linux Kernel: Process End
    _CB(ET_KERNEL_EXIT, {
        // Do nothing if the value is not initialized
        if (g_linux_offset.task_struct.pid == 0) return;
        uint64_t syscall_pid = et_get_syscall_user_thread_id(env);
        et_remove_process(syscall_pid);
        return;

    });

    // Linux Kernel: wake up the newly forked process
    _CB(ET_KERNEL_WAKE_NEW_TASK, {
        // Do nothing if the value is not initialized
        if (g_linux_offset.task_struct.pid == 0) return;
        uint32_t target_pid = vpmu_read_uint32_from_guest(
          env, et_get_input_arg(env, 1), g_linux_offset.task_struct.pid);
        uint64_t syscall_pid = et_get_syscall_user_thread_id(env);
        if (syscall_pid != 0 && et_find_traced_pid(syscall_pid)) {
            et_attach_to_parent_pid(syscall_pid, target_pid);
        }
    });

    // Linux Kernel: Fork a process
    _CB(ET_KERNEL_FORK,
        {
          // uint64_t syscall_pid = et_get_syscall_user_thread_id(env);
          // DBG(STR_VPMU "fork from %lu\n", syscall_pid);
        });

    _CB(ET_KERNEL_MMAP, {
        uint64_t syscall_pid = et_get_syscall_user_thread_id(env);
        if (syscall_pid == -1) return; // Not found / some error

        // Do nothing if the value is not initialized
        if (g_linux_offset.dentry.d_iname == 0) return;
        // DBG(STR_VPMU "fork from %lu\n", syscall_pid);
        char      fullpath[1024] = {0};
        int       position       = 0;
        uintptr_t dentry_addr    = 0;
        uintptr_t mode           = 0;
        uintptr_t vaddr          = 0;
        uint64_t  mmap_len       = 0;

        if (et_get_input_arg(env, 1) == 0) return; // vaddr is zero
        dentry_addr = vpmu_read_uintptr_from_guest(
          env, et_get_input_arg(env, 1), g_linux_offset.file.fpath.dentry);
        if (dentry_addr == 0) return; // pointer to dentry is zero
        et_parse_dentry_path(env, dentry_addr, fullpath, &position, sizeof(fullpath), 64);
        if (VPMU.platform.linux_version < KERNEL_VERSION(3, 9, 0)) {
            mode = vpmu_read_uintptr_from_guest(env, et_get_input_arg(env, 5), 0);
        } else {
            mode = et_get_input_arg(env, 4);
        }
        vaddr    = et_get_input_arg(env, 2);
        mmap_len = et_get_input_arg(env, 3);

        /*
        DBG(STR_VPMU "pid %lu on core %2lu mmap file: %s @ %lx mode: (%lx) ",
            syscall_pid,
            vpmu::get_core_id(),
            fullpath,
            vaddr,
            mode);
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

        auto process = event_tracer.find_process(syscall_pid);
        if (process != nullptr) {
            et_add_process_mapped_file(syscall_pid, fullpath, mode, mmap_len);
            process->mmap_updated_flag = false;
        }

        (void)vaddr;
        return;
    });

    _CR(ET_KERNEL_MMAP, {
        uint64_t syscall_pid = et_get_syscall_user_thread_id(env);
        auto     process     = event_tracer.find_process(syscall_pid);
        if (process && process->mmap_updated_flag == false) {
            /*
            DBG(STR_VPMU "Mapped Address: 0x%lx to 0x%lx\n",
                (uint64_t)et_get_ret_value(env),
                (uint64_t)et_get_ret_value(env) + process->binary_list.back()->file_size);
            */
            uint64_t vaddr = et_get_ret_value(env);
            process->binary_list.back()->set_mapped_address(vaddr);
            process->mmap_updated_flag = true;
        }
    });

#undef _CR
#undef _CB
}

bool et_kernel_call_event(uint64_t vaddr, void* env, int core_id)
{
    return event_tracer.get_kernel().call_event(vaddr, env, core_id);
}
