#ifndef __VPMU_QEMU_H_
#define __VPMU_QEMU_H_

#include "vpmu-common.h" // Common headers and macros

enum VPMU_CPU_MODE { VPMU_CPU_MODE_ARM, VPMU_CPU_MODE_THUMB };

typedef struct VPMUPlatformInfo {
    struct {
        uint32_t cores;
        uint64_t frequency;
    } cpu, gpu;
    bool kvm_enabled;
} VPMUPlatformInfo;

typedef struct VPMU_Struct {
    // The path for outputing files and logs
    char output_path[1024];
    // A flag that indicate whether VPMU is enabled
    bool enabled;
    bool all_cpu_idle_flag;
    bool iomem_access_flag;
    bool swi_fired_flag;
    /* for timer interrupt */
    uint64_t timer_interrupt_return_pc;

    struct timespec start_time, end_time;
    struct timespec program_start_time;

    uint64_t cpu_idle_time_ns;
    uint64_t ticks;

    uint64_t iomem_count;

    uint64_t timing_model;

    // TODO Is there a better way for multi-core execution env?
    // Should be set for vpmu device only.
    void *cpu_arch_state; // This is for identifying MMU table

    struct {
        uint64_t total_tb_visit_count;
        uint64_t cold_tb_visit_count;
        uint64_t hot_tb_visit_count;
        uint64_t hot_dcache_read_count;
        uint64_t hot_dcache_write_count;
        uint64_t hot_icache_count;
    } modelsel;
    VPMUPlatformInfo platform;
} VPMU_Struct;

// A structure storing VPMU configuration
extern struct VPMU_Struct VPMU;

// Prevent prototype warnings from some compilers
uint64_t h_time_difference(struct timespec *t1, struct timespec *t2);
void tic(struct timespec *t1);
uint64_t toc(struct timespec *t1, struct timespec *t2);
uint64_t vpmu_get_timestamp_us(void);

void VPMU_init(int argc, char **argv);
void VPMU_reset(void);
void VPMU_sync(void);
void VPMU_sync_non_blocking(void);
void VPMU_dump_result(void);
void vpmu_dump_readable_message(void);
void vpmu_print_status(VPMU_Struct *vpmu);
uint64_t vpmu_target_time_ns(void);

#endif
