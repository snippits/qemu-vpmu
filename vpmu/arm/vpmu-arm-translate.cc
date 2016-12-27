extern "C" {
#include "vpmu-arm-translate.h"
//#define NEED_CPU_H
//#include "qemu/osdep.h"
//#include "cpu.h"
}
#include "vpmu.hpp"      // VPMU common header
#include "vpmu-inst.hpp" // Inst_Stream, vpmu_inst_stream

// TODO add cpu cores
uint16_t vpmu_get_arm_ticks(uint32_t insn)
{
    return vpmu_inst_stream.get_translator(0)->get_arm_ticks(insn);
}

uint16_t vpmu_get_thumb_ticks(uint32_t insn)
{
    return vpmu_inst_stream.get_translator(0)->get_thumb_ticks(insn);
}

uint16_t vpmu_get_cp14_ticks(uint32_t insn)
{
    return vpmu_inst_stream.get_translator(0)->get_cp14_ticks(insn);
}

#if defined(CONFIG_VPMU) && defined(CONFIG_VPMU_VFP)
uint16_t vpmu_get_vfp_ticks(uint32_t insn, uint64_t vfp_vec_len)
{
    return vpmu_inst_stream.get_translator(0)->get_vfp_ticks(insn);
}
#endif
