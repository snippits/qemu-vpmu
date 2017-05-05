#ifndef __VPMU_QEMU_H_
#define __VPMU_QEMU_H_

#include "../vpmu-common.h" // Common headers and macros
#include "../vpmu-conf.h"   // Common definitions of macros

enum VPMU_CPU_MODE { VPMU_CPU_MODE_ARM, VPMU_CPU_MODE_THUMB };

typedef struct VPMUPlatformInfo {
    struct {
        uint32_t cores;
        uint64_t frequency;
    } cpu, gpu;
    bool     kvm_enabled;
    uint64_t linux_version;
} VPMUPlatformInfo;

typedef struct VPMU_Struct {
    // The path for outputing files and logs
    char output_path[1024];
    // A flag that indicate whether VPMU is enabled
    bool enabled;
    bool all_cpu_idle_flag;
    bool iomem_access_flag;
    bool swi_fired_flag;
    bool threaded_tcg_flag;
    /* for timer interrupt */
    uint64_t timer_interrupt_return_pc;

    struct timespec start_time, end_time;
    struct timespec program_start_time;

    uint64_t cpu_idle_time_ns;
    uint64_t ticks;

    uint64_t iomem_count;

    uint64_t timing_model;

    struct {
        // The per core enable flag is used to indicate whether there is
        // any core running monitored process. We still use VPMU.enabled
        // to decide whether run VPMU codes for performance counters.
        bool     vpmu_enabled;   // Indicate whether VPMU is enabled on this core
        void *   cpu_arch_state; // This is for identifying MMU table
        uint64_t current_pid;    // Current pid on the core
        uint64_t padding[8];     // 8 words of padding
    } core[VPMU_MAX_CPU_CORES];

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

// These two are thread local values which could be used in multi-threaded tcg
uint64_t vpmu_get_core_id(void);
void     vpmu_set_core_id(uint64_t);

#endif
