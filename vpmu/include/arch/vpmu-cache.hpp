#ifndef __VPMU_CACHE_HPP_
#define __VPMU_CACHE_HPP_
extern "C" {
#include "vpmu-cache.h"
}
#include "vpmu.hpp"        // VPMU common header
#include "vpmu-stream.hpp" // VPMUStream, VPMUStream_T
#include "vpmu-packet.hpp" // VPMU_Inst, VPMU_Branch, VPMU_Cache
#include "json.hpp"        // nlohmann::json
// The implementaion of stream buffer and multi- threading/processing
#include "stream_impl/single-thread.hpp" // VPMU_Stream_Single_Thread
#include "stream_impl/multi-thread.hpp"  // VPMUStreamMultiThread
#include "stream_impl/multi-process.hpp" // VPMU_Stream_Multi_Process

class CacheStream : public VPMUStream_T<VPMU_Cache>
{
public:
    CacheStream() : VPMUStream_T<VPMU_Cache>("CACHE") { log_debug("Constructed"); }
    CacheStream(const char* module_name) : VPMUStream_T<VPMU_Cache>(module_name) {}
    CacheStream(std::string module_name) : VPMUStream_T<VPMU_Cache>(module_name) {}

    void set_stream_impl(void) override
    {
        // Get the default implementation of stream interface.
        impl = std::make_unique<VPMUStreamMultiProcess<VPMU_Cache>>("C_Strm");
        // impl = std::make_unique<VPMUStreamMultiThread<VPMU_Cache>>("C_Strm");
        // impl = std::make_unique<VPMUStreamSingleThread<VPMU_Cache>>("C_Strm");
        // Construct the channel (buffer) and allocate resources
        impl->build(1024 * 64);
    }

    void send(uint8_t proc, uint8_t core, uint32_t addr, uint16_t type, uint16_t size);

    uint64_t get_cache_cycles(int n)
    {
        VPMU_Cache::Model model  = get_model(n);
        VPMU_Cache::Data  data   = get_data(n);
        uint64_t          cycles = 0;

        for (int level = L1_CACHE; level < MAX_LEVEL_OF_CACHE; level++) {
            uint64_t miss_cnt = 0, hit_cnt = 0;
            for (int core = 0; core < VPMU_MAX_CPU_CORES; core++) {
                auto& cache = data.inst_cache[level][core];
                miss_cnt += cache[VPMU_Cache::CACHE_READ_MISS];
                hit_cnt += cache[VPMU_Cache::CACHE_READ] - miss_cnt;
            }
            cycles += model.latency[level] * miss_cnt + 1 * hit_cnt;

            miss_cnt = hit_cnt = 0;
            for (int core = 0; core < VPMU_MAX_CPU_CORES; core++) {
                auto& cache = data.data_cache[level][core];
                miss_cnt += cache[VPMU_Cache::CACHE_READ_MISS]
                            + cache[VPMU_Cache::CACHE_WRITE_MISS];
                hit_cnt += cache[VPMU_Cache::CACHE_READ] + cache[VPMU_Cache::CACHE_WRITE]
                           - miss_cnt;
            }
            cycles += model.latency[level] * miss_cnt + 1 * hit_cnt;
        }

        return cycles;
    }

    uint64_t get_memory_cycles(int n)
    {
        VPMU_Cache::Model model  = get_model(n);
        VPMU_Cache::Data  data   = get_data(n);
        uint64_t          cycles = 0;

        cycles += data.memory_accesses * model.latency[VPMU_Cache::Data_Level::MEMORY];
        return cycles;
    }

    uint64_t get_total_cycles(int n)
    {
        return get_memory_cycles(n) + get_cache_cycles(n);
    }

    uint64_t get_total_cycles() { return get_total_cycles(0); }

private:
    // This is a register function declared in the vpmu-cache.cc file.
    Sim_ptr create_sim(std::string sim_name) override;
};

extern CacheStream vpmu_cache_stream;
#endif
