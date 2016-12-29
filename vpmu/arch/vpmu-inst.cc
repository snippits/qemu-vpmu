#include "config-target.h" // Target Configuration (CONFIG_ARM)
#include "vpmu-inst.hpp"   // InstructionStream
#include "vpmu-packet.hpp" // VPMU_Inst::Reference

// Define the global instance here for accessing
InstructionStream vpmu_inst_stream;

extern "C" {

void vpmu_inst_ref(uint8_t core, uint8_t mode, ExtraTBInfo* ptr)
{
    vpmu_inst_stream.send(core, mode, ptr);
}

uint64_t vpmu_total_inst_count(void)
{
    return vpmu_inst_stream.get_inst_count();
}
}
