extern "C" {
#include "config-target.h"
#include "vpmu-common.h"   // Include common C headers
#include "vpmu/linux-mm.h" // VM_EXEC and other mmap() mode states
}
#include "event-tracing.hpp" // EventTracer
#include "phase/phase.hpp"   // Phase class
#include "json.hpp"          // nlohmann::json
#include "vpmu-device.h"     // VPMU related definitions
#include "vpmu-utils.hpp"    // vpmu::host::timestamp_us()

// The global variable storing offsets of kernel struct types
LinuxStructOffset g_linux_offset;
LinuxStructSize   g_linux_size;

/// A helper to print message of mmap
static inline void print_mode(uintptr_t mode, uintptr_t mask, const char* message)
{
#ifdef CONFIG_VPMU_DEBUG_MSG
    if (mode & mask) CONSOLE_LOG("%s", message);
#endif
}

static std::string genlog_unmap(uint64_t pid, uint64_t saddr, uint64_t eaddr)
{
    char buffer[4096] = {};

    DBG(STR_KERNEL "pid %lu on core %2lu unmap: %lx - %lx\n",
        pid,
        vpmu::get_core_id(),
        saddr,
        eaddr);

    snprintf(buffer,
             sizeof(buffer),
             "pid %lu on core %2lu unmap: %lx - %lx\n",
             pid,
             vpmu::get_core_id(),
             saddr,
             eaddr);

    return buffer;
}

static std::string genlog_mmap(uint64_t pid, MMapInfo mmap_info)
{
    char     buffer[4096] = {};
    uint64_t free_space   = 0;

    DBG(STR_KERNEL "pid %lu on core %2lu mmap file: %s @ %lx - %lx mode: (%lx) ",
        pid,
        vpmu::get_core_id(),
        mmap_info.fullpath,
        mmap_info.vaddr,
        mmap_info.vaddr + mmap_info.len,
        mmap_info.mode);
    print_mode(mmap_info.mode, VM_READ, " READ");
    print_mode(mmap_info.mode, VM_WRITE, " WRITE");
    print_mode(mmap_info.mode, VM_EXEC, " EXEC");
    print_mode(mmap_info.mode, VM_SHARED, " SHARED");
    print_mode(mmap_info.mode, VM_IO, " IO");
    print_mode(mmap_info.mode, VM_HUGETLB, " HUGETLB");
    print_mode(mmap_info.mode, VM_DONTCOPY, " DONTCOPY");
    DBG("\n");

    snprintf(buffer,
             sizeof(buffer),
             "pid %lu on core %2lu mmap file: %s @ %lx mode: (%lx) ",
             pid,
             vpmu::get_core_id(),
             mmap_info.fullpath,
             mmap_info.vaddr,
             mmap_info.mode);
    free_space = sizeof(buffer) - strlen(buffer) - 1;
    if (mmap_info.mode & VM_READ) strncat(buffer, " READ", free_space);
    free_space = sizeof(buffer) - strlen(buffer) - 1;
    if (mmap_info.mode & VM_WRITE) strncat(buffer, " WRITE", free_space);
    free_space = sizeof(buffer) - strlen(buffer) - 1;
    if (mmap_info.mode & VM_EXEC) strncat(buffer, " EXEC", free_space);
    free_space = sizeof(buffer) - strlen(buffer) - 1;
    if (mmap_info.mode & VM_SHARED) strncat(buffer, " SHARED", free_space);
    free_space = sizeof(buffer) - strlen(buffer) - 1;
    if (mmap_info.mode & VM_IO) strncat(buffer, " IO", free_space);
    free_space = sizeof(buffer) - strlen(buffer) - 1;
    if (mmap_info.mode & VM_HUGETLB) strncat(buffer, " HUGETLB", free_space);
    free_space = sizeof(buffer) - strlen(buffer) - 1;
    if (mmap_info.mode & VM_DONTCOPY) strncat(buffer, " DONTCOPY", free_space);
    free_space = sizeof(buffer) - strlen(buffer) - 1;
    strncat(buffer, " \n", free_space);

    return buffer;
}

