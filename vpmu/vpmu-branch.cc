#include "vpmu-branch.hpp"
#include "vpmu-packet.hpp"

BranchStream vpmu_branch_stream;

// Put your own timing simulator below
#include "../simulators/branch-one-bit.hpp"
#include "../simulators/branch-two-bits.hpp"
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

extern "C" {

void branch_ref(uint8_t core, uint32_t pc, uint32_t taken)
{
    vpmu_branch_stream.send(core, pc, taken);
}

uint64_t vpmu_branch_predict_correct(void)
{
    return vpmu_branch_stream.get_data().correct[0];
}

uint64_t vpmu_branch_predict_wrong(void)
{
    return vpmu_branch_stream.get_data().wrong[0];
}
}
