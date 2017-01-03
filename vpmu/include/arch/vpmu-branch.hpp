#ifndef __VPMU_BRANCH_HPP_
#define __VPMU_BRANCH_HPP_
extern "C" {
#include "vpmu-branch.h"
}
#include "vpmu.hpp"        // VPMU common header
#include "vpmu-stream.hpp" // VPMUStream, VPMUStream_T
#include "vpmu-packet.hpp" // VPMU_Inst, VPMU_Branch, VPMU_Cache
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
        impl = std::make_unique<VPMUStreamMultiThread<VPMU_Branch>>("B_Strm", 1024 * 8);
    }

    void send(uint8_t core, uint64_t pc, uint32_t taken);

    inline uint64_t get_cycles(int n)
    {
        VPMU_Branch::Model model = get_model(n);
        VPMU_Branch::Data  data  = get_data(n);
        return data.wrong[0] * model.latency;
    }

    inline uint64_t get_cycles(void) { return get_cycles(0); }

private:
    // This is a register function declared in the vpmu-branch.cc file.
    Sim_ptr create_sim(std::string sim_name) override;
};

extern BranchStream vpmu_branch_stream;
#endif
