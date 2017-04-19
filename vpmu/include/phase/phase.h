#ifndef __PHASE_H_
#define __PHASE_H_

#include "vpmu/include/vpmu-extratb.h" // Insn_Counters

#define DEFAULT_WINDOW_SIZE 200000 // 200k instructions
#define DEFAULT_VECTOR_SIZE 2048   // 2048 buckets per BBV

void phasedet_ref(bool               user_mose,
                  const ExtraTBInfo* extra_tb_info,
                  uint64_t           stack_ptr,
                  uint64_t           core_id);

#endif
