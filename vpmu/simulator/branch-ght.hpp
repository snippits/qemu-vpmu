#ifndef __BRANCH_GHT_HPP__
#define __BRANCH_GHT_HPP__
#include "vpmu-sim.hpp"    // VPMUSimulator
#include "vpmu-packet.hpp" // VPMU_Branch::Reference

class Branch_GHT : public VPMUSimulator<VPMU_Branch>
{
public:
    Branch_GHT() : VPMUSimulator("GHT") {}
    ~Branch_GHT() {}

    void destroy() override { ; } // Nothing to do

    void build(VPMU_Branch::Model& model) override
    {
        log_debug("Initializing");

        log_debug(json_config.dump().c_str());
        auto model_name = vpmu::utils::get_json<std::string>(json_config, "name");
        strncpy(model.name, model_name.c_str(), sizeof(model.name));
        model.latency = vpmu::utils::get_json<int>(json_config, "miss latency");
        try {
            entry_size = json_config["entry size"].get<int>();
        } catch (std::domain_error e) {
            entry_size = 256;
            log("Entry size for GHT branch predictor is not set, use default: %d",
                entry_size);
        }

        for (int i = 0; i < VPMU_MAX_CPU_CORES; i++) {
            predictor[i].resize(entry_size);
        }

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
            data = counters;
            break;
        case VPMU_PACKET_DUMP_INFO:
            int i;

            CONSOLE_LOG("  [%d] type : Global History Table Predictor (%d-Entry)\n",
                        id,
                        entry_size);
            // Accuracy
            CONSOLE_LOG("    -> predict accuracy    : (");
            for (i = 0; i < platform_info.cpu.cores - 1; i++) {
                CONSOLE_LOG("%'0.2f, ",
                            (float)counters.correct[i]
                              / (counters.correct[i] + counters.wrong[i]));
            }
            CONSOLE_LOG("%'0.2f)\n",
                        (float)counters.correct[i]
                          / (counters.correct[i] + counters.wrong[i]));
            // Correct
            CONSOLE_LOG("    -> correct prediction  : (");
            for (i = 0; i < platform_info.cpu.cores - 1; i++)
                CONSOLE_LOG("%'" PRIu64 ", ", counters.correct[i]);
            CONSOLE_LOG("%'" PRIu64 ")\n", counters.correct[i]);
            // Wrong
            CONSOLE_LOG("    -> wrong prediction    : (");
            for (i = 0; i < platform_info.cpu.cores - 1; i++)
                CONSOLE_LOG("%'" PRIu64 ", ", counters.wrong[i]);
            CONSOLE_LOG("%'" PRIu64 ")\n", counters.wrong[i]);
            break;
        case VPMU_PACKET_RESET:
            counters = {}; // Zero initializer
            break;
        case VPMU_PACKET_DATA:
            ght_branch_predictor(ref);
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
    int entry_size = 0;
    // predictor (the states of branch predictors)
    std::vector<uint8_t> predictor[VPMU_MAX_CPU_CORES] = {};
    uint64_t             history[VPMU_MAX_CPU_CORES]   = {};
    VPMU_Branch::Data    counters                      = {}; // Zero initializer
    // The CPU configurations for timing model
    using VPMUSimulator::platform_info;

    void ght_branch_predictor(const VPMU_Branch::Reference& ref)
    {
        int taken       = ref.taken;
        int core        = ref.core;
        int entry_index = history[core] & (entry_size - 1);

        if (two_bits_predictor(&predictor[core][entry_index], taken)) {
            counters.correct[core]++;
        } else {
            counters.wrong[core]++;
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
