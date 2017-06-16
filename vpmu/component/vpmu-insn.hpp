#ifndef __VPMU_INSN_HPP_
#define __VPMU_INSN_HPP_
extern "C" {
#include "config-target.h" // Target Configuration (CONFIG_ARM)
#include "vpmu-insn.h"
}
#include "vpmu.hpp"        // VPMU common header
#include "vpmu-stream.hpp" // VPMUStream, VPMUStream_T
#include "vpmu-packet.hpp" // VPMU_Insn, VPMU_Branch, VPMU_Cache
#include "json.hpp"        // nlohmann::json
// The implementaion of stream buffer and multi- threading/processing
#include "stream/single-thread.hpp" // VPMU_Stream_Single_Thread
#include "stream/multi-thread.hpp"  // VPMU_Stream_Multi_Thread
#include "stream/multi-process.hpp" // VPMU_Stream_Multi_Process

class InstructionStream : public VPMUStream_T<VPMU_Insn>
{
public:
    InstructionStream() : VPMUStream_T<VPMU_Insn>("INSN") { log_debug("Constructed"); }
    InstructionStream(const char* module_name) : VPMUStream_T<VPMU_Insn>(module_name) {}
    InstructionStream(std::string module_name) : VPMUStream_T<VPMU_Insn>(module_name) {}

    void set_default_stream_impl(void) override
    {
        // Get the default implementation of stream interface.
        impl = std::make_unique<VPMUStreamSingleThread<VPMU_Insn>>("I_Strm", 1024 * 64);
    }

    inline uint64_t get_insn_count(void)
    {
        VPMU_Insn::Data data = get_data(0);
        return data.insn_cnt[0];
    }

    inline uint64_t get_cycles(int n)
    {
        VPMU_Insn::Data data = get_data(n);
        return data.cycles[0];
    }

    inline uint64_t get_cycles(void)
    {
        return get_cycles(0);
    }

    // TODO This is a new funcion
    template <class F, class... Args>
    void async(F&& f, Args&&... args)
    {
        log_debug("lambda");
        f(std::forward<Args>(args)...);
    }

#if defined(TARGET_ARM)
    void send(uint8_t core, uint8_t mode, ExtraTBInfo* ptr);
// End of TARGET_ARM
#elif defined(TARGET_X86_64)
    void send(uint8_t core, uint8_t mode, ExtraTBInfo* ptr);
#endif // End of TARGET_X86_64

    VPMUArchTranslate& get_translator(int n)
    {
        if (n > jobs.size()) n = 0;
        return jobs[n]->get_translator_handle();
    }

private:
    // This is a register function declared in the vpmu-insn.cc file.
    Sim_ptr create_sim(std::string sim_name) override;
};

extern InstructionStream vpmu_insn_stream;
#endif