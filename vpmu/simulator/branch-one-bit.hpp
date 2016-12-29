#ifndef __BRANCH_ONE_BIT_HPP__
#define __BRANCH_ONE_BIT_HPP__
#include "vpmu-sim.hpp"    // VPMUSimulator
#include "vpmu-packet.hpp" // VPMU_Branch::Reference

class Branch_One_Bit : public VPMUSimulator<VPMU_Branch>
{
public:
    Branch_One_Bit() : VPMUSimulator("One Bit") { log_debug("Constructed"); }
    ~Branch_One_Bit() { log_debug("Destructed"); }

    void destroy() override { ; } // Nothing to do

    void build(VPMU_Branch& branch) override
    {
        log_debug("Initializing");

        log_debug(json_config.dump().c_str());
        auto model_name = vpmu::utils::get_json<std::string>(json_config, "name");
        strncpy(branch.model.name, model_name.c_str(), sizeof(branch.model.name));
        branch.model.latency = vpmu::utils::get_json<int>(json_config, "miss latency");

        log_debug("Initialized");
    }

    void
    packet_processor(int id, VPMU_Branch::Reference& ref, VPMU_Branch& branch) override
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
            branch.data = counters;
            break;
        case VPMU_PACKET_DUMP_INFO:
            int i;

            CONSOLE_LOG("  [%d] type : One Bit Predictor\n", id);
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
            counters = {0};
            break;
        case VPMU_PACKET_DATA:
            one_bit_branch_predictor(&ref);
            break;
        default:
            log_fatal("Unexpected packet");
        }
    }

private:
#ifdef CONFIG_VPMU_DEBUG_MSG
    // The total number of packets counter for debugging
    uint64_t debug_packet_num_cnt = 0;
#endif
    // predictor (the states of branch predictors)
    uint64_t          predictor[VPMU_MAX_CPU_CORES] = {0};
    VPMU_Branch::Data counters                      = {0};
    // The CPU configurations for timing model
    using VPMUSimulator::platform_info;

    void one_bit_branch_predictor(VPMU_Branch::Reference* ref)
    {
        int taken = ref->taken;
        int core  = ref->core;

        switch (predictor[core]) {
        case 0: // predict not taken
            if (taken) {
                predictor[core] = 1;
                counters.wrong[core]++;
            } else
                counters.correct[core]++;
            break;
        case 1: // predict taken
            if (taken)
                counters.correct[core]++;
            else {
                predictor[core] = 0;
                counters.wrong[core]++;
            }
            break;
        default:
            ERR_MSG("predictor[core] error\n");
            exit(1);
        }
    }
};

#endif
