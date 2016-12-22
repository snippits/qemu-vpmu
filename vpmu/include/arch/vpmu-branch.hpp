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
#include "stream_impl/single-thread.hpp" // VPMUStreamSingleThread
#include "stream_impl/multi-thread.hpp"  // VPMUStreamMultiThread
#include "stream_impl/multi-process.hpp" // VPMUStreamMultiProcess

class BranchStream : public VPMUStream_T<VPMU_Branch>
{
public:
    BranchStream() : VPMUStream_T<VPMU_Branch>("BRANCH") { log_debug("Constructed"); }
    BranchStream(const char* module_name) : VPMUStream_T<VPMU_Branch>(module_name) {}
    BranchStream(std::string module_name) : VPMUStream_T<VPMU_Branch>(module_name) {}

    void build(nlohmann::json configs) override
    {
        reset();
        // std::cout << configs.dump();
        log_debug("Initializing");

        if (configs.size() < 1) {
            ERR_MSG("There is no content!");
        }

        // Get the default implementation of stream interface.
        // impl = std::make_unique<VPMUStreamMultiProcess<VPMU_Branch>>("B_Strm");
        impl = std::make_unique<VPMUStreamMultiThread<VPMU_Branch>>("B_Strm");
        // impl = std::make_unique<VPMUStreamSingleThread<VPMU_Branch>>("B_Strm");
        // Construct the channel (buffer) and allocate resources
        impl->build(1024 * 8);

        // Locate and create instances of simulator according to the name.
        if (configs.is_array()) {
            for (auto sim_config : configs) {
                attach_simulator(sim_config);
            }
        } else {
            attach_simulator(configs);
        }

        log_debug("attaching %d simulators", jobs.size());
        // Start worker threads/processes with its ring buffer implementation
        // Attention: the ownership might be taken away from implementations.
        impl->run(jobs);

        log_debug("Initialized");
    }

    void send(uint8_t core, uint64_t pc, uint32_t taken);

private:
    // This is a register function declared in the vpmu-branch.cc file.
    Sim_ptr create_sim(std::string sim_name) override;
};

extern BranchStream vpmu_branch_stream;
#endif
