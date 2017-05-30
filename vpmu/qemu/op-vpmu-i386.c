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

void
  HELPER(vpmu_memory_access)(CPUX86State *env, uint64_t addr, uint64_t rw, uint64_t size)
{
    CPUState *cs = CPU(ENV_GET_CPU(env));
    int cpl = env->hflags & (3); // CPU-Privilege-Level = User(ring3) / Supervisor(ring0)
    uint8_t mode = X86_CPU_MODE_NON;
    // Get the core id from CPUState structure
    int core_id = cs->cpu_index;

    if (likely(VPMU.enabled)) {
        if (cpl == 0) {
            mode = X86_CPU_MODE_SVC;
        } else if (cpl == 3) {
            mode = X86_CPU_MODE_USR;
        } else {
            CONSOLE_LOG(STR_VPMU "Unhandled privilege : %d\n", cpl);
        }

        // TODO Is this VA the real address fed into cache?? Ex: ARM uses MVA
        if (vpmu_model_has(VPMU_DCACHE_SIM, VPMU)) {
            if (unlikely(VPMU.iomem_access_flag)) {
                // IO segment
                VPMU.iomem_access_flag = 0; // Clear flag
                VPMU.iomem_count++;
            } else {
                // Memory segment
                if (VPMU.core[core_id].hot_tb_flag) {
                    rw |= VPMU_PACKET_HOT;
                }
                cache_ref(PROCESSOR_CPU, core_id, addr, rw, size);
            }
        }
    }
    (void)mode;
}

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
        if (vpmu_model_has(VPMU_JIT_MODEL_SELECT, VPMU)) {
            uint64_t distance = VPMU.modelsel[core_id].total_tb_visit_count
                                - extra_tb_info->modelsel.last_visit;

            if (distance < 100) {
#ifdef CONFIG_VPMU_DEBUG_MSG
                VPMU.modelsel[core_id].hot_tb_visit_count++;
#endif
                extra_tb_info->modelsel.hot_tb_flag = true;
            } else {
#ifdef CONFIG_VPMU_DEBUG_MSG
                VPMU.modelsel[core_id].cold_tb_visit_count++;
#endif
                extra_tb_info->modelsel.hot_tb_flag = false;
            }
            // Advance timestamp
            extra_tb_info->modelsel.last_visit =
              VPMU.modelsel[core_id].total_tb_visit_count;
            VPMU.modelsel[core_id].total_tb_visit_count++;
            VPMU.core[core_id].hot_tb_flag = extra_tb_info->modelsel.hot_tb_flag;
        } else {
            extra_tb_info->modelsel.hot_tb_flag = false;
            VPMU.core[core_id].hot_tb_flag      = false;
        } // End of VPMU_JIT_MODEL_SELECT

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

#if 0
// TODO: Prepare to be deprecated
// helper function for ET and other usage. Only "taken" branch will enter this helper.
void HELPER(vpmu_et_call)(CPUX86State *env, uint64_t target_addr, uint64_t return_addr)
{
#ifdef CONFIG_VPMU_SET
#endif
}

// helper function for ET and other usage.
void HELPER(vpmu_et_jmp)(CPUX86State *env, uint64_t vaddr)
{
#ifdef CONFIG_VPMU_SET
// et_x86_check_function_call(env, vaddr, vaddr);
#endif
}
#endif
