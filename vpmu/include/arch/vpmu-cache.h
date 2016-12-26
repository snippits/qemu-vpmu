#ifndef __VPMU_CACHE_H_
#define __VPMU_CACHE_H_

#include "../vpmu-conf.h"   // VPMU_MAX_CPU_CORES
#include "../vpmu-common.h" // Include common headers

enum Cache_Data_Index {
    CACHE_READ,
    CACHE_WRITE,
    CACHE_READ_MISS,
    CACHE_WRITE_MISS,
    SIZE_OF_CACHE_INDEX
};

enum Cache_Data_Level {
    NOT_USED,
    L1_CACHE,
    L2_CACHE,
    L3_CACHE,
    MEMORY,
    MAX_LEVEL_OF_CACHE
};

// The data/states of each simulators
typedef struct Cache_Data {
    //[level][core][r/w miss/hit]
    uint64_t inst_cache[MAX_LEVEL_OF_CACHE][VPMU_MAX_CPU_CORES][SIZE_OF_CACHE_INDEX];
    uint64_t data_cache[MAX_LEVEL_OF_CACHE][VPMU_MAX_CPU_CORES][SIZE_OF_CACHE_INDEX];
} VPMU_Cache_Data;

typedef struct VPMU_Cache_Model {
    char name[64];
    // number of layers this cache configuration has
    int levels;
    // cache latency information
    int latency[MAX_LEVEL_OF_CACHE];
    // cache block size information
    int d_log2_blocksize[MAX_LEVEL_OF_CACHE];
    int d_log2_blocksize_mask[MAX_LEVEL_OF_CACHE];
    // cache block size information
    int i_log2_blocksize[MAX_LEVEL_OF_CACHE];
    int i_log2_blocksize_mask[MAX_LEVEL_OF_CACHE];
    // data cache: true -> write allocate; false -> non write allocate
    int d_write_alloc[MAX_LEVEL_OF_CACHE];
    // data cache: true -> write back; false -> write through
    int d_write_back[MAX_LEVEL_OF_CACHE];
} VPMU_Cache_Model;

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
