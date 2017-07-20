#include "vpmu/qemu/vpmu-qemu.h"        // ExtraTB, XXX_ref() etc.
#include "vpmu/packet/vpmu-packet.h"    // data types for sending traces
#include "vpmu/qemu/vpmu-device.h"      // vpmu_model_has and its macros
#include "vpmu/component/vpmu-insn.h"   // vpmu_insn_ref
#include "vpmu/component/vpmu-cache.h"  // vpmu_cache_ref
#include "vpmu/component/vpmu-branch.h" // vpmu_branch_ref
#include "vpmu/phase/phase.h"           // phasedet_ref

#ifdef TARGET_ARM
enum VPMU_ARCH_MODE {
    VPMU_ARCH_MODE_USR = ARM_CPU_MODE_USR,
    VPMU_ARCH_MODE_SVC = ARM_CPU_MODE_SVC,
};
#define vpmu_arch_sp env->regs[13]

#elif defined(TARGET_X86_64) || defined(TARGET_I386)
enum VPMU_ARCH_MODE {
    VPMU_ARCH_MODE_USR = 0x10,
    VPMU_ARCH_MODE_SVC = 0x13,
};
#define vpmu_arch_sp env->regs[R_ESP]

#endif

static inline target_ulong
vaddr_to_mvaddr(CPUArchState *env, target_ulong addr, target_ulong rw)
{
#ifdef TARGET_ARM
    // PLD instructions
    // http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.ddi0434b/CHDEDHHD.html
    if (rw == 0xff) { // 0xff is ARM_PLD
        addr = env->regs[(addr >> 16) & 0xf] + (addr & 0xfff);
        rw   = CACHE_PACKET_READ;
    }

    // ARM Fast Context Switch Extension (FCSE): VA -> MVA
    int mmu_idx = cpu_mmu_index(env, false);
    if (addr < 0x02000000 && mmu_idx != ARMMMUIdx_S2NS
        && !arm_feature(env, ARM_FEATURE_V8)) {
        if (mmu_idx == ARMMMUIdx_S1E3) {
            addr += env->cp15.fcseidr_s;
        } else {
            addr += env->cp15.fcseidr_ns;
        }
    }

#elif defined(TARGET_X86_64) || defined(TARGET_I386)
// TODO Is this VA the real address fed into cache?? Ex: ARM uses MVA
#else
#error "Not supported"
#endif

    return addr;
}

static inline int vpmu_get_arch_mode(CPUArchState *env)
{
    int mode = 0;
#ifdef TARGET_ARM
    // mode = User(USR)/Supervisor(SVC)/Interrupt Request(IRQ)
    mode = env->uncached_cpsr & CPSR_M;
#elif defined(TARGET_X86_64) || defined(TARGET_I386)
    // CPU-Privilege-Level = User(ring3) / Supervisor(ring0)
    int cpl = env->hflags & (3);
    // Use mode to replace ring level
    mode = 0;
    if (cpl == 0) {
        mode = VPMU_ARCH_MODE_SVC;
    } else if (cpl == 3) {
        mode = VPMU_ARCH_MODE_USR;
    } else {
        CONSOLE_LOG(STR_VPMU "Unhandled privilege : %d\n", cpl);
    }
#else
#error "Not supported"
#endif
    return mode;
}

