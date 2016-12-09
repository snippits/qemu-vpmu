#include "vpmu-inst.hpp"
#include "vpmu-packet.hpp"

InstructionStream vpmu_inst_stream;

// Put your own timing simulator below
#include "simulators/Cortex-A9.hpp"
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

extern "C" {

void vpmu_inst_ref(uint8_t core, uint8_t mode, ExtraTBInfo* ptr)
{
    vpmu_inst_stream.send(core, mode, ptr);
}

uint64_t vpmu_total_inst_count(void)
{
    return vpmu_inst_stream.get_total_inst_count();
}

uint64_t vpmu_cpu_cycle_count(void)
{
    return vpmu_inst_stream.get_total_cycle_count();
}
}
