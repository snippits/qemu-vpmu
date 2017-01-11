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

// NOTE: Due to some QEMU's function is limited in C compiler only,
// we implemented some interface functions in C code and management in C++ code
//
#ifndef __cplusplus
// These headers would be failed to compile in C++
#include "qemu/osdep.h"    // DeviceState, VMState, etc.
#include "cpu.h"           // QEMU CPU definitions and macros (CPUArchState)
#include "exec/exec-all.h" // tlb_fill()

// Implemented in C side
void et_check_function_call(CPUArchState* env,
                            uint64_t      target_addr,
                            uint64_t      return_addr);
#endif

enum ET_KERNEL_EVENT_TYPE et_find_kernel_event(uint64_t vaddr);

// Implemented in CPP side
void et_add_program_to_list(const char* name);
void et_remove_program_from_list(const char* name);
bool et_find_program_in_list(const char* name);

void et_add_new_process(const char* name, uint64_t pid);
void et_remove_process(uint64_t pid);
void et_attach_to_parent_pid(uint64_t parent_pid, uint64_t child_pid);
bool et_find_traced_pid(uint64_t pid);
bool et_find_traced_process(const char* name);
void et_set_process_cpu_state(uint64_t pid, void* cs);
void et_add_process_mapped_file(uint64_t pid, const char* fullpath, uint64_t mode);

#endif // __VPMU_EVENT_TRACING_
