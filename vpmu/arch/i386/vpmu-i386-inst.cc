#include "config-target.h" // Target Configuration (CONFIG_ARM)
#include "vpmu-inst.hpp"   // InstructionStream
#include "vpmu-packet.hpp" // VPMU_Inst::Reference

// Put your own timing simulator below
// Put you own timing simulator above
InstructionStream::Sim_ptr InstructionStream::create_sim(std::string sim_name)
{
    // Construct your timing model with ownership transfering
    // The return will use "move semantics" automatically.
    if (sim_name == "")
        return nullptr;
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

