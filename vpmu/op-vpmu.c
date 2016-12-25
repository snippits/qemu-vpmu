#include "vpmu/include/vpmu-device.h"
#include "vpmu/include/vpmu-packet.h"
#include "vpmu/include/vpmu-qemu.h"

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
                    hot_cache_ref(PROCESSOR_CPU, 0, addr, rw, size);
                } else {
                    cache_ref(PROCESSOR_CPU, 0, addr, rw, size);
                }
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
    CPUState *cs                 = CPU(ENV_GET_CPU(env));
    VPMU.cs                      = cs;
    static uint32_t current_pid  = 0;
    static char     flag_tracing = 0;
    uint32_t *      traced_pid   = VPMU.traced_process_pid;

    // Linux Kernel: Fork a process
    if (flag_tracing && target_addr == VPMU.do_fork_addr) {
        // DBG("fork from %d\n", current_pid);
    }

    // Linux Kernel: wake up the newly forked process
    if (flag_tracing && target_addr == VPMU.wake_up_new_task_addr) {
        // This is kernel v3.6.11
        // uint32_t target_pid = READ_INT_FROM_GUEST(cs, env->regs[0], 204);
        // This is kernel v4.4.0
        uint32_t target_pid = READ_INT_FROM_GUEST(cs, env->regs[0], 512);
        if (current_pid != 0 && current_pid == traced_pid[0]) {
            traced_pid[VPMU.num_traced_threads] = target_pid;
            VPMU.num_traced_threads++;
        }
    }

    // Linux Kernel: New process creation
    if (!flag_tracing && target_addr == VPMU.execve_addr) {
        // This is kernel v3.6.11
        // char *filepath = (char *)READ_FROM_GUEST_KERNEL(cs, env->regs[0], 0);
        // This is kernel v4.4.0
        uintptr_t name_addr = (uintptr_t)READ_INT_FROM_GUEST(cs, env->regs[0], 0);
        char *    filepath  = (char *)READ_FROM_GUEST_KERNEL(cs, name_addr, 0);
        if (VPMU.traced_process_name[0] != '\0'
            && strstr(filepath, VPMU.traced_process_name) != NULL) {
            // DBG("target_addr == %x from %x\n", target_addr, return_addr);
            // DBG("file: %s (pid=%d)\n", filepath, current_pid);
            tic(&(VPMU.start_time));
            VPMU_reset();
            vpmu_model_setup(&VPMU, VPMU.traced_process_model);
            vpmu_simulator_status(&VPMU, 0);
            traced_pid[0]           = current_pid;
            VPMU.num_traced_threads = 1;
            VPMU.enabled            = 1;
            flag_tracing            = 1;
        }
    }

    // Linux Kernel: Process End
    if (flag_tracing && target_addr == VPMU.exit_addr) {
        int flag_hit = 0;
        // Loop through pid list to find if hit
        for (int i = 0; traced_pid[i] != 0; i++) {
            if (current_pid == traced_pid[i]) {
                flag_hit = 1;
                break;
            }
        }
        if (flag_hit) VPMU.num_traced_threads--;
        if (flag_hit && VPMU.num_traced_threads == 0) {
            VPMU_sync();
            toc(&(VPMU.start_time), &(VPMU.end_time));
            VPMU_dump_result();
            memset(traced_pid, 0, sizeof(VPMU.traced_process_pid));
            VPMU.enabled = 0;
            flag_tracing = 0;
        }
    }

    // Linux Kernel: Context switch
    if (target_addr == VPMU.switch_to_addr) {
#if TARGET_LONG_BITS == 32
        uint32_t task_ptr = READ_INT_FROM_GUEST(cs, env->regs[2], 12);
#else
#pragma message("VPMU SET: 64 bits Not supported!!")
#endif
        // current_pid = READ_INT_FROM_GUEST(cs, task_ptr, 204); //This is kernel v3.6.11
        current_pid = READ_INT_FROM_GUEST(cs, task_ptr, 512); // This is kernel v4.4.0
        // ERR_MSG("pid = %x %d\n", env->regs[2], current_pid);

        // Switching VPMU when current process is traced
        if (likely(flag_tracing)) {
            int flag_hit = 0;

            // Skip pid 0 (init). 0 might be the initialization value of VPMU's pid
            if (unlikely(current_pid == 0)) {
                VPMU.enabled = 0;
                return;
            }
            // Loop through pid list to find if hit
            for (int i = 0; traced_pid[i] != 0; i++) {
                if (current_pid == traced_pid[i]) {
                    flag_hit = 1;
                    break;
                }
            }
            if (likely(flag_hit))
                VPMU.enabled = 1;
            else
                VPMU.enabled = 0;
        }
    }
#endif

    if (likely(VPMU.enabled)) {
        // CONSOLE_LOG("pc: %x->%x\n", return_addr, target_addr);
    }
}

// helper function to accumulate counters
void HELPER(vpmu_accumulate_tb_info)(CPUARMState *env, void *opaque)
{
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
#ifdef CONFIG_VPMU_DEBUG
                VPMU.modelsel.hot_tb_visit_count++;
#endif
                extra_tb_info->modelsel.hot_tb_flag = 1;
            } else {
#ifdef CONFIG_VPMU_DEBUG
                VPMU.modelsel.cold_tb_visit_count++;
#endif
                extra_tb_info->modelsel.hot_tb_flag = 0;
            }
            // Advance timestamp
            extra_tb_info->modelsel.last_visit = VPMU.modelsel.total_tb_visit_count;
            VPMU.modelsel.total_tb_visit_count++;
        } // End of VPMU_JIT_MODEL_SELECT

        if (vpmu_model_has(VPMU_INSN_COUNT_SIM, VPMU)) {
            vpmu_inst_ref(0, mode, extra_tb_info);
        } // End of VPMU_INSN_COUNT_SIM

        if (vpmu_model_has(VPMU_ICACHE_SIM, VPMU)) {
            if (extra_tb_info->modelsel.hot_tb_flag) {
                hot_cache_ref(PROCESSOR_CPU,
                              0,
                              extra_tb_info->start_addr,
                              CACHE_PACKET_INSTRN,
                              extra_tb_info->counters.size_bytes);
            } else {
                cache_ref(PROCESSOR_CPU,
                          0,
                          extra_tb_info->start_addr,
                          CACHE_PACKET_INSTRN,
                          extra_tb_info->counters.size_bytes);
            }
        } // End of VPMU_ICACHE_SIM

        if (vpmu_model_has(VPMU_PIPELINE_SIM, VPMU)) {
            VPMU.ticks += extra_tb_info->ticks;
        } // End of VPMU_PIPELINE_SIM

        if (vpmu_model_has(VPMU_BRANCH_SIM, VPMU)) {
            // Add global counter value of branch count.
            if (last_tb_has_branch) {
                if (extra_tb_info->start_addr - last_tb_pc <= 4) {
                    branch_ref(0, last_tb_pc, 0); // Not taken
                } else {
                    branch_ref(0, last_tb_pc, 1); // Taken
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