static std::string genlog_mprotect(uint64_t pid, MMapInfo mmap_info)
{
    char     buffer[4096] = {};
    uint64_t free_space   = 0;

    DBG(STR_KERNEL "pid %lu on core %2lu mprotect: %lx - %lx mode: (%lx) ",
        pid,
        vpmu::get_core_id(),
        mmap_info.vaddr,
        mmap_info.vaddr + mmap_info.len,
        mmap_info.mode);
    print_mode(mmap_info.mode, VM_READ, " READ");
    print_mode(mmap_info.mode, VM_WRITE, " WRITE");
    print_mode(mmap_info.mode, VM_EXEC, " EXEC");
    print_mode(mmap_info.mode, VM_SHARED, " SHARED");
    DBG("\n");

    snprintf(buffer,
             sizeof(buffer),
             "pid %lu on core %2lu mprotect: %lx - %lx mode: (%lx) ",
             pid,
             vpmu::get_core_id(),
             mmap_info.vaddr,
             mmap_info.vaddr + mmap_info.len,
             mmap_info.mode);
    free_space = sizeof(buffer) - strlen(buffer) - 1;
    if (mmap_info.mode & VM_READ) strncat(buffer, " READ", free_space);
    free_space = sizeof(buffer) - strlen(buffer) - 1;
    if (mmap_info.mode & VM_WRITE) strncat(buffer, " WRITE", free_space);
    free_space = sizeof(buffer) - strlen(buffer) - 1;
    if (mmap_info.mode & VM_EXEC) strncat(buffer, " EXEC", free_space);
    free_space = sizeof(buffer) - strlen(buffer) - 1;
    strncat(buffer, " \n", free_space);

    return buffer;
}

static std::string genlog_mmap_ret(uint64_t pid, MMapInfo mmap_info)
{
    char buffer[1024] = {};

    DBG(STR_KERNEL "pid %lu on core %2lu mmap: %lx - %lx\n",
        pid,
        vpmu::get_core_id(),
        mmap_info.vaddr,
        mmap_info.vaddr + mmap_info.len);

    snprintf(buffer,
             sizeof(buffer),
             "pid %lu on core %2lu mmap: %lx - %lx\n",
             pid,
             vpmu::get_core_id(),
             mmap_info.vaddr,
             mmap_info.vaddr + mmap_info.len);
    return buffer;
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
        et_set_linux_thread_struct_size(8192);
        return;
    }
