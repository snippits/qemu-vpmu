#ifndef __PHASE_H_
#define __PHASE_H_

#include "vpmu/vpmu-extratb.h" // Insn_Counters

void phasedet_ref(bool               user_mose,
                  const ExtraTBInfo* extra_tb_info,
                  uint64_t           stack_ptr,
                  uint64_t           core_id);

#endif
