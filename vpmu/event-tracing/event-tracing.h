#ifndef __VPMU_EVENT_TRACING_
#define __VPMU_EVENT_TRACING_

#include "config-target.h"        // Target configuration
#include "../vpmu-common.h"       // Common headers and macros
#include "event-tracing-helper.h" // Helper functions like et_get_input_arg()
#include "kernel-event-cb.h"      // Helper functions for Linux kernel events

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
// End of implementation in C side

// Implemented in C++ side
// event-tracing.cc
void et_set_linux_sym_addr(const char* sym_name, uint64_t addr);
enum ET_KERNEL_EVENT_TYPE et_find_kernel_event(uint64_t vaddr);

void et_add_program_to_list(const char* name);
void et_remove_program_from_list(const char* name);
bool et_find_program_in_list(const char* name);

bool et_find_traced_pid(uint64_t pid);
bool et_find_traced_process(const char* name);
void et_attach_to_parent_pid(uint64_t parent_pid, uint64_t child_pid);
void et_add_new_process(const char* path, const char* name, uint64_t pid);
void et_remove_process(uint64_t pid);
void et_set_process_cpu_state(uint64_t pid, void* cs);
void et_add_process_mapped_file(uint64_t    pid,
                                const char* fullpath,
                                uint64_t    mode,
                                uint64_t    file_size);
void et_attach_shared_library_to_process(uint64_t    pid,
                                         const char* fullpath,
                                         uint64_t    file_size);
void et_update_program_elf_dwarf(const char* name, const char* file_name);

void et_update_last_mmaped_binary(uint64_t pid, uint64_t vaddr, uint64_t len);

// End of implementation in C++ side
#endif // __VPMU_EVENT_TRACING_
