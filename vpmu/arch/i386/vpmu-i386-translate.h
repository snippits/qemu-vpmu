#ifndef _VPMU_I386_TRANSLATE_H_
#define _VPMU_I386_TRANSLATE_H_
#include <stdint.h>        // uint8_t, uint32_t, etc.
#include "config-target.h" // QEMU Target Information

#include "vpmu/vpmu-extratb.h" // ExtraTBInfo

// TODO support multi-model??
// Interface Functions for Instruction Timing.
// It should be stateless and reentry-able for thread safe!!!
void vpmu_accumulate_x86_64_ticks(ExtraTBInfo* ex_tb, uint64_t insn);
#endif
