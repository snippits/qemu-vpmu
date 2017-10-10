#ifndef __BRANCH_TWO_BITS_HPP_
#define __BRANCH_TWO_BITS_HPP_
#pragma once

#include "vpmu-sim.hpp"             // VPMUSimulator
#include "vpmu-branch-packet.hpp"   // VPMU_Branch
#include "vpmu-template-output.hpp" // Template output format

class Branch_Two_Bits : public VPMUSimulator<VPMU_Branch>
{
public:
    Branch_Two_Bits() : VPMUSimulator("Two Bits") {}
    ~Branch_Two_Bits() {}

    void destroy() override { ; } // Nothing to do

    void build(VPMU_Branch::Model& model) override
    {
        log_debug("Initializing");

        log_debug(json_config.dump().c_str());
        auto model_name = vpmu::utils::get_json<std::string>(json_config, "name");
        strncpy(branch_model.name, model_name.c_str(), sizeof(branch_model.name));
        branch_model.latency = vpmu::utils::get_json<int>(json_config, "miss latency");

        model = branch_model;
        log_debug("Initialized");
    }

    void packet_processor(int                           id,
                          const VPMU_Branch::Reference& ref,
                          VPMU_Branch::Data&            data) override
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
            data = branch_data;
            break;
        case VPMU_PACKET_DUMP_INFO:
            CONSOLE_LOG("  [%d] type : Two Bits Predictor\n", id);
            vpmu::output::Branch_counters(branch_model, branch_data);

            break;
        case VPMU_PACKET_RESET:
            branch_data = {}; // Zero initializer
            break;
        case VPMU_PACKET_DATA:
            two_bits_branch_predictor(ref);
            break;
        default:
            LOG_FATAL("Unexpected packet");
        }
    }

private:
#ifdef CONFIG_VPMU_DEBUG_MSG
    // The total number of packets counter for debugging
    uint64_t debug_packet_num_cnt = 0;
#endif
    // predictor (the states of branch predictors)
    uint64_t           predictor[VPMU_MAX_CPU_CORES] = {};
    VPMU_Branch::Data  branch_data                   = {};
    VPMU_Branch::Model branch_model                  = {};
    // The CPU configurations for timing model
    using VPMUSimulator::platform_info;

    void two_bits_branch_predictor(const VPMU_Branch::Reference& ref)
    {
        int       taken = ref.taken;
        int       core  = ref.core;
        uint64_t* entry = &predictor[core];

        bool flag = ((*entry >= 2) && (taken != 0)) || ((*entry < 2) && (taken == 0));

        // Update predictor
        if (taken) {
            if (*entry < 3) *entry += 1;
        } else {
            if (*entry > 0) *entry -= 1;
        }

        // Update branch_data
        if (flag) {
            branch_data.correct[core]++;
        } else {
            branch_data.wrong[core]++;
        }
    }
};

#endif
