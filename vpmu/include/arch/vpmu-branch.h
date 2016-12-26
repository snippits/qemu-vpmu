#ifndef __VPMU_BRANCH_H_
#define __VPMU_BRANCH_H_

#include "../vpmu-conf.h"   // VPMU_MAX_CPU_CORES
#include "../vpmu-common.h" // Include common headers

void branch_ref(uint8_t core, uint32_t pc, uint32_t taken);

#endif
