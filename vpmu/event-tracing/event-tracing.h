#ifndef __VPMU_EVENT_TRACING_
#define __VPMU_EVENT_TRACING_

#include "config-target.h"  // Target configuration
#include "../vpmu-common.h" // Common headers and macros

enum ET_KERNEL_EVENT_TYPE {
    ET_KERNEL_MMAP,
    ET_KERNEL_FORK,
    ET_KERNEL_WAKE_NEW_TASK,
    ET_KERNEL_EXECV,
    ET_KERNEL_EXIT,
    ET_KERNEL_CONTEXT_SWITCH,
    ET_KERNEL_NONE,
    ET_KERNEL_EVENT_COUNT
};

typedef struct LinuxStructOffset {
    struct file {
        struct fpath {
            uint64_t dentry;
        } fpath;
    } file;
    struct dentry {
        uint64_t d_iname;
        uint64_t d_parent;
    } dentry;
    struct thread_info {
        uint64_t task;
    } thread_info;
    struct task_struct {
        uint64_t pid;
    } task_struct;
} LinuxStructOffset;

typedef struct LinuxStructSize {
    uint64_t stack_thread_size;
} LinuxStructSize;

extern LinuxStructOffset g_linux_offset;
extern LinuxStructSize g_linux_size;

// NOTE: Due to some QEMU's function is limited in C compiler only,
// we implemented some interface functions in C code and management in C++ code
//
#ifndef __cplusplus
// These headers would be failed to compile in C++
#include "qemu/osdep.h"    // DeviceState, VMState, etc.
#include "cpu.h"           // QEMU CPU definitions and macros (CPUArchState)
#include "exec/exec-all.h" // tlb_fill()

// Implemented in C side
void et_check_function_call(CPUArchState* env, uint64_t target_addr);
#endif

// The following type of "env" should be "CPUArchState*" when called
uint64_t et_get_syscall_user_thread_id(void *env);
uint64_t et_get_syscall_user_thread(void *env);
uint64_t et_get_input_arg(void* env, int num);
uint64_t et_get_ret_addr(void* env);
uint64_t et_get_ret_value(void* env);
uint64_t et_get_syscall_num(void* env);
uint64_t et_get_syscall_input_arg(void* env, int num);
void et_parse_dentry_path(void*     env,
                          uintptr_t dentry_addr,
                          char*     buff,
                          int*      position,
                          int       size_buff,
                          int       max_levels);
// End of implementation in C side

enum ET_KERNEL_EVENT_TYPE et_find_kernel_event(uint64_t vaddr);

// Implemented in C++ side
// event-tracing.cc
void et_set_linux_sym_addr(const char* sym_name, uint64_t addr);

void et_add_program_to_list(const char* name);
void et_remove_program_from_list(const char* name);
bool et_find_program_in_list(const char* name);
void et_update_program_elf_dwarf(const char* name, const char* file_name);

void et_add_new_process(const char* path, const char* name, uint64_t pid);
void et_remove_process(uint64_t pid);
void et_attach_to_parent_pid(uint64_t parent_pid, uint64_t child_pid);
bool et_find_traced_pid(uint64_t pid);
bool et_find_traced_process(const char* name);
void et_set_process_cpu_state(uint64_t pid, void* cs);
void et_add_process_mapped_file(uint64_t    pid,
                                const char* fullpath,
                                uint64_t    mode,
                                uint64_t    file_size);
void et_attach_shared_library_to_process(uint64_t    pid,
                                         const char* fullpath,
                                         uint64_t    file_size);

void et_update_last_mmaped_binary(uint64_t pid, uint64_t vaddr, uint64_t len);

// kernel-event-cb.cc
void et_set_linux_thread_struct_size(uint64_t value);
void et_set_linux_struct_offset(uint64_t type, uint64_t value);
void et_set_default_linux_struct_offset(uint64_t version);
bool et_kernel_call_event(uint64_t vaddr, void* env, int core_id);
void register_callbacks_kernel_events(void);
// End of implementation in C++ side
#endif // __VPMU_EVENT_TRACING_
