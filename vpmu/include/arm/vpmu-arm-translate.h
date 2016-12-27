#ifndef _VPMU_ARM_TRANSLATE_H_
#define _VPMU_ARM_TRANSLATE_H_
#include <stdint.h>        // uint8_t, uint32_t, etc.
#include "config-target.h" // QEMU Target Information

#include "vpmu/include/vpmu-extratb.h" // ExtraTBInfo

// TODO support multi-model??
// Interface Functions for Instruction Timing.
// It should be stateless and reentry-able for thread safe!!!
void vpmu_accumulate_arm_ticks(ExtraTBInfo* ex_tb, uint32_t insn);
void vpmu_accumulate_thumb_ticks(ExtraTBInfo* ex_tb, uint32_t insn);
void vpmu_accumulate_cp14_ticks(ExtraTBInfo* ex_tb, uint32_t insn);
void vpmu_accumulate_vfp_ticks(ExtraTBInfo* ex_tb, uint32_t insn, uint32_t vfp_vec_len);
#endif
