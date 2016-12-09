#include "vpmu-cache.hpp"
#include "vpmu-packet.hpp"

CacheStream vpmu_cache_stream;

// Put your own timing simulator below
#include "simulators/dinero.hpp"
// Put you own timing simulator above
CacheStream::Sim_ptr CacheStream::create_sim(std::string sim_name)
{
    // Construct your timing model with ownership transfering
    // The return will use "move semantics" automatically.
    if (sim_name == "dinero")
        return std::make_unique<Cache_Dinero>();
    else
        return nullptr;
}

void cache_ref(
  uint8_t proc, uint8_t core, uint32_t addr, uint16_t type, uint16_t data_size)
{
    vpmu_cache_stream.send(proc, core, addr, type, data_size);
}
