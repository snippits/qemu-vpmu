#ifndef __VPMU_QEMU_H_
#define __VPMU_QEMU_H_
#include <time.h>

#include "arch/vpmu-inst.h"
#include "arch/vpmu-cache.h"
#include "arch/vpmu-branch.h"

typedef struct VPMUPlatformInfo {
    struct {
        uint32_t cores;
        // uint64_t frequency;
    } cpu, gpu;
} VPMUPlatformInfo;

typedef struct VPMU_Struct {
    // A flag that indicate whether VPMU is enabled
    int8_t enabled;
    int8_t all_cpu_idle_flag;
    int8_t iomem_access_flag;
    int8_t swi_fired_flag;
    /* for timer interrupt */
    uint64_t timer_interrupt_return_pc;

    struct timespec start_time, end_time;

    uint64_t cpu_idle_time_ns;
    uint64_t ticks;

    uint64_t iomem_count;

    uint64_t timing_model;

    /* TODO move to anotehr place Modelsel*/
    uint64_t         total_tb_visit_count;
    uint64_t         cold_tb_visit_count;
    uint64_t         hot_tb_visit_count;
    uint64_t         hot_dcache_read_count;
    uint64_t         hot_dcache_write_count;
    uint64_t         hot_icache_count;
    VPMUPlatformInfo platform;
    /* Configurations VPMU needs to know */
    // TODO remove all these
    VPMU_Inst_Model   cpu_model;
    VPMU_Cache_Model  cache_model;
    VPMU_Branch_Model branch_model;
} VPMU_Struct;

// A structure to extend TB info for accumulating counters when executing each TB.
typedef struct ExtraTBInfo {
    Inst_Counters counters;
    uint8_t       has_branch;
    uint16_t      ticks;
    uint64_t      start_addr;

    // Modelsel
    struct {
        uint8_t  hot_ref_counter;
        uint32_t last_visit;
        uint32_t cold_hot_boundary;
        double   hot_icache_miss_rate;
        uint16_t num_of_cacheblks;
    } modelsel;
} ExtraTBInfo;

// A structure storing VPMU configuration
extern struct VPMU_Struct VPMU;

void VPMU_init(int argc, char **argv);
uint64_t vpmu_cycle_count(void);
uint64_t vpmu_estimated_pipeline_time_ns(void);
uint64_t vpmu_estimated_sys_mem_time_ns(void);
uint64_t vpmu_estimated_io_mem_time_ns(void);
uint64_t vpmu_estimated_execution_time_ns(void);
void     vpmu_dump_readable_message(void);

inline uint64_t h_time_difference(struct timespec *t1, struct timespec *t2)
{
    uint64_t period = 0;

    period = t2->tv_nsec - t1->tv_nsec;
    period += (t2->tv_sec - t1->tv_sec) * 1000000000;

    return period;
}

inline void tic(struct timespec *t1)
{
    clock_gettime(CLOCK_REALTIME, t1);
}

inline uint64_t toc(struct timespec *t1, struct timespec *t2)
{
    clock_gettime(CLOCK_REALTIME, t2);
    return h_time_difference(t1, t2);
}

void vpmu_inst_ref(uint8_t core, uint8_t mode, ExtraTBInfo *ptr);
void VPMU_sync(void);
void VPMU_sync_non_blocking(void);
void VPMU_reset(void);
void VPMU_dump_result(void);
void vpmu_simulator_status(VPMU_Struct *vpmu);
void model_sel_ref(uint8_t      proc,
                   uint8_t      core_id,
                   uint32_t     addr,
                   uint16_t     accesstype,
                   uint16_t     size,
                   ExtraTBInfo *tb_info);

#endif
