#ifndef _VPMU_ARM_TRANSLATE_H_
#define _VPMU_ARM_TRANSLATE_H_
#include <stdint.h>        // uint8_t, uint32_t, etc.
#include "config-target.h" // QEMU Target Information

// TODO support multi-model??
// Interface Functions for Instruction Timing.
// It should be stateless and reentry-able for thread safe!!!
uint16_t vpmu_get_arm_ticks(uint32_t core, uint32_t insn);
uint16_t vpmu_get_thumb_ticks(uint32_t core, uint32_t insn);
uint16_t vpmu_get_cp14_ticks(uint32_t core, uint32_t insn);
uint16_t vpmu_get_vfp_ticks(uint32_t core, uint32_t insn, uint32_t vfp_vec_len);
#endif
