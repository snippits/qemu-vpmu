#ifndef __VPMU_INST_HPP_
#define __VPMU_INST_HPP_
extern "C" {
#include "vpmu-inst.h"
}
#include "vpmu.hpp"        // VPMU common header
#include "vpmu-stream.hpp" // VPMUStream, VPMUStream_T
#include "vpmu-packet.hpp" // VPMU_Inst, VPMU_Branch, VPMU_Cache
#include "json.hpp"        // nlohmann::json
// The implementaion of stream buffer and multi- threading/processing
#include "stream_impl/single-thread.hpp" // VPMU_Stream_Single_Thread
#include "stream_impl/multi-thread.hpp"  // VPMU_Stream_Multi_Thread
#include "stream_impl/multi-process.hpp" // VPMU_Stream_Multi_Process

class InstructionStream : public VPMUStream_T<VPMU_Inst>
{
public:
    InstructionStream() : VPMUStream_T<VPMU_Inst>("INST") { log_debug("Constructed"); }
    InstructionStream(const char* module_name) : VPMUStream_T<VPMU_Inst>(module_name) {}
    InstructionStream(std::string module_name) : VPMUStream_T<VPMU_Inst>(module_name) {}

    void build(nlohmann::json configs) override
    {
        reset();
        // std::cout << configs.dump();
        log_debug("Initializing");

        if (configs.size() < 1) {
            ERR_MSG("There is no content!");
        }

        // Get the default implementation of stream interface.
        // impl = std::make_unique<VPMUStreamMultiProcess<VPMU_Inst>>("I_Strm");
        // impl = std::make_unique<VPMUStreamMultiThread<VPMU_Inst>>("I_Strm");
        impl = std::make_unique<VPMUStreamSingleThread<VPMU_Inst>>("I_Strm");
        // Construct the channel (buffer) and allocate resources
        impl->build(1024 * 64);

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

    uint64_t get_total_inst_count(void)
    {
        VPMU_Inst::Data data = get_data(0);
        return data.inst_cnt[0];
    }

    uint64_t get_total_cycle_count(void)
    {
        VPMU_Inst::Data data = get_data(0);
        return data.cycles[0];
    }

    // TODO This is a new funcion
    template <class F, class... Args>
    void async(F&& f, Args&&... args)
    {
        log_debug("lambda");
        f(std::forward<Args>(args)...);
    }

#if defined(CONFIG_VPMU_TARGET_ARM)
    void send(uint8_t core, uint8_t mode, ExtraTBInfo* ptr);
// End of CONFIG_VPMU_TARGET_ARM
#elif defined(CONFIG_VPMU_TARGET_X86_64)

#endif // End of CONFIG_VPMU_TARGET_X86_64

private:
    // This is a register function declared in the vpmu-inst.cc file.
    Sim_ptr create_sim(std::string sim_name) override;
};

extern InstructionStream vpmu_inst_stream;
#endif
