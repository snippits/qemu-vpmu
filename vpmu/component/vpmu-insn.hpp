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

    inline uint64_t get_insn_count(int model_idx, int core_id)
    {
        VPMU_Insn::Data data = get_data(model_idx);
        if (core_id == -1)
            return data.sum_all().total_insn;
        else
            return data.sum_all_mode().total_insn[core_id];
    }

    inline uint64_t get_cycles(int model_idx, int core_id)
    {
        VPMU_Insn::Data data = get_data(model_idx);
        if (core_id == -1)
            return data.sum_all().cycles;
        else
            return data.sum_all_mode().cycles[core_id];
    }

    // TODO
    // Summarize instruction count of all cores means nothing. We define it as prohibit.
    inline uint64_t get_insn_count(void) { return get_insn_count(0, -1); }
    inline uint64_t get_cycles(void) { return get_cycles(0, -1); }
    // inline uint64_t get_insn_count(void) = delete;
    // inline uint64_t get_cycles(void) = delete;

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
