#ifndef __VPMU_COMMON_H_
#define __VPMU_COMMON_H_

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h> // uint8_t, uint32_t, etc.
#include <time.h>

// Mode selector
#define VPMU_INSN_COUNT_SIM         0x1 << 0
#define VPMU_DCACHE_SIM             0x1 << 1
#define VPMU_ICACHE_SIM             0x1 << 2
#define VPMU_BRANCH_SIM             0x1 << 3
#define VPMU_PIPELINE_SIM           0x1 << 4
#define VPMU_JIT_MODEL_SELECT       0x1 << 5
#define VPMU_EVENT_TRACE            0x1 << 6

#define vpmu_model_has(model, vpmu) (vpmu.timing_model & (model))

#endif
