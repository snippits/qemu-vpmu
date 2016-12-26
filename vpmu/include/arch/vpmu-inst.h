#ifndef __VPMU_INST_H_
#define __VPMU_INST_H_

#include "../vpmu-conf.h"    // VPMU_MAX_CPU_CORES
#include "../vpmu-common.h"  // Include common headers
#include "../vpmu-extratb.h" // Extra TB Information

uint64_t vpmu_total_inst_count(void);
void vpmu_inst_ref(uint8_t core, uint8_t mode, ExtraTBInfo *ptr);

#endif
