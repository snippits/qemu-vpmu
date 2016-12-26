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
// ... reserved
#define VPMU_MMAP_SET_PROC_NAME     0x0040
#define VPMU_MMAP_SET_PROC_SIZE     0x0048
#define VPMU_MMAP_SET_PROC_BIN      0x0050
// ... reserved

void vpmu_dev_init(uint32_t base);

#endif
