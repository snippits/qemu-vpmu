#include "vpmu/include/vpmu-qemu.h"          // ExtraTB, XXX_ref() etc.
#include "vpmu/include/packet/vpmu-packet.h" // data types for sending traces
#include "vpmu/include/vpmu-device.h"        // vpmu_model_has and its macros
#include "vpmu/include/arch/vpmu-insn.h"     // vpmu_insn_ref
#include "vpmu/include/arch/vpmu-cache.h"    // vpmu_cache_ref
#include "vpmu/include/arch/vpmu-branch.h"   // vpmu_branch_ref

enum VPMU_X86_CPU_MODE{
       VCM_USR = 0x10,
       VCM_SVC = 0x13,
       VCM_NON = 0x00 
};

void HELPER(vpmu_accumulate_tb_info)(CPUX86State *env, void *opaque)
{
    CPUState *   cs            = CPU(ENV_GET_CPU(env));
    ExtraTBInfo *extra_tb_info = (ExtraTBInfo *)opaque;
    int     cpl  = env->hflags & (3); // CPU-Privilege-Level = User(ring3) / Supervisor(ring0)
    uint8_t mode = VCM_NON;
#ifdef CONFIG_VPMU_SET
    VPMU.cpu_arch_state = env;
#endif

    vpmu_current_extra_tb_info = extra_tb_info;

    if (likely(env && VPMU.enabled)) {

        if (cpl == 0) {
            mode = VCM_SVC;
        } else if (cpl == 3) {
            mode = VCM_USR;
        } else {
            CONSOLE_LOG("unhandled privilege : %d\n", cpl);
        }

        if (vpmu_model_has(VPMU_INSN_COUNT_SIM, VPMU)) {
            vpmu_insn_ref(cs->cpu_index, mode, extra_tb_info);
        } // End of VPMU_INSN_COUNT_SIM

        if (vpmu_model_has(VPMU_ICACHE_SIM, VPMU)) {
            uint16_t type = CACHE_PACKET_INSN;
            if (extra_tb_info->modelsel.hot_tb_flag) {
                type |= VPMU_PACKET_HOT;
            }
            cache_ref(PROCESSOR_CPU,
                      cs->cpu_index,
                      extra_tb_info->start_addr,
                      type,
                      extra_tb_info->counters.size_bytes);
        } // End of VPMU_ICACHE_SIM
    }
}

void HELPER(vpmu_memory_access)(CPUX86State *env, uint64_t addr, uint64_t rw, uint64_t size)
{
    CPUState *   cs            = CPU(ENV_GET_CPU(env));
    int     cpl  = env->hflags & (3); // CPU-Privilege-Level = User(ring3) / Supervisor(ring0)
    uint8_t mode = VCM_NON;
 
    if (likely(env && VPMU.enabled)) {
        if (cpl == 0) {
            mode = VCM_SVC;
        } else if (cpl == 3) {
            mode = VCM_USR;
        } else {
            CONSOLE_LOG("unhandled privilege : %d\n", cpl);
        }

        // CONSOLE_LOG("cpu_index=%d mode=%d\n", cs->cpu_index, mode);
        if( mode == VCM_SVC ){
            if (vpmu_model_has(VPMU_DCACHE_SIM, VPMU)) {
                cache_ref(PROCESSOR_CPU, cs->cpu_index, addr, rw, size);
            }
        }
        else if( mode == VCM_USR ){
            if (vpmu_model_has(VPMU_DCACHE_SIM, VPMU)) {
                cache_ref(PROCESSOR_CPU, cs->cpu_index, addr, rw, size);
            }
        }
    }
}
