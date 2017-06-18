#ifndef __CPU_CORTEX_A9_HPP__
#define __CPU_CORTEX_A9_HPP__
extern "C" {
#include "vpmu-qemu.h"        // ExtraTBInfo
#include "vpmu-arm-insnset.h" // Instruction Set
}
#include "vpmu-sim.hpp"       // VPMUSimulator
#include "vpmu-translate.hpp" // VPMUARMTranslate
#include "vpmu-packet.hpp"    // VPMU_Insn

/// @brief Cortex A9 component simulator class
/// @details This class demonstrates the use of VPMUSimulator class for CPU simulation.
/// The implementations of a component simulator should override build(),
/// packet_processor(), and get_translator_handle() classes as the basic requirement.
/// This example declare Translation class under the scope of CPU_CortexA9, but this is
/// not a requirement.
///
class CPU_CortexA9 : public VPMUSimulator<VPMU_Insn>
{
private: // VPMUARMTranslate
    /// @brief Translation class of VPMUARMTranslate
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
            log_debug("ARM 64 is not supported yet");
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
    CPU_CortexA9() : VPMUSimulator("CortexA9") {}
    ~CPU_CortexA9() {}

    /// Must override this. This is called in translation time.
    VPMUARMTranslate& get_translator_handle(void) override { return translator; }

    /// @brief This is where to release/free/deallocate resources holded by simulator.
    void destroy() override { ; }
    /// @brief Initiate and allocate resource required by this timing simulator.
    void build(VPMU_Insn::Model& model) override;
    /// @brief The main function of each timing simulator for processing traces.
    void packet_processor(int                         id,
                          const VPMU_Insn::Reference& ref,
                          VPMU_Insn::Data&            data) override;

private:
#ifdef CONFIG_VPMU_DEBUG_MSG
    /// The total number of packets counter for debugging
    uint64_t debug_packet_num_cnt = 0;
#endif
    /// Rename platform_info. The CPU configurations for timing model
    using VPMUSimulator::platform_info;
    /// The instance of Translator called from QEMU when doing binary translation
    Translation translator = {};
    // The data stored in this simulator
    VPMU_Insn::Data insn_data = {};
    // The model stored in this simulator
    VPMU_Insn::Model insn_model = {};

    /// @brief The main function for processing data packets and accumulate cycles
    /// per-core.
    /// @details This function is a private function since it is used only in this calss.
    /// This function is called when packet_processor() receives VPMU_PACKET_DATA,
    /// and accumulates the counters and cycles for doing the CPU timing simulation.
    ///
    /// If one wants to simulate pipeline, one needs to implement per-core pipeline state
    /// and use the states when accumulating the cycles.
    /// In this simple model, we do not simulate that part of impact.
    void accumulate(const VPMU_Insn::Reference& ref);

#ifdef CONFIG_VPMU_VFP
    void print_vfp_count(void);
#endif
    // End of VPMUSimulator
};

#endif
