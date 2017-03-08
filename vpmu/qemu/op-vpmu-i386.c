#include "vpmu/include/vpmu-qemu.h"          // ExtraTB, XXX_ref() etc.
#include "vpmu/include/packet/vpmu-packet.h" // data types for sending traces
#include "vpmu/include/vpmu-device.h"        // vpmu_model_has and its macros
#include "vpmu/include/arch/vpmu-insn.h"     // vpmu_insn_ref
#include "vpmu/include/arch/vpmu-cache.h"    // vpmu_cache_ref
#include "vpmu/include/arch/vpmu-branch.h"   // vpmu_branch_ref
#include "vpmu/include/phase/phase.h"        // phasedet_ref


enum VPMU_X86_CPU_MODE{
       X86_CPU_MODE_USR = 0x10,
       X86_CPU_MODE_SVC = 0x13,
       X86_CPU_MODE_NON = 0x00 
};

void HELPER(vpmu_accumulate_tb_info)(CPUX86State *env, void *opaque)
{
    CPUState *   cs            = CPU(ENV_GET_CPU(env));
    ExtraTBInfo *extra_tb_info = (ExtraTBInfo *)opaque;
    int     cpl  = env->hflags & (3); // CPU-Privilege-Level = User(ring3) / Supervisor(ring0)
    uint8_t mode = X86_CPU_MODE_NON;
    static unsigned int last_tb_pc         = 0;
    static unsigned int last_tb_has_branch = 0;


#ifdef CONFIG_VPMU_SET
    VPMU.cpu_arch_state = env;
#endif

    vpmu_current_extra_tb_info = extra_tb_info;

    if (likely(env && VPMU.enabled)) {

        if (cpl == 0) {
            mode = X86_CPU_MODE_SVC;
        } else if (cpl == 3) {
            mode = X86_CPU_MODE_USR;
        } else {
            CONSOLE_LOG("unhandled privilege : %d\n", cpl);
        }

#ifdef CONFIG_VPMU_SET
        if (vpmu_model_has(VPMU_PHASEDET, VPMU)) {
            phasedet_ref((mode == X86_CPU_MODE_USR), extra_tb_info);
        } // End of VPMU_PHASEDET

        et_x86_check_mmap_return(env, extra_tb_info->start_addr);
#endif

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

        if (vpmu_model_has(VPMU_BRANCH_SIM, VPMU)) {
            // Add global counter value of branch count.
            if (last_tb_has_branch) {
                if (extra_tb_info->start_addr - last_tb_pc <= 8) {
                    branch_ref(cs->cpu_index, last_tb_pc, 0); // Not taken
                } else {
                    branch_ref(cs->cpu_index, last_tb_pc, 1); // Taken
                }
            }
            last_tb_pc = extra_tb_info->start_addr + extra_tb_info->counters.size_bytes;
            last_tb_has_branch = extra_tb_info->has_branch;
        } // End of VPMU_BRANCH_SIM
    }
}

void HELPER(vpmu_memory_access)(CPUX86State *env, uint64_t addr, uint64_t rw, uint64_t size)
{
    CPUState *   cs            = CPU(ENV_GET_CPU(env));
    int     cpl  = env->hflags & (3); // CPU-Privilege-Level = User(ring3) / Supervisor(ring0)
    uint8_t mode = X86_CPU_MODE_NON;
 
    if (likely(env && VPMU.enabled)) {
        if (cpl == 0) {
            mode = X86_CPU_MODE_SVC;
        } else if (cpl == 3) {
            mode = X86_CPU_MODE_USR;
        } else {
            CONSOLE_LOG("unhandled privilege : %d\n", cpl);
        }

        // CONSOLE_LOG("cpu_index=%d mode=%d\n", cs->cpu_index, mode);
        if( mode == X86_CPU_MODE_SVC ){
            if (vpmu_model_has(VPMU_DCACHE_SIM, VPMU)) {
                cache_ref(PROCESSOR_CPU, cs->cpu_index, addr, rw, size);
            }
        }
        else if( mode == X86_CPU_MODE_USR ){
            if (vpmu_model_has(VPMU_DCACHE_SIM, VPMU)) {
                cache_ref(PROCESSOR_CPU, cs->cpu_index, addr, rw, size);
            }
        }
    }
}

// helper function for SET and other usage. Only "taken" branch will enter this helper.
#if TARGET_LONG_BITS == 32
void HELPER(vpmu_branch)(CPUX86State *env, uint32_t target_addr, uint32_t return_addr)
#elif TARGET_LONG_BITS == 64
void HELPER(vpmu_branch)(CPUX86State *env, uint64_t target_addr, uint64_t return_addr)
#else
#error Unhandled TARGET_LONG_BITS value
#endif
{
#ifdef CONFIG_VPMU_SET
    et_x86_check_function_call(env, target_addr, return_addr);
#endif

    if (likely(VPMU.enabled)) {
        // CONSOLE_LOG("pc: %x->%x\n", return_addr, target_addr);
    }
}