#elif defined(TARGET_X86_64)
    if (version == KERNEL_VERSION(4, 4, 0)) {
        // This is kernel v4.4.0
        et_set_linux_struct_offset(VPMU_MMAP_OFFSET_FILE_f_path_dentry, 24);
        et_set_linux_struct_offset(VPMU_MMAP_OFFSET_DENTRY_d_iname, 56);
        et_set_linux_struct_offset(VPMU_MMAP_OFFSET_DENTRY_d_parent, 24);
        et_set_linux_struct_offset(VPMU_MMAP_OFFSET_THREAD_INFO_task, 0);
        et_set_linux_struct_offset(VPMU_MMAP_OFFSET_TASK_STRUCT_pid, 1040);
        et_set_linux_thread_struct_size(16384);
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

bool et_kernel_call_event(void* env, int core_id, uint64_t vaddr)
{
    return event_tracer.get_kernel().call_event(env, core_id, vaddr);
}

void et_register_callbacks_kernel_events(void)
{
    auto& kernel = event_tracer.get_kernel();

    // Linux Kernel: New process creation
    kernel.events.register_call(ET_KERNEL_EXECV, [](void* env) {
        const char* bash_path = nullptr;
        uint64_t    irq_pid   = et_get_syscall_user_thread_id(env);

        if (VPMU.platform.linux_version < KERNEL_VERSION(3, 14, 0)) {
            // Old linux pass filename directly as a char*
            bash_path =
              (const char*)vpmu_read_ptr_from_guest(env, et_get_input_arg(env, 1), 0);
        } else if (VPMU.platform.linux_version < KERNEL_VERSION(3, 19, 0)) {
            // Later linux pass filename as a struct file *, containing char*
            // but the position of argument is still at the first one.
            uintptr_t name_addr =
              vpmu_read_uintptr_from_guest(env, et_get_input_arg(env, 1), 0);
            bash_path = (const char*)vpmu_read_ptr_from_guest(env, name_addr, 0);
        } else {
            // Newer linux pass filename as a struct file *, containing char*
            uintptr_t name_addr =
              vpmu_read_uintptr_from_guest(env, et_get_input_arg(env, 2), 0);
            bash_path = (const char*)vpmu_read_ptr_from_guest(env, name_addr, 0);
        }
        /*
        DBG(STR_KERNEL "Exec file: %s on core %lu (pid=%lu)\n",
            bash_path,
            vpmu::get_core_id(),
            irq_pid);
        */

        auto process = event_tracer.find_process(irq_pid);
        // If process exists and it's forked by others (not top process)
        if (process && !process->is_top_process) {
            // Enter when a script executes a sub-process
            auto timing_model = process->timing_model;
            // Create a new process and overwrite all its original contents.
            // This immitates the actual behavior in Linux kernel.
            if (auto program = event_tracer.find_program(bash_path)) {
                // Try to use existing program info. if there is one.
                ET_Process empty_process(program, irq_pid);
                *process = empty_process;
            } else {
                ET_Process empty_process(bash_path, irq_pid);
                *process = empty_process;
            }
            // Rename the process by the bash name (same as htop)
            process->name = bash_path;
            // Reset the timing model to its original value
            process->timing_model = timing_model;
            // DBG(STR_KERNEL "Exec a new process %s (pid=%lu)\n", bash_path, irq_pid);
            vpmu::enable_vpmu_on_core();
        } else if (event_tracer.find_program(bash_path)) {
            // Enter this condition when this process is not executed by
            // any monitored process, e.g. program run by a user.
            process = event_tracer.add_new_process(bash_path, irq_pid);
            // DBG(STR_KERNEL "Start tracing %s (pid=%lu)\n", bash_path, irq_pid);
            vpmu::enable_vpmu_on_core();
        }

        // Do snapshot when a new monitored process is executed
        if (process) {
            uint64_t core_id = vpmu::get_core_id();
            VPMU_async([process, core_id]() {
                VPMUSnapshot new_snapshot(true, core_id);
                process->snapshot         = new_snapshot;
                process->snapshot_phase   = new_snapshot;
                process->guest_launchtime = vpmu::target::time_us();
            });
            process->is_running = true;
        }
    });

    // Linux Kernel: Context switch
    kernel.events.register_call(ET_KERNEL_CONTEXT_SWITCH, [](void* env) {
        uint64_t pid      = et_get_switch_to_pid(env);
        uint64_t prev_pid = et_get_switch_to_prev_pid(env);

        /*
        DBG(STR_KERNEL "Core %2lu switch pid %lu to %lu\n",
            vpmu::get_core_id(),
            prev_pid,
            pid);
        */
        VPMU.core[vpmu::get_core_id()].current_pid = pid;

        // Accumulate the profiling counters when a process is scheduled out
        auto prev_process = event_tracer.find_process(prev_pid);
        if (prev_pid != pid && prev_process && prev_process->is_running) {
            uint64_t core_id   = vpmu::get_core_id();
            uint64_t timestamp = vpmu::host::timestamp_us();
            VPMU_async([prev_process, core_id, timestamp]() {
                VPMUSnapshot new_snapshot(true, core_id);
                prev_process->prof_counters += new_snapshot - prev_process->snapshot;
                prev_process->snapshot_phase = new_snapshot - prev_process->snapshot_phase;
                uint64_t target_timestamp = vpmu::target::time_us();
                prev_process->phase_history.push_back({{timestamp, target_timestamp, 0}});
            });
            prev_process->is_running = false;
        }

        auto process = event_tracer.find_process(pid);
        // Do snapshot when a process is scheduled in
        if (process && !process->is_running) {
            // NOTE: Sometimes kernel will schedule in a process twice for unknown reason.
            // We don't need to do snapshot again in this case.
            uint64_t core_id = vpmu::get_core_id();
            VPMU_async([process, core_id]() {
                VPMUSnapshot new_snapshot(true, core_id);
                process->snapshot = new_snapshot;
                // Apply previous records before context switching
                process->snapshot_phase += new_snapshot;
            });
            process->is_running = true;
        }

        // Turn on/off VPMU depending on the situation
        if (process) {
            process->set_cpu_state(env);
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
    kernel.events.register_call(ET_KERNEL_EXIT, [](void* env) {
        uint64_t irq_pid = et_get_syscall_user_thread_id(env);

        auto process = event_tracer.find_process(irq_pid);
        if (process) {
            uint64_t core_id = vpmu::get_core_id();
            VPMU_async([process, core_id]() {
                VPMUSnapshot new_snapshot(true, core_id);
                process->prof_counters += new_snapshot - process->snapshot;
                process->snapshot = new_snapshot;
            });
            process->is_running = false;
        }
        event_tracer.remove_process(irq_pid);
        return;

    });

    // Linux Kernel: wake up the newly forked process
    kernel.events.register_call(ET_KERNEL_WAKE_NEW_TASK, [](void* env) {
        uint64_t irq_pid    = et_get_syscall_user_thread_id(env);
        uint64_t addr       = et_get_input_arg(env, 1);
        uint64_t offset     = g_linux_offset.task_struct.pid;
        uint32_t target_pid = vpmu_read_uint32_from_guest(env, addr, offset);
        // Safe checks are done in this call
        event_tracer.attach_to_parent(event_tracer.find_process(irq_pid), target_pid);
    });

    // Linux Kernel: Fork a process
    kernel.events.register_call(ET_KERNEL_FORK, [](void* env) {
        // uint64_t irq_pid = et_get_syscall_user_thread_id(env);
        // DBG(STR_KERNEL "fork from %lu\n", irq_pid);
    });

    kernel.events.register_call(ET_KERNEL_MMAP, [](void* env) {
        // Do nothing if the value is not initialized
        if (g_linux_offset.dentry.d_iname == 0) return;
        MMapInfo mmap_info = {};
        uint64_t irq_pid   = et_get_syscall_user_thread_id(env);

        // Is struct file * nullptr (anonymous mapping)
        if (et_get_input_arg(env, 1) != 0) {
            uint64_t addr        = et_get_input_arg(env, 1);
            uint64_t offset      = g_linux_offset.file.fpath.dentry;
            uint64_t dentry_addr = vpmu_read_uintptr_from_guest(env, addr, offset);
            if (dentry_addr == 0) return; // pointer to dentry is zero
            et_parse_dentry_path(
              env, dentry_addr, mmap_info.fullpath, sizeof(mmap_info.fullpath));
        }
        if (VPMU.platform.linux_version < KERNEL_VERSION(3, 9, 0)) {
            mmap_info.mode = et_get_input_arg(env, 5);
        } else {
            mmap_info.mode = et_get_input_arg(env, 4);
        }
        mmap_info.vaddr = et_get_input_arg(env, 2);
        mmap_info.len   = et_get_input_arg(env, 3);

        auto process = event_tracer.find_process(irq_pid);
        if (process != nullptr) {
            // Remember the latest mapped address for later updating this address value
            process->set_last_mapped_info(mmap_info);
            // process->append_debug_log(genlog_mmap(irq_pid, mmap_info));
        }
    });

    kernel.events.register_return(ET_KERNEL_MMAP, [](void* env) {
        // Do nothing if the value is not initialized
        if (g_linux_offset.dentry.d_iname == 0) return;
        uint64_t irq_pid = et_get_syscall_user_thread_id(env);

        auto process = event_tracer.find_process(irq_pid);
        if (process && process->get_last_mapped_info().vaddr != 0) {
            uint64_t vaddr     = et_get_ret_value(env);
            auto&    mmap_info = process->get_last_mapped_info();
            // Update new base address
            mmap_info.vaddr = vaddr;
            event_tracer.attach_mapped_region(process, mmap_info);
            // process->append_debug_log(genlog_mmap_ret(irq_pid, mmap_info));
            // process->vm_maps.debug_print_vm_map();

            // Clear the flag
            process->clear_last_mapped_info();
        }
    });

    kernel.events.register_call(ET_KERNEL_MPROTECT, [](void* env) {
        // Do nothing if the value is not initialized
        if (g_linux_offset.dentry.d_iname == 0) return;
        uint64_t irq_pid    = et_get_syscall_user_thread_id(env);
        uint64_t start_addr = et_get_input_arg(env, 3);
        uint64_t end_addr   = et_get_input_arg(env, 4);
        uint64_t mode       = et_get_input_arg(env, 5);

        auto process = event_tracer.find_process(irq_pid);
        if (process) {
            process->vm_maps.update(start_addr, end_addr, mode);
            process->max_vm_maps.update(start_addr, end_addr, mode);
            // auto buff = genlog_mprotect(irq_pid, {start_addr, end_addr, mode, ""});
            // process->append_debug_log(buff);
            // process->vm_maps.debug_print_vm_map();
        }
    });

    kernel.events.register_call(ET_KERNEL_MUNMAP, [](void* env) {
        // Do nothing if the value is not initialized
        if (g_linux_offset.dentry.d_iname == 0) return;
        uint64_t irq_pid    = et_get_syscall_user_thread_id(env);
        uint64_t start_addr = et_get_input_arg(env, 4);
        uint64_t end_addr   = et_get_input_arg(env, 5);

        auto process = event_tracer.find_process(irq_pid);
        if (process) {
            (void)start_addr;
            (void)end_addr;
            // Unmap the region
            process->vm_maps.unmap(start_addr, end_addr);
            // process->append_debug_log(genlog_unmap(irq_pid, start_addr, end_addr));
            // process->vm_maps.debug_print_vm_map();
        }
    });

    // This is to prevent compiler warning of unused warning
    (void)print_mode;
    (void)genlog_mmap;
    (void)genlog_unmap;
    (void)genlog_mmap_ret;
    (void)genlog_mprotect;
}
