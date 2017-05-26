#include "vpmu/qemu/vpmu-qemu.h"        // ExtraTB, XXX_ref() etc.
#include "vpmu/packet/vpmu-packet.h"    // data types for sending traces
#include "vpmu/qemu/vpmu-device.h"      // vpmu_model_has and its macros
#include "vpmu/component/vpmu-insn.h"   // vpmu_insn_ref
#include "vpmu/component/vpmu-cache.h"  // vpmu_cache_ref
#include "vpmu/component/vpmu-branch.h" // vpmu_branch_ref
#include "vpmu/phase/phase.h"           // phasedet_ref

enum VPMU_X86_CPU_MODE {
    X86_CPU_MODE_USR = 0x10,
    X86_CPU_MODE_SVC = 0x13,
    X86_CPU_MODE_NON = 0x00
};

void HELPER(vpmu_accumulate_tb_info)(CPUX86State *env, void *opaque)
{
    CPUState *   cs            = CPU(ENV_GET_CPU(env));
    ExtraTBInfo *extra_tb_info = (ExtraTBInfo *)opaque;
    int cpl = env->hflags & (3); // CPU-Privilege-Level = User(ring3) / Supervisor(ring0)
    uint8_t mode = X86_CPU_MODE_NON;
    // Get the core id from CPUState structure
    int core_id = cs->cpu_index;

    if (cpl == 0) {
        mode = X86_CPU_MODE_SVC;
    } else if (cpl == 3) {
        mode = X86_CPU_MODE_USR;
    } else {
        CONSOLE_LOG("unhandled privilege : %d\n", cpl);
    }
#ifdef CONFIG_VPMU_SET
    // Only need to check function calls when TB is not contiguous
    if (extra_tb_info->start_addr - VPMU.core[core_id].last_tb_pc > 8) {
        et_check_function_call(env, extra_tb_info->start_addr);
    }
    if (vpmu_model_has(VPMU_PHASEDET, VPMU)) {
        phasedet_ref(
          (mode == X86_CPU_MODE_USR), extra_tb_info, env->regs[R_ESP], cs->cpu_index);
    } // End of VPMU_PHASEDET
#endif

    if (likely(env && VPMU.enabled)) {

        if (vpmu_model_has(VPMU_INSN_COUNT_SIM, VPMU)) {
            vpmu_insn_ref(cs->cpu_index, mode, extra_tb_info);
        } // End of VPMU_INSN_COUNT_SIM

        if (vpmu_model_has(VPMU_ICACHE_SIM, VPMU)) {
            uint16_t type = CACHE_PACKET_INSN;
            cache_ref(PROCESSOR_CPU,
                      cs->cpu_index,
                      extra_tb_info->start_addr,
                      type,
                      extra_tb_info->counters.size_bytes);
        } // End of VPMU_ICACHE_SIM

        if (vpmu_model_has(VPMU_BRANCH_SIM, VPMU)) {
            // Add global counter value of branch count.
            if (VPMU.core[core_id].last_tb_has_branch) {
                if (extra_tb_info->start_addr - VPMU.core[core_id].last_tb_pc <= 8) {
                    branch_ref(
                      cs->cpu_index, VPMU.core[core_id].last_tb_pc, 0); // Not taken
                } else {
                    branch_ref(cs->cpu_index, VPMU.core[core_id].last_tb_pc, 1); // Taken
                }
            }
        } // End of VPMU_BRANCH_SIM
    }

    VPMU.core[core_id].last_tb_pc =
      extra_tb_info->start_addr + extra_tb_info->counters.size_bytes;
    VPMU.core[core_id].last_tb_has_branch = extra_tb_info->has_branch;
}

void
  HELPER(vpmu_memory_access)(CPUX86State *env, uint64_t addr, uint64_t rw, uint64_t size)
{
    CPUState *cs = CPU(ENV_GET_CPU(env));
    int cpl = env->hflags & (3); // CPU-Privilege-Level = User(ring3) / Supervisor(ring0)
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
        if (mode == X86_CPU_MODE_SVC) {
            if (vpmu_model_has(VPMU_DCACHE_SIM, VPMU)) {
                cache_ref(PROCESSOR_CPU, cs->cpu_index, addr, rw, size);
            }
        } else if (mode == X86_CPU_MODE_USR) {
            if (vpmu_model_has(VPMU_DCACHE_SIM, VPMU)) {
                cache_ref(PROCESSOR_CPU, cs->cpu_index, addr, rw, size);
            }
        }
    }
}

// helper function for ET and other usage. Only "taken" branch will enter this helper.
void HELPER(vpmu_et_call)(CPUX86State *env, uint64_t target_addr, uint64_t return_addr)
{
#ifdef CONFIG_VPMU_SET
// TODO: No kernel function reaches here.
// et_x86_check_function_call(env, target_addr, return_addr);
#endif
}

// helper function for ET and other usage.
void HELPER(vpmu_et_jmp)(CPUX86State *env, uint64_t vaddr)
{
#ifdef CONFIG_VPMU_SET
// et_x86_check_function_call(env, vaddr, vaddr);
#endif
}
