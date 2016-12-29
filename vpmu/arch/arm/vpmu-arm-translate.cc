extern "C" {
#include "vpmu-arm-translate.h"
}
#include "vpmu.hpp"      // VPMU common header
#include "vpmu-inst.hpp" // Inst_Stream, vpmu_inst_stream

// NOTE: These interface function should NOT include CPU core index!!!
// QEMU might translate TBs on core 1 and execute the same TB on core 1 and 2.
// If you care about pipelining on different cores, inject some information
// in extra TB info for runtime pipeline checking with core index.
// The extra TB info could be modified for your needs!!!
//
// If you really want CPU information and some other functions,
// include the following headers in another C file. (NOT CPP)
// And implement your own interface and functions for doing magics.
// #include "qemu/osdep.h"
// #include "cpu.h"
// #include "exec/exec-all.h"
// #include "translate.h"

void vpmu_accumulate_arm_ticks(ExtraTBInfo* ex_tb, uint32_t insn)
{
    ex_tb->ticks += vpmu_inst_stream.get_translator(0).get_arm_ticks(insn);
}

void vpmu_accumulate_thumb_ticks(ExtraTBInfo* ex_tb, uint32_t insn)
{
    ex_tb->ticks += vpmu_inst_stream.get_translator(0).get_thumb_ticks(insn);
}

void vpmu_accumulate_cp14_ticks(ExtraTBInfo* ex_tb, uint32_t insn)
{
    ex_tb->ticks += vpmu_inst_stream.get_translator(0).get_cp14_ticks(insn);
}

#if defined(CONFIG_VPMU) && defined(CONFIG_VPMU_VFP)
void vpmu_accumulate_vfp_ticks(ExtraTBInfo* ex_tb, uint32_t insn, uint64_t vfp_vec_len)
{
    ex_tb->ticks += vpmu_inst_stream.get_translator(0).get_vfp_ticks(insn);
}
#endif
