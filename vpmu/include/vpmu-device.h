#ifndef __VPMU_DEVICE_H_
#define __VPMU_DEVICE_H_

#include "vpmu-common.h" // Common headers and macros

#define VPMU_DEVICE_MAJOR_NUM       0x7A //122
#define VPMU_DEVICE_MINOR_NUM       0
#define VPMU_DEVICE_NAME            "VPMU"
#define VPMU_DEVICE_BASE_ADDR       0xf1000000
#define VPMU_DEVICE_IOMEM_SIZE      0x2000

#define VPMU_MMAP_ENABLE            0x0000
#define VPMU_MMAP_DISABLE           0x0008
#define VPMU_MMAP_REPORT            0x0010
#define VPMU_MMAP_RESET             0x0018
#define VPMU_MMAP_SET_TIMING_MODEL  0x0020
// ... reserved
#define VPMU_MMAP_ADD_PROC_NAME     0x0040
#define VPMU_MMAP_REMOVE_PROC_NAME  0x0048
#define VPMU_MMAP_SET_PROC_SIZE     0x0050
#define VPMU_MMAP_SET_PROC_BIN      0x0058
// ... reserved
#define VPMU_MMAP_OFFSET_FILE_f_path_dentry      0x0100
#define VPMU_MMAP_OFFSET_DENTRY_d_iname          0x0108
#define VPMU_MMAP_OFFSET_DENTRY_d_parent         0x0110
#define VPMU_MMAP_OFFSET_THREAD_INFO_task        0x0118
#define VPMU_MMAP_OFFSET_TASK_STRUCT_pid         0x0120
// ... reserved
#define VPMU_MMAP_OFFSET_KERNEL_SYM_NAME         0x0200
#define VPMU_MMAP_OFFSET_KERNEL_SYM_ADDR         0x0208

// Mode selector
#define VPMU_INSN_COUNT_SIM         0x1 << 0
#define VPMU_DCACHE_SIM             0x1 << 1
#define VPMU_ICACHE_SIM             0x1 << 2
#define VPMU_BRANCH_SIM             0x1 << 3
#define VPMU_PIPELINE_SIM           0x1 << 4
#define VPMU_JIT_MODEL_SELECT       0x1 << 5
#define VPMU_EVENT_TRACE            0x1 << 6
#define VPMU_WHOLE_SYSTEM           0x1 << 7
#define VPMU_PHASEDET               0x1 << 8
#define VPMU_VMS_SIM                0x1 << 9

#define vpmu_model_has(model, vpmu) (vpmu.timing_model & (model))

void vpmu_dev_init(uint32_t base);

#endif
