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

#endif

void et_set_linux_sym_addr(const char* sym_name, uint64_t addr);
enum ET_KERNEL_EVENT_TYPE et_find_kernel_event(uint64_t vaddr);

void et_add_program_to_list(const char* name);
void et_remove_program_from_list(const char* name);
bool et_find_program_in_list(const char* name);
bool et_find_process_in_list(const char* name);

void et_update_program_elf_dwarf(const char* name, const char* host_file_path);
void et_check_function_call(void*    env,
                            uint64_t core_id,
                            bool     user_mode,
                            uint64_t target_addr);

#endif // __VPMU_EVENT_TRACING_
