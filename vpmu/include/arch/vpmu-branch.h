#ifndef __VPMU_BRANCH_H_
#define __VPMU_BRANCH_H_

#include <stdint.h>       //uint8_t, uint32_t, etc.
#include "../vpmu-conf.h" // VPMU_MAX_CPU_CORES

typedef struct VPMU_Branch_Model {
    char name[1024];
} VPMU_Branch_Model;

void branch_ref(uint8_t core, uint32_t pc, uint32_t taken);
uint64_t vpmu_branch_predict_correct(void);
uint64_t vpmu_branch_predict_wrong(void);

#endif
