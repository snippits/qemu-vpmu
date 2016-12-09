#include "vpmu.hpp"
#include "vpmu-packet.hpp"

#define MAINTAIN_ICACHE_PATTERN_CYCLE 10          // Acctually 9+1 cycles
#define CYCLE (MAINTAIN_ICACHE_PATTERN_CYCLE - 1) // Acctually 9+1 cycles
#define QUANTUM 300                               // Unit of boundry expansion
#define BITMASK(n) (((1) << (n)) - 1)             // An n-bit mask

enum MODELSEL_METHOD { EXACT, SPEED };

// TODO:implement L2 Lazy $.
// TODO:implement non-write-allocation, write back, write through cases.
static inline uint8_t analyze_dataxs(uint32_t addr, uint16_t rw)
{
    // pc for dcache sim
    static uint32_t block_addr_start[4] = {
      0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
    static uint8_t counter = 0;

    addr &= VPMU.cache_model.i_log2_blocksize_mask[1];
    if ((block_addr_start[0] == addr) || (block_addr_start[1] == addr)
        || (block_addr_start[2] == addr)
        || (block_addr_start[3] == addr)) { // hot data access
        return SPEED;
    } else { // cold data access
        // classify cases for write-allocation
        if (rw == CACHE_PACKET_READ || VPMU.cache_model.d_write_alloc[L1_CACHE]) {
            block_addr_start[counter++] =
              (addr & VPMU.cache_model.i_log2_blocksize_mask[1]);
            counter &= 3;
        }
        return EXACT;
    }
}

static inline void
dcache_model_sel(uint8_t proc, uint8_t core_id, uint32_t addr, uint16_t rw, uint16_t size)
{
    switch (analyze_dataxs(addr, rw)) {
    case EXACT:
        cache_ref(proc, core_id, addr, rw, size);
        break;
    case SPEED:
        if (rw == CACHE_PACKET_WRITE) {
            VPMU.hot_dcache_write_count++;
        } else {
            VPMU.hot_dcache_read_count++;
        }
        break;
    }
}

static inline void icache_model_sel(uint8_t proc, uint8_t core_id, ExtraTBInfo* tb_info)
{
    int distance = VPMU.total_tb_visit_count - tb_info->modelsel.last_visit;

    VPMU.total_tb_visit_count++;
    if (0 < distance && distance < 100) {
        VPMU.hot_icache_count += tb_info->modelsel.num_of_cacheblks;
        tb_info->modelsel.last_visit++;

#ifdef CONFIG_VPMU_DEBUG
        VPMU.hot_tb_visit_count++;
#endif
    } else {
        tb_info->modelsel.last_visit = VPMU.total_tb_visit_count;
        cache_ref(PROCESSOR_CPU,
                  0,
                  tb_info->start_addr,
                  CACHE_PACKET_INSTRN,
                  tb_info->counters.size_bytes);

#ifdef CONFIG_VPMU_DEBUG
        VPMU.cold_tb_visit_count++;
#endif
    }

    // static unsigned int previous_pc = 0;

    // if (previous_pc == tb_info->start_addr) {
    //    VPMU.hot_icache_count += tb_info->modelsel.num_of_cacheblks;
    // } else {
    //    cache_ref(
    //      proc, core_id, tb_info->start_addr, CACHE_PACKET_INSTRN, tb_info->size_insns);
    // }
}

void model_sel_ref(uint8_t      proc,
                   uint8_t      core_id,
                   uint32_t     addr,
                   uint16_t     accesstype,
                   uint16_t     size,
                   ExtraTBInfo* tb_info)
{
    if (accesstype == CACHE_PACKET_INSTRN)
        icache_model_sel(proc, core_id, tb_info);
    else
        dcache_model_sel(proc, core_id, addr, accesstype, size);
}
