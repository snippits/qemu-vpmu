#ifndef __BRANCH_GHT_HPP_
#define __BRANCH_GHT_HPP_
#pragma once

#include "vpmu-sim.hpp"             // VPMUSimulator
#include "vpmu-branch-packet.hpp"   // VPMU_Branch
#include "vpmu-template-output.hpp" // Template output format

class Branch_GHT : public VPMUSimulator<VPMU_Branch>
{
public:
    Branch_GHT() : VPMUSimulator("GHT") {}
    ~Branch_GHT() {}

    void destroy() override { ; } // Nothing to do

    VPMU_Branch::Model build(void) override
    {
        log_debug("Initializing");

        log_debug(json_config.dump().c_str());
        auto model_name = vpmu::utils::get_json<std::string>(json_config, "name");
        strncpy(branch_model.name, model_name.c_str(), sizeof(branch_model.name));
        branch_model.latency = vpmu::utils::get_json<int>(json_config, "miss latency");
        entry_size = json_config.value("entry size", 256);

        for (int i = 0; i < VPMU_MAX_CPU_CORES; i++) {
            predictor[i].resize(entry_size);
        }

        log_debug("Initialized");
        return branch_model;
    }

    RetStatus packet_processor(int id, const VPMU_Branch::Reference& ref) override
    {
#ifdef CONFIG_VPMU_DEBUG_MSG
        debug_packet_num_cnt++;
        if (ref.type == VPMU_PACKET_DUMP_INFO) {
            CONSOLE_LOG("    %'" PRIu64 " packets received\n", debug_packet_num_cnt);
            debug_packet_num_cnt = 0;
        }
#endif

        // Every simulators should handle VPMU_BARRIER_PACKET to support synchronization
        // The implementation depends on your own packet type and writing style
        switch (ref.type) {
        case VPMU_PACKET_BARRIER:
        case VPMU_PACKET_SYNC_DATA:
            return branch_data;
            break;
        case VPMU_PACKET_DUMP_INFO:
            CONSOLE_LOG("  [%d] type : Global History Table Predictor (%d-Entry)\n",
                        id,
                        entry_size);
            vpmu::output::Branch_counters(branch_model, branch_data);

            break;
        case VPMU_PACKET_RESET:
            branch_data = {}; // Zero initializer
            break;
        case VPMU_PACKET_DATA:
            ght_branch_predictor(ref);
            break;
        default:
            LOG_FATAL("Unexpected packet");
        }

        return branch_data;
    }

private:
#ifdef CONFIG_VPMU_DEBUG_MSG
    // The total number of packets counter for debugging
    uint64_t debug_packet_num_cnt = 0;
#endif
    int entry_size = 0;
    // predictor (the states of branch predictors)
    std::vector<uint8_t> predictor[VPMU_MAX_CPU_CORES] = {};
    uint64_t             history[VPMU_MAX_CPU_CORES]   = {};
    VPMU_Branch::Data    branch_data                   = {};
    VPMU_Branch::Model   branch_model                  = {};
    // The CPU configurations for timing model
    using VPMUSimulator::platform_info;

    void ght_branch_predictor(const VPMU_Branch::Reference& ref)
    {
        int taken       = ref.taken;
        int core        = ref.core;
        int entry_index = history[core] & (entry_size - 1);

        if (two_bits_predictor(&predictor[core][entry_index], taken)) {
            branch_data.correct[core]++;
        } else {
            branch_data.wrong[core]++;
        }
        history[core] = (history[core] << 1) | (taken == true);
    }

    bool two_bits_predictor(uint8_t* entry, int taken)
    {
        bool flag = ((*entry >= 2) && (taken != 0)) || ((*entry < 2) && (taken == 0));

        if (taken) {
            if (*entry < 3) *entry += 1;
        } else {
            if (*entry > 0) *entry -= 1;
        }

        return flag;
    }
};

#endif
