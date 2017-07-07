#ifndef __VPMU_EVENT_TRACING_HELPER_H_
#define __VPMU_EVENT_TRACING_HELPER_H_

// The following type of "env" should be "CPUArchState*" when called
uint64_t et_get_syscall_user_thread_id(void* env);
uint64_t et_get_syscall_user_thread(void* env);
uint64_t et_get_input_arg(void* env, int num);
uint64_t et_get_ret_addr(void* env);
uint64_t et_get_ret_value(void* env);
uint64_t et_get_syscall_num(void* env);
uint64_t et_get_syscall_arg(void* env, int num);
void et_parse_dentry_path(void*     env,
                          uintptr_t dentry_addr,
                          char*     buff,
                          int*      position,
                          int       size_buff,
                          int       max_levels);
#endif
