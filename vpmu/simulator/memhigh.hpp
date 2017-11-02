#ifndef __CACHE_MEMHIGH_HPP_
#define __CACHE_MEMHIGH_HPP_
#pragma once

#include <string>                   // std::string
#include "vpmu-sim.hpp"             // VPMUSimulator
#include "vpmu-cache-packet.hpp"    // VPMU_Cache
#include "vpmu-utils.hpp"           // miscellaneous functions
#include "vpmu-template-output.hpp" // Template output format

// TODO refactor this code
//
using nlohmann::json;
class Cache_MemHigh : public VPMUSimulator<VPMU_Cache>
{
public:
    Cache_MemHigh() : VPMUSimulator("MemHigh") {}
    ~Cache_MemHigh() {}

    void destroy() override {}

    VPMU_Cache::Model build(void) override
    {
        log_debug("Initializing");

        log_debug(json_config.dump().c_str());
        // TODO This is a dummy model which should be parsed from json_config
        auto model_name = vpmu::utils::get_json<std::string>(json_config, "name");
        strncpy(cache_model.name, model_name.c_str(), sizeof(cache_model.name));
        cache_model.levels                                                  = 1;
        cache_model.latency[VPMU_Cache::Data_Level::MEMORY]                 = 1;
        cache_model.latency[VPMU_Cache::Data_Level::L1_CACHE]               = 1;
        cache_model.d_log2_blocksize[VPMU_Cache::Data_Level::L1_CACHE]      = 5;
        cache_model.d_log2_blocksize_mask[VPMU_Cache::Data_Level::L1_CACHE] = 31;
        cache_model.i_log2_blocksize[VPMU_Cache::Data_Level::L1_CACHE]      = 5;
        cache_model.i_log2_blocksize_mask[VPMU_Cache::Data_Level::L1_CACHE] = 31;
        cache_model.d_write_alloc[VPMU_Cache::Data_Level::L1_CACHE]         = false;
        cache_model.d_write_back[VPMU_Cache::Data_Level::L1_CACHE]          = false;

        log_debug("Initialized");
        return cache_model;
    }

    RetStatus packet_processor(int id, const VPMU_Cache::Reference &ref) override
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
        case VPMU_PACKET_SYNC_DATA:
            return {};
            break;
        case VPMU_PACKET_DUMP_INFO:
            CONSOLE_LOG("  [%d] type : MemHigh\n", id);

            break;
        case VPMU_PACKET_RESET:
            load_count  = 0;
            store_count = 0;
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

        return true;
    }

private:
    VPMU_Cache::Model cache_model = {};
#ifdef CONFIG_VPMU_DEBUG_MSG
    // The total number of packets counter for debugging
    uint64_t debug_packet_num_cnt = 0;
#endif
    uint64_t load_count  = 0;
    uint64_t store_count = 0;

    // The CPU configurations for timing model
    using VPMUSimulator::platform_info;
};

#endif
