#ifndef __CPU_CORTEX_A9_HPP__
#define __CPU_CORTEX_A9_HPP__
extern "C" {
#include "vpmu-qemu.h"        // ExtraTBInfo
#include "vpmu-arm-instset.h" // Instruction Set
}
#include "vpmu-sim.hpp"       // VPMUSimulator
#include "vpmu-translate.hpp" // VPMUARMTranslate
#include "vpmu-packet.hpp"    // VPMU_Inst

#define VPMU_INST_SUM(_D, _N)                                                            \
    _D.user._N + _D.system._N + _D.interrupt._N + _D.system_call._N + _D.rest._N         \
      + _D.fpu._N + _D.co_processor._N

class CPU_CortexA9 : public VPMUSimulator<VPMU_Inst>
{
private: // VPMUARMTranslate
    class Translation : public VPMUARMTranslate
    {
    public:
        typedef struct Model {
            char     name[1024];
            uint32_t frequency;
            uint32_t dual_issue;
        } Model;

        void build(nlohmann::json config);
        uint16_t get_arm64_ticks(uint64_t insn) override
        {
            log_debug("ARM 64 not is supported yet");
            return 0;
        }
        uint16_t get_arm_ticks(uint32_t insn) override;
        uint16_t get_thumb_ticks(uint32_t insn) override;
        uint16_t get_cp14_ticks(uint32_t insn) override;
#ifdef CONFIG_VPMU_VFP
        uint16_t get_vfp_ticks(uint32_t insn, uint64_t vfp_vec_len);
#endif
    private:
        Model    cpu_model;
        uint32_t arm_instr_time[ARM_INSTRUCTION_TOTAL_COUNTS];
        uint64_t insn_buf[2]    = {0}; // For future 64 bits ARM insns
        uint8_t  insn_buf_index = 0;
        int      interlocks[16];
        int      interlock_base;
#ifdef CONFIG_VPMU_VFP
        uint32_t arm_vfp_instr_time[] = {ARM_VFP_INSTRUCTION_TOTAL_COUNTS};
        uint32_t arm_vfp_latency[]    = {ARM_VFP_INSTRUCTION_TOTAL_COUNTS};
        uint8_t  vfp_locks[32]        = {0};
        uint16_t vfp_base             = 0;
#endif

        uint32_t _get_arm_ticks(uint32_t insn);
        uint16_t _dual_issue_check();
        void _interlock_def(int reg, int delay);
        int _interlock_use(int reg);
        int _get_insn_ticks(uint32_t insn);
        int _get_insn_ticks_thumb(uint32_t insn);
#ifdef CONFIG_VPMU_VFP
        void _vfp_lock_release(int insn);
        void _vfp_lock_analyze(int rd, int rn, int rm, int dp, int insn);
        int _analyze_vfp_ticks(uint32_t insn, uint64_t vfp_vec_len);
#endif
    }; // End of class Translation

public: // VPMUSimulator
    CPU_CortexA9() : VPMUSimulator("CortexA9") { log_debug("Constructed"); }
    ~CPU_CortexA9() { log_debug("Destructed"); }

    VPMUARMTranslate& get_translator_handle(void) override { return translator; }

    void destroy() override { ; } // Nothing to do
    void build(VPMU_Inst& inst) override;
    void packet_processor(int id, VPMU_Inst::Reference& ref, VPMU_Inst& inst) override;

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

    void accumulate(VPMU_Inst::Reference& ref, VPMU_Inst::Data& inst_data);

    uint64_t vpmu_total_inst_count(VPMU_Inst::Data& inst_data)
    {
        return VPMU_INST_SUM(inst_data, total_inst);
    }

    uint64_t vpmu_total_ldst_count(VPMU_Inst::Data& inst_data)
    {
        return VPMU_INST_SUM(inst_data, load) + VPMU_INST_SUM(inst_data, store);
    }

    uint64_t vpmu_total_load_count(VPMU_Inst::Data& inst_data)
    {
        return VPMU_INST_SUM(inst_data, load);
    }

    uint64_t vpmu_total_store_count(VPMU_Inst::Data& inst_data)
    {
        return VPMU_INST_SUM(inst_data, store);
    }

    uint64_t vpmu_branch_insn_count(VPMU_Inst::Data& inst_data)
    {
        return VPMU_INST_SUM(inst_data, branch);
    }

#ifdef CONFIG_VPMU_VFP
    void print_vfp_count(void);
#endif
    // End of VPMUSimulator
};

#undef VPMU_INST_SUM
#endif
