#include "vpmu-branch.hpp"
#include "vpmu-packet.hpp"

BranchStream vpmu_branch_stream;

// Put your own timing simulator below
#include "simulator/branch-one-bit.hpp"
#include "simulator/branch-two-bits.hpp"
// Put you own timing simulator above
BranchStream::Sim_ptr BranchStream::create_sim(std::string sim_name)
{
    // Construct your timing model with ownership transfering
    // The return will use "move semantics" automatically.
    if (sim_name == "one bit")
        return std::make_unique<Branch_One_Bit>();
    else if (sim_name == "two bits")
        return std::make_unique<Branch_Two_Bits>();
    else
        return nullptr;
}

void BranchStream::send(uint8_t core, uint64_t pc, uint32_t taken)
{
    VPMU_Branch::Reference r;
    r.type  = VPMU_PACKET_DATA; // The type of reference
    r.core  = core;             // The number of CPU core
    r.pc    = pc;               // The address of pc
    r.taken = taken;            // If this is a taken branch

    send_ref(r);
}

extern "C" {

void branch_ref(uint8_t core, uint32_t pc, uint32_t taken)
{
    vpmu_branch_stream.send(core, pc, taken);
}

}
