#include "vpmu/include/vpmu-qemu.h"          // ExtraTB, XXX_ref() etc.
#include "vpmu/include/packet/vpmu-packet.h" // data types for sending traces
#include "vpmu/include/vpmu-device.h"        // vpmu_model_has and its macros
#include "vpmu/include/arch/vpmu-inst.h"     // vpmu_inst_ref
#include "vpmu/include/arch/vpmu-cache.h"    // vpmu_cache_ref
#include "vpmu/include/arch/vpmu-branch.h"   // vpmu_branch_ref

// helper function to calculate TLB misses
void HELPER(vpmu_tlb_access)(uint32_t addr)
{
    if (likely(VPMU.enabled)) {
    }
}

// helper function to caclulate cache reference
void
  HELPER(vpmu_memory_access)(CPUARMState *env, uint32_t addr, uint32_t rw, uint32_t size)
{
    CPUState *cs = CPU(ENV_GET_CPU(env));
    // static uint32_t cnt = 0;
    if (likely(VPMU.enabled)) {
        if (vpmu_model_has(VPMU_DCACHE_SIM, VPMU)) {
            // PLD instructions
            // http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.ddi0434b/CHDEDHHD.html
            if (rw == 0xff) { // 0xff is ARM_PLD
                addr = env->regs[(addr >> 16) & 0xf] + (addr & 0xfff);
                rw   = CACHE_PACKET_READ;
            }

#ifdef TARGET_ARM
// TODO check this formula and modify this line
// addr = addr & 0x000 | pid ;
#endif

            if (unlikely(VPMU.iomem_access_flag)) {
                // IO segment
                VPMU.iomem_access_flag = 0; // Clear flag
                VPMU.iomem_count++;
            } else {
                // Memory segment
                if (vpmu_current_extra_tb_info->modelsel.hot_tb_flag) {
                    rw |= VPMU_PACKET_HOT;
                }
                cache_ref(PROCESSOR_CPU, cs->cpu_index, addr, rw, size);
            }
        }
    }
}

// helper function for SET and other usage. Only "taken" branch will enter this helper.
#if TARGET_LONG_BITS == 32
void HELPER(vpmu_branch)(CPUARMState *env, uint32_t target_addr, uint32_t return_addr)
#elif TARGET_LONG_BITS == 64
void HELPER(vpmu_branch)(CPUARMState *env, uint64_t target_addr, uint64_t return_addr)
#else
#error Unhandled TARGET_LONG_BITS value
#endif
{
#ifdef CONFIG_VPMU_SET
    et_check_function_call(env, target_addr, return_addr);
#endif

    if (likely(VPMU.enabled)) {
        // CONSOLE_LOG("pc: %x->%x\n", return_addr, target_addr);
    }
}

