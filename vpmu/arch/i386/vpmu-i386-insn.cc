#include "config-target.h" // Target Configuration (CONFIG_ARM)
#include "vpmu-insn.hpp"   // InstructionStream
#include "vpmu-packet.hpp" // VPMU_Insn::Reference

// Put your own timing simulator below
#include "simulator/Intel-I7.hpp"

// Put you own timing simulator above
InstructionStream::Sim_ptr InstructionStream::create_sim(std::string sim_name)
{
    // Construct your timing model with ownership transfering
    // The return will use "move semantics" automatically.
    if (sim_name == "Intel-I7")
        return std::make_unique<CPU_IntelI7>();
    else
        return nullptr;
}

void InstructionStream::send(uint8_t core, uint8_t mode, ExtraTBInfo* ptr)
{
    VPMU_Insn::Reference r;
    r.type            = VPMU_PACKET_DATA; // The type of reference
    r.core            = core;             // The number of CPU core
    r.mode            = mode;             // CPU mode
    r.tb_counters_ptr = ptr;              // TB Info Pointer

    send_ref(core, r);
}

