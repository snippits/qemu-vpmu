extern "C" {
#include "vpmu-i386-translate.h" // Interface header between QEMU and VPMU
#include "vpmu-i386-insnset.h"   // Instruction Set
}

#include "vpmu.hpp" // VPMU common headers
#include "Intel-I7.hpp"
#include "vpmu-utils.hpp"

#include "todo-misc.hpp"

// Compute the number of cycles that this instruction will take,
// not including any I-cache or D-cache misses. This function
// is called for each instruction in a basic block when that
// block is being translated.
// VPMU CORE
int CPU_IntelI7::Translation::_get_insn_ticks(uint32_t insn)
{
    int result = 1; /* by default, use 1 cycle */
    return result;
}

uint32_t CPU_IntelI7::Translation::_get_x86_64_ticks(uint64_t insn)
{
    return 1;
}

void CPU_IntelI7::build(VPMU_Insn::Model& model)
{
    log_debug("Initializing");

    log_debug(json_config.dump().c_str());

    auto model_name = vpmu::utils::get_json<std::string>(json_config, "name");
    strncpy(model.name, model_name.c_str(), sizeof(model.name));
    model.frequency  = vpmu::utils::get_json<int>(json_config, "frequency");
    model.dual_issue = vpmu::utils::get_json<bool>(json_config, "dual_issue");

    translator.build(json_config);
    log_debug("Initialized");
}

void CPU_IntelI7::packet_processor(int                         id,
                                   const VPMU_Insn::Reference& ref,
                                   VPMU_Insn::Data&            data)
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
    case VPMU_PACKET_SYNC_DATA:
        data.insn_cnt[0] = vpmu_total_insn_count(data);
        data.cycles[0]   = cycles[0];
        break;
    case VPMU_PACKET_DUMP_INFO:
        CONSOLE_LOG("  [%d] type : Intel I7\n", id);
        CONSOLE_U64(" Total instruction count       :", vpmu_total_insn_count(data));
        CONSOLE_LOG("  ->User mode insn count       : ");
        vpmu_print_u64_array(data.user.total_insn);
        CONSOLE_LOG("  ->Supervisor mode insn count : ");
        vpmu_print_u64_array(data.system.total_insn);
        CONSOLE_U64(" Total load instruction count  :", vpmu_total_load_count(data));
        CONSOLE_LOG("  ->User mode load count       : ");
        vpmu_print_u64_array(data.user.load);
        CONSOLE_LOG("  ->Supervisor mode load count : ");
        vpmu_print_u64_array(data.system.load);
        CONSOLE_U64(" Total store instruction count :", vpmu_total_store_count(data));
        CONSOLE_LOG("  ->User mode store count      : ");
        vpmu_print_u64_array(data.user.store);
        CONSOLE_LOG("  ->Supervisor mode store count: ");
        vpmu_print_u64_array(data.system.store);

        break;
    case VPMU_PACKET_RESET:
        memset(cycles, 0, sizeof(cycles));
        memset(&data, 0, sizeof(VPMU_Insn::Data));
        break;
    case VPMU_PACKET_DATA:
        accumulate(ref, data);
        break;
    default:
        LOG_FATAL("Unexpected packet");
    }

#undef CONSOLE_TME
#undef CONSOLE_U64
}

void CPU_IntelI7::accumulate(const VPMU_Insn::Reference& ref, VPMU_Insn::Data& insn_data)
{
    VPMU_Insn::Insn_Data_Cell* cell = NULL;
    // Defining the types (struct) for communication
    enum CPU_MODE { // Copy from QEMU cpu.h
        USR = 0x10,
        SVC = 0x13,
    };

    if (ref.mode == USR) {
        cell = &insn_data.user;
    } else {
        cell = &insn_data.system;
    }
    cell->total_insn[ref.core] += ref.tb_counters_ptr->counters.total;
    cell->load[ref.core] += ref.tb_counters_ptr->counters.load;
    cell->store[ref.core] += ref.tb_counters_ptr->counters.store;
    cell->branch[ref.core] += ref.tb_counters_ptr->has_branch;
    cycles[ref.core] += ref.tb_counters_ptr->ticks;
}

#ifdef CONFIG_VPMU_VFP
void CPU_IntelI7::print_vfp_count(void)
{
#define etype(x) macro_str(x)
    int                i;
    uint64_t           counted                    = 0;
    uint64_t           total_counted              = 0;
    static const char* str_x86_vfp_instructions[] = {X86_INSTRUCTION};

    for (i = 0; i < x86_VFP_INSTRUCTION_TOTAL_COUNTS; i++) {
        if (GlobalVPMU.VFP_count[i] > 0) {
            CONSOLE_LOG(
              "%s: %llu ", str_x86_vfp_instructions[i], GlobalVPMU.VFP_count[i]);
            CONSOLE_LOG("need = %d spend cycle = %llu\n",
                        x86_vfp_instr_time[i],
                        GlobalVPMU.VFP_count[i] * x86_vfp_instr_time[i]);

            if (i < (x86_VFP_INSTRUCTION_TOTAL_COUNTS - 2)) {
                counted += GlobalVPMU.VFP_count[i];
                total_counted += GlobalVPMU.VFP_count[i] * x86_vfp_instr_time[i];
            }
        }
    }
    // CONSOLE_LOG( "total latency: %llu\n", GlobalVPMU.VFP_BASE);
    // CONSOLE_LOG( "Counted instructions: %llu\n", counted);
    // CONSOLE_LOG( "total Counted cycle: %llu\n", total_counted);
    CONSOLE_LOG("VFP : total latency: %llu\n", GlobalVPMU.VFP_BASE);
    CONSOLE_LOG("VFP : Counted instructions: %llu\n", counted);
    CONSOLE_LOG("VFP : total Counted cycle: %llu\n", total_counted);
#undef etype
}
#endif
void CPU_IntelI7::Translation::build(nlohmann::json config)
{
    auto model_name = vpmu::utils::get_json<std::string>(config, "name");
    strncpy(cpu_model.name, model_name.c_str(), sizeof(cpu_model.name));
    cpu_model.frequency  = vpmu::utils::get_json<int>(config, "frequency");
    cpu_model.dual_issue = vpmu::utils::get_json<bool>(config, "dual_issue");

    nlohmann::json root = config["instruction"];
    // TODO move this to CPU model
    for (nlohmann::json::iterator it = root.begin(); it != root.end(); ++it) {
        // Skip the attribute next
        std::string key   = it.key();
        uint32_t    value = it.value();

        x86_instr_time[get_index_of_x86_insn(key.c_str())] = value;
    }
}

// TODO After removeing this from here to a separate module.
// And enable the timing feedback from VPMU to QEMU's virtual clock
// We should count the instruction count in order to make time move.
// And the final result of timing should subtract this value.
//====================  VPMU Translation Instrumentation   ===================
uint16_t CPU_IntelI7::Translation::get_x86_64_ticks(uint64_t insn)
{
    uint16_t ticks = 0;

    ticks = _get_x86_64_ticks(insn);
    // TODO FIXME
    // if (cpu_model.dual_issue) {
    //     insn_buf[insn_buf_index] = insn;
    //     insn_buf_index++;
    //     if (insn_buf_index == 2) {
    //         ticks -= _dual_issue_check();
    //         insn_buf_index = 0;
    //     }
    // }
    // DBG("%u\n", ticks);
    // ticks = get_insn_ticks(insn);
    // Remove unused function warnig
    (void)_get_insn_ticks(insn);
    return ticks;
}