// helper function to caclulate cache reference
void HELPER(vpmu_memory_access)(CPUArchState *env,
                                target_ulong  addr,
                                target_ulong  rw,
                                target_ulong  size)
{
    CPUState *cs = CPU(ENV_GET_CPU(env));
    // Get the core id from CPUState structure
    int core_id = cs->cpu_index;
    // static uint32_t cnt = 0;

    // Exit all added helper functions directly when QEMU is going to be terminated.
    if (unlikely(VPMU.qemu_terminate_flag)) return;
    // Exit if VPMU is not enabled
    if (unlikely(env == NULL || !VPMU.enabled)) return;

    if (vpmu_model_has(VPMU_DCACHE_SIM, VPMU)) {
        addr = vaddr_to_mvaddr(env, addr, rw);
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

// helper function to accumulate counters
void HELPER(vpmu_accumulate_tb_info)(CPUArchState *env, void *opaque)
{
    // QEMU get cpu state object
    CPUState *cs = CPU(ENV_GET_CPU(env));
    // Cast pointer to ExtraTBInfo *
    ExtraTBInfo *extra_tb_info = (ExtraTBInfo *)opaque;
    // Get the core id from CPUState structure
    int core_id = cs->cpu_index;
    // Determine if the PC is contiguous
    bool contiguous_pc_flag =
      ((extra_tb_info->start_addr - VPMU.core[core_id].last_tb_pc) <= TARGET_LONG_SIZE);
    // Get CPU mode (architecture safe function)
    int mode = vpmu_get_arch_mode(env);
    // Record the mode of each TB in its extra tb info
    extra_tb_info->cpu_mode = mode;

    // static uint32_t return_addr = 0;
    // static uint32_t last_issue_time = 0;
    // char *state = &(VPMU.state);

    // Exit all added helper functions directly when QEMU is going to be terminated.
    if (unlikely(VPMU.qemu_terminate_flag)) return;

#ifdef CONFIG_VPMU_SET
    // Only need to check function calls when TB is not contiguous
    if (!contiguous_pc_flag) {
        et_check_function_call(
          env, cs->cpu_index, (mode == VPMU_ARCH_MODE_USR), extra_tb_info->start_addr);
    }
    if (vpmu_model_has(VPMU_PHASEDET, VPMU)) {
        phasedet_ref((mode == VPMU_ARCH_MODE_USR), extra_tb_info, vpmu_arch_sp, core_id);
    } // End of VPMU_PHASEDET
#endif

    // Exit if VPMU is not enabled, must be done after event tracing functions
    if (unlikely(env == NULL || !VPMU.enabled)) return;

    // The following codes send traces accordingly
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
        extra_tb_info->modelsel.last_visit = VPMU.modelsel[core_id].total_tb_visit_count;
        VPMU.modelsel[core_id].total_tb_visit_count++;
        VPMU.core[core_id].hot_tb_flag = extra_tb_info->modelsel.hot_tb_flag;
    } else {
        extra_tb_info->modelsel.hot_tb_flag = false;
        VPMU.core[core_id].hot_tb_flag      = false;
    } // End of VPMU_JIT_MODEL_SELECT

    if (vpmu_model_has(VPMU_INSN_COUNT_SIM, VPMU)) {
        vpmu_insn_ref(core_id, mode, extra_tb_info);
    } // End of VPMU_INSN_COUNT_SIM

    if (vpmu_model_has(VPMU_ICACHE_SIM, VPMU)) {
        uint16_t type = CACHE_PACKET_INSN;
        if (extra_tb_info->modelsel.hot_tb_flag) {
            type |= VPMU_PACKET_HOT;
        }
        cache_ref(PROCESSOR_CPU,
                  core_id,
                  extra_tb_info->start_addr,
                  type,
                  extra_tb_info->counters.size_bytes);
    } // End of VPMU_ICACHE_SIM

    if (vpmu_model_has(VPMU_PIPELINE_SIM, VPMU)) {
        VPMU.ticks += extra_tb_info->ticks;
    } // End of VPMU_PIPELINE_SIM

    if (vpmu_model_has(VPMU_BRANCH_SIM, VPMU)) {
        // Add global counter value of branch count.
        if (VPMU.core[core_id].last_tb_has_branch) {
            if (contiguous_pc_flag) {
                branch_ref(core_id, VPMU.core[core_id].last_tb_pc, 0); // Not taken
            } else {
                branch_ref(core_id, VPMU.core[core_id].last_tb_pc, 1); // Taken
            }
        }
    } // End of VPMU_BRANCH_SIM

    // Update per-core state info
    VPMU.core[core_id].last_tb_pc =
      extra_tb_info->start_addr + extra_tb_info->counters.size_bytes;
    VPMU.core[core_id].last_tb_has_branch = extra_tb_info->has_branch;

#if 0
    /* TODO: this mechanism should be wrapped */
    /* asm_do_IRQ handles all the hardware interrupts, not only for timer interrupts
     * pac_timer_interrupt() is the timer handler which asm_do_IRQ will call
     * run_softirqd is softirq handler in older Linux version
     * Linux handle arm interrupt with run_timer_softirq()*/
    // static char timer_interrupt_exception = 0;
    char *timer_interrupt_exception = &(VPMU.timer_interrupt_exception);

    if (env->regs[15] == ISR_addr[TICK_SCHED_TIMER])
    // if (env->regs[15] == ISR_addr[RUN_TIMER_SOFTIRQ]
    //    || env->regs[15] == ISR_addr[GOLDFISH_TIMER_INTERRUPT])
    {
        if (*state == 0) {
            /* calibrate the timer interrupt */
            int64_t tmp;
            tmp = vpmu_estimated_execution_time() - last_issue_time;
            /* timer interrupt interval = 10 ms = 10000 us */
            if (tmp > TIMER_INTERRUPT_INTERVAL) {
                last_issue_time            = vpmu_estimated_execution_time();
                *timer_interrupt_exception = 1;
            }
            /* The link register in asm_do_irq are being cleared
             * so we cannot use env->regs[14] directly */
            return_addr = VPMU.timer_interrupt_return_pc;
            // printf("vpmu remembered return PC=0x%x\n",return_addr);
        }
        *state = 1;
    }

    /* In timer interrupt state */
    if (unlikely(*state == 1)) {
        /* check if timer interrupt is returned */
        if (unlikely((return_addr - 4) == env->regs[15])) {
            /* timer interrupt returned */
            *state                     = 0;
            *timer_interrupt_exception = 0;
        } else {
            /* still in the timer interrupt stage
             * Prevent timer interrupt to be counted, must return
             */
            if (*timer_interrupt_exception == 0) return;
        }
    }
#endif
}

#undef vpmu_arch_sp
#if 0
// TODO: Prepare to be deprecated
// helper function for SET and other usage. Only "taken" branch will enter this helper.
#if TARGET_LONG_BITS == 32
void HELPER(vpmu_branch)(CPUARMState *env, uint32_t target_addr, uint32_t return_addr)
#elif TARGET_LONG_BITS == 64
void HELPER(vpmu_branch)(CPUARMState *env, uint64_t target_addr, uint64_t return_addr)
#else
#error Unhandled TARGET_LONG_BITS value
#endif
{
    if (likely(VPMU.enabled)) {
        // CONSOLE_LOG("pc: %x->%x\n", return_addr, target_addr);
    }
}

// helper function to calculate TLB misses
void HELPER(vpmu_tlb_access)(uint32_t addr)
{
    if (likely(VPMU.enabled)) {
    }
}
#endif
