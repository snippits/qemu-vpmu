#include "vpmu-cache.hpp" // CacheStream

// Define the global instance here for accessing
CacheStream vpmu_cache_stream;

// Put your own timing simulator below
#include "simulator/dinero.hpp"
#include "simulator/memhigh.hpp"

// Put you own timing simulator above
CacheStream::Sim_ptr CacheStream::create_sim(std::string sim_name)
{
    // Construct your timing model with ownership transfering
    // The return will use "move semantics" automatically.
    if (sim_name == "dinero")
        return std::make_unique<Cache_Dinero>();
    else if (sim_name == "memhigh")
        return std::make_unique<Cache_MemHigh>();
    else
        return nullptr;
}

// TODO:implement L2 Lazy $.
// TODO:implement non-write-allocation, write back, write through cases.
bool CacheStream::data_possibly_hit(uint64_t addr, uint32_t rw)
{
    // pc for dcache sim
    static uint32_t block_addr_start[4] = {
      0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
    static uint8_t    counter     = 0;
    VPMU_Cache::Model cache_model = get_model(0);

    int mask             = cache_model.i_log2_blocksize_mask[VPMU_Cache::L1_CACHE];
    int write_alloc_flag = cache_model.d_write_alloc[VPMU_Cache::L1_CACHE];
    addr &= mask;
    if ((block_addr_start[0] == addr) || (block_addr_start[1] == addr)
        || (block_addr_start[2] == addr)
        || (block_addr_start[3] == addr)) { // hot data access
        return true;
    } else { // cold data access
        // classify cases for write-allocation
        if (rw == CACHE_PACKET_READ || write_alloc_flag) {
            block_addr_start[counter++] = (addr & mask);
            counter &= 3;
        }
        return false;
    }
}

void CacheStream::send(
  uint8_t proc, uint8_t core, uint64_t addr, uint16_t type, uint16_t size)
{
    VPMU_Cache::Reference r;
    r.type      = type; // The type of reference
    r.processor = proc; // The address of pc
    r.core      = core; // The number of CPU core
    r.addr      = addr; // The virtual address of ld/st request
    r.size      = size; // If this is a taken branch

    // Use after CPU cores when it's in GPU core
    if (proc == PROCESSOR_GPU) core += VPMU_MAX_CPU_CORES;
    send_ref(core, r);
}

void CacheStream::send_hot_tb(
  uint8_t proc, uint8_t core, uint64_t addr, uint16_t type, uint16_t size)
{
    if (type == CACHE_PACKET_INSN) {
        VPMU_Cache::Model cache_model = get_model(0);

        uint64_t bs       = cache_model.i_log2_blocksize[VPMU_Cache::L1_CACHE];
        uint64_t block_s  = addr >> bs;
        uint64_t block_e  = ((addr + size) - 1) >> bs;
        int      num_blks = block_e - block_s + 1;
        VPMU.modelsel[core].hot_icache_count += num_blks;
    } else {
        if (data_possibly_hit(addr, type)) {
            if (type == CACHE_PACKET_WRITE) {
                VPMU.modelsel[core].hot_dcache_write_count++;
            } else {
                VPMU.modelsel[core].hot_dcache_read_count++;
            }
        } else {
            // Fallback to normal simulation
            cache_ref(proc, core, addr, type, size);
        }
    }
}

void cache_ref(
  uint8_t proc, uint8_t core, uint64_t addr, uint16_t type, uint16_t data_size)
{
    vpmu_cache_stream.send(proc, core, addr, type, data_size);
}

void hot_cache_ref(
  uint8_t proc, uint8_t core, uint64_t addr, uint16_t type, uint16_t data_size)
{
    vpmu_cache_stream.send_hot_tb(proc, core, addr, type, data_size);
}
