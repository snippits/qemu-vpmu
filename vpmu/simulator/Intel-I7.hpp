#ifndef __CPU_INTEL_I7_HPP__
#define __CPU_INTEL_I7_HPP__
extern "C" {
#include "vpmu-qemu.h"        // ExtraTBInfo
#include "vpmu-i386-insnset.h" // Instruction Set
}
#include "vpmu-sim.hpp"       // VPMUSimulator
#include "vpmu-translate.hpp" // VPMUi386Translate
#include "vpmu-packet.hpp"    // VPMU_Insn

#define VPMU_INSN_SUM(_D, _N) _D.user._N + _D.system._N
class CPU_IntelI7 : public VPMUSimulator<VPMU_Insn>
{
private: // VPMUi386Translate
    class Translation : public VPMUi386Translate
    {
    public:
        typedef struct Model {
            char     name[1024];
            uint32_t frequency;
            uint32_t dual_issue;
        } Model;

        void build(nlohmann::json config);
        uint16_t get_x86_64_ticks(uint64_t insn) override;
        uint16_t get_i386_ticks(uint32_t insn) override
        {
            log_debug("X86 32-bit is not supported yet");
            return 0;
        }
    private:
        Model    cpu_model;
        uint32_t x86_instr_time[X86_INSTRUCTION_TOTAL_COUNTS];
        uint32_t _get_x86_64_ticks(uint64_t insn);
        int _get_insn_ticks(uint32_t insn);
    }; // End of class Translation

public: // VPMUSimulator
    CPU_IntelI7() : VPMUSimulator("IntelI7") {}
    ~CPU_IntelI7() {}

    VPMUi386Translate& get_translator_handle(void) override { return translator; }

    void destroy() override { ; } // Nothing to do
    void build(VPMU_Insn::Model& model) override;
    void packet_processor(int                         id,
                          const VPMU_Insn::Reference& ref,
                          VPMU_Insn::Data&            data) override;

private:
#ifdef CONFIG_VPMU_DEBUG_MSG
    // The total number of packets counter for debugging
    uint64_t debug_packet_num_cnt = 0;
#endif
    uint64_t cycles[VPMU_MAX_CPU_CORES] = {0};
    // The CPU configurations for timing model
    using VPMUSimulator::platform_info;
    // The instance of Translator called from QEMU when doing binary translation
    Translation translator;

    void accumulate(const VPMU_Insn::Reference& ref, VPMU_Insn::Data& insn_data);

    uint64_t vpmu_total_insn_count(VPMU_Insn::Data& insn_data)
    {
        return VPMU_INSN_SUM(insn_data, total_insn);
    }

    uint64_t vpmu_total_ldst_count(VPMU_Insn::Data& insn_data)
    {
        return VPMU_INSN_SUM(insn_data, load) + VPMU_INSN_SUM(insn_data, store);
    }

    uint64_t vpmu_total_load_count(VPMU_Insn::Data& insn_data)
    {
        return VPMU_INSN_SUM(insn_data, load);
    }

    uint64_t vpmu_total_store_count(VPMU_Insn::Data& insn_data)
    {
        return VPMU_INSN_SUM(insn_data, store);
    }

    uint64_t vpmu_branch_insn_count(VPMU_Insn::Data& insn_data)
    {
        return VPMU_INSN_SUM(insn_data, branch);
    }
    // End of VPMUSimulator
};




#undef VPMU_INSN_SUM
#endif
