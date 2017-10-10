#include "config-target.h" // Target Configuration (CONFIG_ARM)
#include "vpmu-insn.hpp"   // InstructionStream

// Define the global instance here for accessing
InstructionStream vpmu_insn_stream;

void vpmu_insn_ref(uint8_t core, uint8_t mode, ExtraTBInfo* ptr)
{
    vpmu_insn_stream.send(core, mode, ptr);
}

uint64_t vpmu_total_insn_count(void)
{
    return vpmu_insn_stream.get_insn_count();
}

