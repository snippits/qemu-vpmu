#ifndef __VPMU_INST_H_
#define __VPMU_INST_H_

#include <stdint.h>       //uint8_t, uint32_t, etc.
#include <string.h>       // strncpy
#include "../vpmu-conf.h" // VPMU_MAX_CPU_CORES

typedef struct Inst_Counters {
    uint16_t total;
    uint8_t  load;
    uint8_t  store;
    uint8_t  alu;
    uint8_t  bit; // shift, and, or, xor
    uint8_t  fpu;
    uint8_t  vfp;
    uint8_t  neon;
    uint8_t  co_processor;
    uint16_t size_bytes;
} Inst_Counters;

typedef struct Inst_Data_Cell {
    uint64_t total_inst;
    uint64_t load;
    uint64_t store;
    uint64_t branch;
} Inst_Data_Cell;

typedef struct VPMU_Inst_Model {
    char     name[1024];
    uint32_t frequency;
    uint8_t  dual_issue;
} VPMU_Inst_Model;

uint64_t vpmu_total_inst_count(void);
uint64_t vpmu_cpu_cycle_count(void);

#endif
