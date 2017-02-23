#include "vpmu/include/vpmu-qemu.h"          // ExtraTB, XXX_ref() etc.
#include "vpmu/include/packet/vpmu-packet.h" // data types for sending traces
#include "vpmu/include/vpmu-device.h"        // vpmu_model_has and its macros
#include "vpmu/include/arch/vpmu-insn.h"     // vpmu_insn_ref
#include "vpmu/include/arch/vpmu-cache.h"    // vpmu_cache_ref
#include "vpmu/include/arch/vpmu-branch.h"   // vpmu_branch_ref

void HELPER(vpmu_accumulate_tb_info)(CPUX86State *env, void *opaque)
{
    CPUState *   cs            = CPU(ENV_GET_CPU(env));
    ExtraTBInfo *extra_tb_info = (ExtraTBInfo *)opaque;
    int     cpl  = env->hflags & (3); // CPU-Privilege-Level = User(ring3) / Supervisor(ring0)
    uint8_t mode = 0x10;
    // copy from target-arm/cpu.h
    // enum arm_cpu_mode{
    //    USR = 0x10,
    //    SVC = 0x13
    // };

    vpmu_current_extra_tb_info = extra_tb_info;

    switch (cpl) {
    case 0:
        mode = 0x13;
        break;
    case 3:
        mode = 0x10;
        break;
    default:
        CONSOLE_LOG("unhandled privilege : %d\n", cpl);
    }

    if (likely(env && VPMU.enabled)) {
        if (vpmu_model_has(VPMU_INSN_COUNT_SIM, VPMU)) {
            vpmu_insn_ref(cs->cpu_index, mode, extra_tb_info); 
        } // End of VPMU_INSN_COUNT_SIM
    }
}

void HELPER(vpmu_memory_access)(CPUX86State *env, uint64_t addr, uint64_t rw, uint64_t size)
{
    CPUState *   cs            = CPU(ENV_GET_CPU(env));
    int     cpl  = env->hflags & (3); // CPU-Privilege-Level = User(ring3) / Supervisor(ring0)
    uint8_t mode = 0x10;
    // copy from target-arm/cpu.h
    // enum arm_cpu_mode{
    //    USR = 0x10,
    //    SVC = 0x13
    // };

    switch (cpl) {
    case 0:
        mode = 0x13;
        break;
    case 3:
        mode = 0x10;
        break;
    default:
        CONSOLE_LOG("unhandled privilege : %d\n", cpl);
    }

    if (likely(env && VPMU.enabled)) {
        // CONSOLE_LOG("cpu_index=%d mode=%d\n", cs->cpu_index, mode);
        if( mode == 0x13 ){
            if (vpmu_model_has(VPMU_DCACHE_SIM, VPMU)) {
                cache_ref(PROCESSOR_CPU, cs->cpu_index, addr, rw, size);
            }
        }
        else if( mode == 0x10 ){
            if (vpmu_model_has(VPMU_DCACHE_SIM, VPMU)) {
                cache_ref(PROCESSOR_CPU, cs->cpu_index, addr, rw, size);
            }
        }
    }
}
