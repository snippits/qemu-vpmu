#ifndef __VPMU_CACHE_H_
#define __VPMU_CACHE_H_

#include "../vpmu-conf.h"   // VPMU_MAX_CPU_CORES
#include "../vpmu-common.h" // Include common headers

void cache_ref(
  uint8_t proc, uint8_t core, uint64_t addr, uint16_t type, uint16_t data_size);
void hot_cache_ref(
  uint8_t proc, uint8_t core, uint64_t addr, uint16_t type, uint16_t data_size);

uint64_t vpmu_sys_mem_access_cycle_count(void);
uint64_t vpmu_io_mem_access_cycle_count(void);
uint64_t vpmu_L1_dcache_access_count(void);
uint64_t vpmu_L1_dcache_miss_count(void);
uint64_t vpmu_L1_dcache_read_miss_count(void);
uint64_t vpmu_L1_dcache_write_miss_count(void);
uint64_t vpmu_L2_dcache_access_count(void);
uint64_t vpmu_L2_dcache_miss_count(void);
uint64_t vpmu_L2_dcache_read_miss_count(void);
uint64_t vpmu_L2_dcache_write_miss_count(void);

uint64_t vpmu_L1_icache_access_count(void);
uint64_t vpmu_L1_icache_miss_count(void);

#endif
