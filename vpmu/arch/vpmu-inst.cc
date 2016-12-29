#include "config-target.h" // Target Configuration (CONFIG_ARM)
#include "vpmu-inst.hpp"   // InstructionStream
#include "vpmu-packet.hpp" // VPMU_Inst::Reference

// Define the global instance here for accessing
InstructionStream vpmu_inst_stream;

#if defined(TARGET_ARM)
// Put your own timing simulator below
#include "simulator/Cortex-A9.hpp"
// Put you own timing simulator above
InstructionStream::Sim_ptr InstructionStream::create_sim(std::string sim_name)
{
    // Construct your timing model with ownership transfering
    // The return will use "move semantics" automatically.
    if (sim_name == "Cortex-A9")
        return std::make_unique<CPU_CortexA9>();
    else
        return nullptr;
}

void InstructionStream::send(uint8_t core, uint8_t mode, ExtraTBInfo* ptr)
{
    VPMU_Inst::Reference r;
    r.type            = VPMU_PACKET_DATA; // The type of reference
    r.core            = core;             // The number of CPU core
    r.mode            = mode;             // CPU mode
    r.tb_counters_ptr = ptr;              // TB Info Pointer

    send_ref(r);
}

// End of TARGET_ARM
#elif defined(TARGET_X86_64)

#endif // End of TARGET_X86_64

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
