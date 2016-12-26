#ifndef _VPMU_H_
#define _VPMU_H_
#include <stdio.h>
#include <stdlib.h>

#include "vpmu-conf.h" // Import the common configurations and QEMU config-host.h
#include "vpmu-qemu.h" // The interface between QEMU and VPMU
#include "vpmu-log.h"  // The logging macros of VPMU
//========================  VPMU Common Definitions  ========================
//==========================  VPMU Common Macros  ===========================

#define READ_FROM_GUEST_KERNEL(_cs, _addr, _offset)                                      \
    ((uint8_t *)vpmu_get_phy_addr_global((void *)_cs, (uintptr_t)_addr) + _offset)

#define READ_BYTE_FROM_GUEST(_cs, _addr, _offset)                                        \
    *((uint8_t *)READ_FROM_GUEST_KERNEL(_cs, _addr, _offset))
#define READ_INT_FROM_GUEST(_cs, _addr, _offset)                                         \
    *((uint32_t *)READ_FROM_GUEST_KERNEL(_cs, _addr, _offset))
#define READ_LONG_FROM_GUEST(_cs, _addr, _offset)                                        \
    *((uint64_t *)READ_FROM_GUEST_KERNEL(_cs, _addr, _offset))

//==================  VPMU Common Externs(Outer Variables) ==================

uintptr_t vpmu_get_phy_addr_global(void *ptr, uintptr_t vaddr);
size_t vpmu_copy_from_guest(void *dst, uintptr_t src, const size_t size, void *cs);

#endif
