#ifndef __VPMU_EXTRA_TB_H_
#define __VPMU_EXTRA_TB_H_

#include "config-target.h" // Target configuration

typedef struct Insn_Counters {
    uint16_t total;
    uint8_t  load;
    uint8_t  store;
    uint8_t  alu;
    uint8_t  bit; // shift, and, or, xor
#if defined(TARGET_ARM)
    uint8_t fpu;
    uint8_t vfp;
    uint8_t neon;
#elif defined(TARGET_X86_64)
#endif
    uint8_t  co_processor;
    uint16_t size_bytes;
} Insn_Counters;

// A structure to extend TB info for accumulating counters when executing each TB.
typedef struct ExtraTBInfo {
    Insn_Counters counters;
    uint8_t       has_branch;
    uint8_t       cpu_mode;
    uint16_t      ticks;
    uint64_t      start_addr;

    // Modelsel
    struct {
        uint8_t  hot_tb_flag;
        uint64_t last_visit;
    } modelsel;
} ExtraTBInfo;

#endif
