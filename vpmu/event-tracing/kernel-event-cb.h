#ifndef __VPMU_KERNEL_EVENT_CB_H_
#define __VPMU_KERNEL_EVENT_CB_H_

enum ET_KERNEL_EVENT_TYPE {
    ET_KERNEL_MMAP,
    ET_KERNEL_MPROTECT,
    ET_KERNEL_MUNMAP,
    ET_KERNEL_FORK,
    ET_KERNEL_WAKE_NEW_TASK,
    ET_KERNEL_EXECV,
    ET_KERNEL_EXIT,
    ET_KERNEL_CONTEXT_SWITCH,
    ET_KERNEL_EVENT_COUNT,
    ET_KERNEL_NONE,
    ET_KERNEL_EVENT_SIZE
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
extern LinuxStructSize   g_linux_size;

void et_set_default_linux_struct_offset(uint64_t version);
void et_set_linux_thread_struct_size(uint64_t value);
void et_set_linux_struct_offset(uint64_t type, uint64_t value);
bool et_kernel_call_event(void* env, int core_id, uint64_t vaddr);
void et_register_callbacks_kernel_events(void);

#endif
