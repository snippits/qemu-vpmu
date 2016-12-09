#include "vpmu.hpp"
#include "vpmu-inst.hpp"
#include "vpmu-cache.hpp"
#include "vpmu-branch.hpp"

extern "C" {

uint64_t vpmu_sys_mem_access_cycle_count(void)
{
    return vpmu_cache_stream.get_memory_cycles(0);
}

uint64_t vpmu_io_mem_access_cycle_count(void)
{
    return VPMU.iomem_count;
}

uint64_t vpmu_cycle_count(void)
{
    return VPMU.ticks + vpmu_cache_stream.get_total_cycles()
           + vpmu_io_mem_access_cycle_count();
}

uint64_t vpmu_estimated_sys_mem_time_ns(void)
{
    /* FIXME...
     * Use last_pipeline_cycle_count to save tmp data
     * as did above by using VPMU.last_ticks.
     */
    return vpmu_sys_mem_access_cycle_count() / (VPMU.cpu_model.frequency / 1000.0);
}

uint64_t vpmu_estimated_cpu_time_ns(void)
{
    /* FIXME...
     * Use last_pipeline_cycle_count to save tmp data
     * as did above by using VPMU.last_ticks.
     */
    return vpmu_cpu_cycle_count() / (VPMU.cpu_model.frequency / 1000.0);
}

uint64_t vpmu_estimated_io_mem_time_ns(void)
{
    /* FIXME...
     * Use last_pipeline_cycle_count to save tmp data
     * as did above by using VPMU.last_ticks.
     */
    return vpmu_io_mem_access_cycle_count() / (VPMU.cpu_model.frequency / 1000.0);
}

uint64_t vpmu_estimated_execution_time_ns(void)
{
    // TODO check if we really need to add idle time? Is it really correct?? Though it's
    // correct when running sleep in guest
    return vpmu_cycle_count() + VPMU.cpu_idle_time_ns;
}

uint64_t vpmu_wall_clock_period(void)
{
    return h_time_difference(&VPMU.start_time, &VPMU.end_time);
}

void vpmu_dump_readable_message(void)
{
// These two are for making info formatable and maintainable
#define CONSOLE_U64(str, val) CONSOLE_LOG(str " %'" PRIu64 "\n", (uint64_t)val)
#define CONSOLE_TME(str, val) CONSOLE_LOG(str " %'lf sec\n", (double)val / 1000000000.0)
    CONSOLE_LOG("Instructions:\n");
    vpmu_inst_stream.dump();
    CONSOLE_LOG("Branch:\n");
    vpmu_branch_stream.dump();
    CONSOLE_LOG("CACHE:\n");
    vpmu_cache_stream.dump();

    CONSOLE_LOG("\n");
    CONSOLE_LOG("Timing Info:\n");
    CONSOLE_TME("  ->CPU                        :", vpmu_estimated_cpu_time_ns());
    CONSOLE_TME("  ->Cache                      :",
                vpmu_cache_stream.get_cache_cycles(0)
                  / (VPMU.cpu_model.frequency / 1000.0));
    CONSOLE_TME("  ->System memory              :", vpmu_estimated_sys_mem_time_ns());
    CONSOLE_TME("  ->I/O memory                 :", vpmu_estimated_io_mem_time_ns());
    CONSOLE_TME("  ->Idle                       :", VPMU.cpu_idle_time_ns);
    CONSOLE_TME("Estimated execution time       :", vpmu_estimated_execution_time_ns());

    CONSOLE_LOG("\n");
    CONSOLE_TME("Emulation Time :", vpmu_wall_clock_period());
    CONSOLE_LOG("MIPS           : %'0.2lf\n\n",
                (double)vpmu_total_inst_count() / (vpmu_wall_clock_period() / 1000.0));

#if 0
    CONSOLE_LOG("Instructions:\n");
    CONSOLE_U64(" Total instruction count       :", vpmu_total_insn_count());
    CONSOLE_U64("  ->User mode insn count       :", vpmu->USR_icount);
    CONSOLE_U64("  ->Supervisor mode insn count :", vpmu->SVC_icount);
    CONSOLE_U64("  ->IRQ mode insn count        :", vpmu->IRQ_icount);
    CONSOLE_U64("  ->Other mode insn count      :", vpmu->sys_rest_icount);
    CONSOLE_U64(" Total ld/st instruction count :", vpmu_total_ldst_count());
    CONSOLE_U64("  ->User mode ld/st count      :", vpmu->USR_ldst_count);
    CONSOLE_U64("  ->Supervisor mode ld/st count:", vpmu->SVC_ldst_count);
    CONSOLE_U64("  ->IRQ mode ld/st count       :", vpmu->IRQ_ldst_count);
    CONSOLE_U64("  ->Other mode ld/st count     :", vpmu->sys_rest_ldst_count);
    CONSOLE_U64(" Total load instruction count  :", vpmu_total_load_count());
    CONSOLE_U64("  ->User mode load count       :", vpmu->USR_load_count);
    CONSOLE_U64("  ->Supervisor mode load count :", vpmu->SVC_load_count);
    CONSOLE_U64("  ->IRQ mode load count        :", vpmu->IRQ_load_count);
    CONSOLE_U64("  ->Other mode load count      :", vpmu->sys_rest_load_count);
    CONSOLE_U64(" Total store instruction count :", vpmu_total_store_count());
    CONSOLE_U64("  ->User mode store count      :", vpmu->USR_store_count);
    CONSOLE_U64("  ->Supervisor mode store count:", vpmu->SVC_store_count);
    CONSOLE_U64("  ->IRQ mode store count       :", vpmu->IRQ_store_count);
    CONSOLE_U64("  ->Other mode store count     :", vpmu->sys_rest_store_count);
    CONSOLE_LOG("Memory:\n");
    CONSOLE_U64("  ->System memory access       :", (vpmu_L1_dcache_miss_count()
                                                 + vpmu_L1_icache_miss_count()));
    CONSOLE_U64("  ->System memory cycles       :", vpmu_sys_mem_access_cycle_count());
    CONSOLE_U64("  ->I/O memory access          :", vpmu->iomem_count);
    CONSOLE_U64("  ->I/O memory cycles          :", vpmu_io_mem_access_cycle_count());
    CONSOLE_U64("Total Cycle count              :", vpmu_cycle_count());
    //Remember add these infos into L1I READ
    CONSOLE_LOG("Model Selection:\n");
    CONSOLE_U64("  ->JIT icache access          :", (vpmu->hot_icache_count));
    CONSOLE_U64("  ->JIT dcache access          :", (vpmu->hot_dcache_read_count + vpmu->hot_dcache_write_count));
    CONSOLE_U64("  ->VPMU icache access         :", vpmu_L1_icache_access_count());
    CONSOLE_U64("  ->VPMU icache misses         :", vpmu_L1_icache_miss_count());
    CONSOLE_U64("  ->VPMU dcache access         :", vpmu_L1_dcache_access_count());
    CONSOLE_U64("  ->VPMU dcache read misses    :", vpmu_L1_dcache_read_miss_count());
    CONSOLE_U64("  ->VPMU dcache write misses   :", vpmu_L1_dcache_write_miss_count());
    CONSOLE_U64("  ->hotTB                      :", VPMU.hot_tb_visit_count);
    CONSOLE_U64("  ->coldTB                     :", VPMU.cold_tb_visit_count);
    */
#endif
#undef CONSOLE_TME
#undef CONSOLE_U64
}
}
