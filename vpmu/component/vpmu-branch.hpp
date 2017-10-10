#ifndef __VPMU_BRANCH_HPP_
#define __VPMU_BRANCH_HPP_
#pragma once

extern "C" {
#include "vpmu-branch.h"
}
#include "vpmu.hpp"        // VPMU common header
#include "vpmu-stream.hpp" // VPMUStream, VPMUStream_T
#include "vpmu-packet.hpp" // VPMU_Insn, VPMU_Branch, VPMU_Cache
#include "json.hpp"        // nlohmann::json
// The implementaion of stream buffer and multi- threading/processing
#include "stream/single-thread.hpp" // VPMUStreamSingleThread
#include "stream/multi-thread.hpp"  // VPMUStreamMultiThread
#include "stream/multi-process.hpp" // VPMUStreamMultiProcess

class BranchStream : public VPMUStream_T<VPMU_Branch>
{
public:
    BranchStream() : VPMUStream_T<VPMU_Branch>("BRANCH") { log_debug("Constructed"); }
    BranchStream(const char* module_name) : VPMUStream_T<VPMU_Branch>(module_name) {}
    BranchStream(std::string module_name) : VPMUStream_T<VPMU_Branch>(module_name) {}

    void set_default_stream_impl(void) override
    {
        // Get the default implementation of stream interface.
        impl = std::make_unique<VPMUStreamMultiThread<VPMU_Branch>>("B_Strm");
    }

    void send(uint8_t core, uint64_t pc, uint32_t taken);

    inline uint64_t get_cycles(int model_idx, int core_id)
    {
        VPMU_Branch::Model model = get_model(model_idx);
        VPMU_Branch::Data  data  = get_data(model_idx);
        if (core_id == -1)
            return vpmu::math::sum_cores(data.wrong) * model.latency;
        else
            return data.wrong[core_id] * model.latency;
    }

    // TODO
    // Summarize branch misses of all cores means nothing. We define it as prohibit.
    inline uint64_t get_cycles(void) { return get_cycles(0, -1); }
    // inline uint64_t get_cycles(void) = delete;

private:
    // This is a register function declared in the vpmu-branch.cc file.
    Sim_ptr create_sim(std::string sim_name) override;
};

extern BranchStream vpmu_branch_stream;
#endif
