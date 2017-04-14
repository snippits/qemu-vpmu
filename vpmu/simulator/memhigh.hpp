#ifndef __CACHE_MEMHIGH_HPP__
#define __CACHE_MEMHIGH_HPP__
#include <string>          // std::string
#include "vpmu-sim.hpp"    // VPMUSimulator
#include "vpmu-packet.hpp" // VPMU_Cache::Reference
#include "vpmu-utils.hpp"  // miscellaneous functions

// TODO refactor this code
//
using nlohmann::json;
class Cache_MemHigh : public VPMUSimulator<VPMU_Cache>
{

    void dump_info(int id)
    {
        CONSOLE_LOG("  [%d] type : memhigh\n", id);
        // Dump info
        CONSOLE_LOG("      Statics        "
                    "|    Load Count      "
                    "|   Store Count      "
                    "|\n");
        // Memory
        //CONSOLE_LOG("|--------------------|--------------------|--------------------|\n");
        CONSOLE_LOG("    -> L1-D          |%'20" PRIu64 "|%'20" PRIu64 "|\n",
                    (uint64_t)load_count,
                    (uint64_t)store_count);
        CONSOLE_LOG("    -> L1-I          |%'20" PRIu64 "|%'20" PRIu64 "|\n",
                    (uint64_t)0,
                    (uint64_t)0);
        
        /*
        CONSOLE_LOG("    -> memory (%0.2lf) |%'20" PRIu64 "|%'20" PRIu64 "|%'20" PRIu64
                    "|\n",
                    (double)0.0,
                    (uint64_t)(d.fetch_read),
                    (uint64_t)0,
                    (uint64_t)0);
        */
    }

    
public:
    Cache_MemHigh() : VPMUSimulator("MemHigh") {}
    ~Cache_MemHigh() {}

    void destroy() override
    {
            }

    void build(VPMU_Cache::Model &model) override
    {
        log_debug("Initializing");

        log_debug(json_config.dump().c_str());
        
        log_debug("Initialized");
    }

    inline void packet_processor(int                          id,
                                 const VPMU_Cache::Reference &ref,
                                 VPMU_Cache::Data &           data) override
    {

#ifdef CONFIG_VPMU_DEBUG_MSG
        debug_packet_num_cnt++;
        if (ref.type == VPMU_PACKET_DUMP_INFO) {
            CONSOLE_LOG("    %'" PRIu64 " packets received\n", debug_packet_num_cnt);
            debug_packet_num_cnt = 0;
        }
#endif
       switch (ref.type) {
        case VPMU_PACKET_BARRIER:
            break;
        case VPMU_PACKET_SYNC_DATA:
            break;
        case VPMU_PACKET_DUMP_INFO:
            dump_info(id);
            load_count=0;
            store_count=0;
            break;
        case VPMU_PACKET_RESET:
            load_count=0;
            store_count=0;
            break;
        case CACHE_PACKET_READ:
            load_count++;
            break;
        case CACHE_PACKET_WRITE:
            store_count++;
            break;
        case CACHE_PACKET_INSN:
            break;
        default:
            ERR_MSG("Unexpected packet in cache simulators\n");
        }
    }


private:
#ifdef CONFIG_VPMU_DEBUG_MSG
    // The total number of packets counter for debugging
    uint64_t debug_packet_num_cnt = 0;
#endif
    uint64_t load_count = 0;
    uint64_t store_count = 0;
    // The CPU configurations for timing model
    using VPMUSimulator::platform_info;
};

#endif
