#ifndef __CPU_CORTEX_A9_HPP__
#define __CPU_CORTEX_A9_HPP__
#include "vpmu-sim.hpp"
#include "vpmu-inst.hpp"
#include "vpmu-packet.hpp"
#include "vpmu-qemu.h" //ExtraTBInfo

#define VPMU_INST_SUM(_D, _N)                                                            \
    _D.user._N + _D.system._N + _D.interrupt._N + _D.system_call._N + _D.rest._N         \
      + _D.fpu._N + _D.co_processor._N

class CPU_CortexA9 : public VPMUSimulator<VPMU_Inst>
{
public:
    CPU_CortexA9() : VPMUSimulator("CortexA9") { log_debug("Constructed"); }
    ~CPU_CortexA9() { log_debug("Destructed"); }

    void build(VPMU_Inst& inst) override
    {
        log_debug("Initializing");

        log_debug(json_config.dump().c_str());

        auto model_name = vpmu::utils::get_json<std::string>(json_config, "name");
        strncpy(inst.model.name, model_name.c_str(), sizeof(inst.model.name));
        inst.model.frequency  = vpmu::utils::get_json<int>(json_config, "frequency");
        inst.model.dual_issue = vpmu::utils::get_json<bool>(json_config, "dual_issue");

        log_debug("Initialized");
    }

    void packet_processor(int id, VPMU_Inst::Reference& ref, VPMU_Inst& inst) override
    {
#define CONSOLE_U64(str, val) CONSOLE_LOG(str " %'" PRIu64 "\n", (uint64_t)val)
#define CONSOLE_TME(str, val) CONSOLE_LOG(str " %'lf sec\n", (double)val / 1000000000.0)
#ifdef CONFIG_VPMU_DEBUG_MSG
        debug_packet_num_cnt++;
        if (ref.type == VPMU_PACKET_DUMP_INFO) {
            CONSOLE_LOG("    %'" PRIu64 " packets received\n", debug_packet_num_cnt);
            debug_packet_num_cnt = 0;
        }
#endif

        // Every simulators should handle VPMU_BARRIER_PACKET to support synchronization
        // The implementation depends on your own packet type and writing style
        switch (ref.type) {
        case VPMU_PACKET_BARRIER:
            inst.data.inst_cnt[0] = vpmu_total_inst_count(inst.data);
            inst.data.cycles[0]   = cycles[0];
            break;
        case VPMU_PACKET_DUMP_INFO:
            CONSOLE_LOG("  [%d] type : Cortex A9\n", id);
            CONSOLE_U64(" Total instruction count       :",
                        vpmu_total_inst_count(inst.data));
            CONSOLE_U64("  ->User mode insn count       :", inst.data.user.total_inst);
            CONSOLE_U64("  ->Supervisor mode insn count :", inst.data.system.total_inst);
            CONSOLE_U64("  ->IRQ mode insn count        :",
                        inst.data.interrupt.total_inst);
            CONSOLE_U64("  ->Other mode insn count      :", inst.data.rest.total_inst);
            CONSOLE_U64(" Total load instruction count  :",
                        vpmu_total_load_count(inst.data));
            CONSOLE_U64("  ->User mode load count       :", inst.data.user.load);
            CONSOLE_U64("  ->Supervisor mode load count :", inst.data.system.load);
            CONSOLE_U64("  ->IRQ mode load count        :", inst.data.interrupt.load);
            CONSOLE_U64("  ->Other mode load count      :", inst.data.rest.load);
            CONSOLE_U64(" Total store instruction count :",
                        vpmu_total_store_count(inst.data));
            CONSOLE_U64("  ->User mode store count      :", inst.data.user.store);
            CONSOLE_U64("  ->Supervisor mode store count:", inst.data.system.store);
            CONSOLE_U64("  ->IRQ mode store count       :", inst.data.interrupt.store);
            CONSOLE_U64("  ->Other mode store count     :", inst.data.rest.store);

            break;
        case VPMU_PACKET_RESET:
            memset(cycles, 0, sizeof(cycles));
            memset(&inst.data, 0, sizeof(VPMU_Inst::Data));
            break;
        case VPMU_PACKET_DATA:
            accumulate(ref, inst.data);
            break;
        default:
            log_fatal("Unexpected packet");
        }

#undef CONSOLE_TME
#undef CONSOLE_U64
    }

private:
    uint64_t cycles[VPMU_MAX_CPU_CORES]    = {0};
    // The total number of packets counter for debugging
    uint64_t debug_packet_num_cnt = 0;
    // The CPU configurations for timing model
    using VPMUSimulator::platform_info;

    void accumulate(VPMU_Inst::Reference& ref, VPMU_Inst::Data& inst_data)
    {
        Inst_Data_Cell* cell = NULL;
        // Defining the types (struct) for communication
        enum CPU_MODE { // Copy from QEMU cpu.h
            USR = 0x10,
            FIQ = 0x11,
            IRQ = 0x12,
            SVC = 0x13,
            MON = 0x16,
            ABT = 0x17,
            HYP = 0x1a,
            UND = 0x1b,
            SYS = 0x1f
        };

        if (ref.mode == USR) {
            cell = &inst_data.user;
        } else if (ref.mode == SVC) {
            // if (ref.swi_fired_flag) { // TODO This feature is still lack of
            // setting this flag to true
            //    cell = &inst_data.system_call;
            //} else {
            //    cell = &inst_data.system;
            //}
            cell = &inst_data.system;
        } else if (ref.mode == IRQ) {
            cell = &inst_data.interrupt;
        } else {
            cell = &inst_data.rest;
        }
        cell->total_inst += ref.tb_counters_ptr->counters.total;
        cell->load += ref.tb_counters_ptr->counters.load;
        cell->store += ref.tb_counters_ptr->counters.store;
        cell->branch += ref.tb_counters_ptr->has_branch;
        cycles[ref.core] += ref.tb_counters_ptr->ticks;
    }

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
};

#undef VPMU_INST_SUM
#endif