// helper function to accumulate counters
void HELPER(vpmu_accumulate_tb_info)(CPUARMState *env, void *opaque)
{
    CPUState *cs = CPU(ENV_GET_CPU(env));
    // Thses are for branch
    static unsigned int last_tb_pc         = 0;
    static unsigned int last_tb_has_branch = 0;

    ExtraTBInfo *extra_tb_info = (ExtraTBInfo *)opaque;
    // mode = User(USR)/Supervisor(SVC)/Interrupt Request(IRQ)
    uint8_t mode = env->uncached_cpsr & CPSR_M;

    vpmu_current_extra_tb_info = extra_tb_info;

    //    static uint32_t return_addr = 0;
    //    static uint32_t last_issue_time = 0;
    //    char *state = &(VPMU.state);

    if (likely(env && VPMU.enabled)) {
        if (vpmu_model_has(VPMU_JIT_MODEL_SELECT, VPMU)) {
            uint64_t distance =
              VPMU.modelsel.total_tb_visit_count - extra_tb_info->modelsel.last_visit;

            if (distance < 100) {
#ifdef CONFIG_VPMU_DEBUG_MSG
                VPMU.modelsel.hot_tb_visit_count++;
#endif
                extra_tb_info->modelsel.hot_tb_flag = 1;
            } else {
#ifdef CONFIG_VPMU_DEBUG_MSG
                VPMU.modelsel.cold_tb_visit_count++;
#endif
                extra_tb_info->modelsel.hot_tb_flag = 0;
            }
            // Advance timestamp
            extra_tb_info->modelsel.last_visit = VPMU.modelsel.total_tb_visit_count;
            VPMU.modelsel.total_tb_visit_count++;
        } else {
            extra_tb_info->modelsel.hot_tb_flag = 0;
        } // End of VPMU_JIT_MODEL_SELECT

        if (vpmu_model_has(VPMU_INSN_COUNT_SIM, VPMU)) {
            vpmu_inst_ref(cs->cpu_index, mode, extra_tb_info);
        } // End of VPMU_INSN_COUNT_SIM

        if (vpmu_model_has(VPMU_ICACHE_SIM, VPMU)) {
            uint16_t type = CACHE_PACKET_INSTRN;
            if (extra_tb_info->modelsel.hot_tb_flag) {
                type |= VPMU_PACKET_HOT;
            }
            cache_ref(PROCESSOR_CPU,
                      cs->cpu_index,
                      extra_tb_info->start_addr,
                      type,
                      extra_tb_info->counters.size_bytes);
        } // End of VPMU_ICACHE_SIM

        if (vpmu_model_has(VPMU_PIPELINE_SIM, VPMU)) {
            VPMU.ticks += extra_tb_info->ticks;
        } // End of VPMU_PIPELINE_SIM

        if (vpmu_model_has(VPMU_BRANCH_SIM, VPMU)) {
            // Add global counter value of branch count.
            if (last_tb_has_branch) {
                if (extra_tb_info->start_addr - last_tb_pc <= 4) {
                    branch_ref(cs->cpu_index, last_tb_pc, 0); // Not taken
                } else {
                    branch_ref(cs->cpu_index, last_tb_pc, 1); // Taken
                }
            }
            last_tb_pc = extra_tb_info->start_addr + extra_tb_info->counters.size_bytes;
            last_tb_has_branch = extra_tb_info->has_branch;
        } // End of VPMU_BRANCH_SIM

#if 0
        /* TODO: this mechanism should be wrapped */
        /* asm_do_IRQ handles all the hardware interrupts, not only for timer interrupts
         * pac_timer_interrupt() is the timer handler which asm_do_IRQ will call
         * run_softirqd is softirq handler in older Linux version
         * Linux handle arm interrupt with run_timer_softirq()*/
        //static char timer_interrupt_exception = 0;
        char *timer_interrupt_exception = &(VPMU.timer_interrupt_exception);

        if (env->regs[15] == ISR_addr[TICK_SCHED_TIMER])
            //if (env->regs[15] == ISR_addr[RUN_TIMER_SOFTIRQ]
            //    || env->regs[15] == ISR_addr[GOLDFISH_TIMER_INTERRUPT])
        {
            if (*state == 0) {
                /* calibrate the timer interrupt */
                int64_t tmp;
                tmp = vpmu_estimated_execution_time() - last_issue_time;
                /* timer interrupt interval = 10 ms = 10000 us */
                if (tmp > TIMER_INTERRUPT_INTERVAL) {
                    last_issue_time = vpmu_estimated_execution_time();
                    *timer_interrupt_exception = 1;
                }
                /* The link register in asm_do_irq are being cleared
                 * so we cannot use env->regs[14] directly */
                return_addr = VPMU.timer_interrupt_return_pc;
                //printf("vpmu remembered return PC=0x%x\n",return_addr);
            }
            *state = 1;
        }

        /* In timer interrupt state */
        if (unlikely(*state == 1)) {
            /* check if timer interrupt is returned */
            if (unlikely((return_addr - 4) == env->regs[15])) {
                /* timer interrupt returned */
                *state = 0;
                *timer_interrupt_exception = 0;
            } else {
                /* still in the timer interrupt stage
                 * Prevent timer interrupt to be counted, must return
                 */
                if (*timer_interrupt_exception == 0)
                    return;
            }
        }
#endif
    }
}
