#ifndef __BRANCH_ALPHA_HPP__
#define __BRANCH_ALPHA_HPP__
#include "vpmu-sim.hpp"    // VPMUSimulator
#include "vpmu-packet.hpp" // VPMU_Branch::Reference

class Branch_ALPHA : public VPMUSimulator<VPMU_Branch>
{
public:
    Branch_ALPHA() : VPMUSimulator("ALPHA") {}
    ~Branch_ALPHA() {}

    void destroy() override { ; } // Nothing to do

    void build(VPMU_Branch::Model& model) override
    {
        log_debug("Initializing");

        log_debug(json_config.dump().c_str());
        auto model_name = vpmu::utils::get_json<std::string>(json_config, "name");
        strncpy(model.name, model_name.c_str(), sizeof(model.name));
        model.latency = vpmu::utils::get_json<int>(json_config, "miss latency");
        g_entry_size = vpmu::utils::get_json<int>(json_config, "global entry size", 4096);
        p_entry_size =
          vpmu::utils::get_json<int>(json_config, "pattern entry size", 1024);

        for (int i = 0; i < VPMU_MAX_CPU_CORES; i++) {
            meta_predictor[i].resize(g_entry_size);
            g_predictor[i].resize(g_entry_size);
            p_predictor[i].resize(p_entry_size);
            p_history[i].resize(p_entry_size);
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

            CONSOLE_LOG("  [%d] type : Alpha 21264 Predictor (%d-GEntry, %d-PEntry)\n",
                        id,
                        g_entry_size,
                        p_entry_size);
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
            alpha_branch_predictor(ref);
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
    int g_entry_size = 0;
    int p_entry_size = 0;
    // predictor (the states of branch predictors)
    std::vector<uint8_t> meta_predictor[VPMU_MAX_CPU_CORES] = {};
    std::vector<uint8_t> g_predictor[VPMU_MAX_CPU_CORES]    = {};
    uint64_t             g_history[VPMU_MAX_CPU_CORES]      = {};
    std::vector<uint8_t> p_predictor[VPMU_MAX_CPU_CORES]    = {};
    std::vector<uint8_t> p_history[VPMU_MAX_CPU_CORES]      = {};
    VPMU_Branch::Data    counters                           = {}; // Zero initializer
    // The CPU configurations for timing model
    using VPMUSimulator::platform_info;

    void alpha_branch_predictor(const VPMU_Branch::Reference& ref)
    {
        int  taken        = ref.taken;
        int  core         = ref.core;
        int  entry_index  = g_history[core] & (g_entry_size - 1);
        bool flag_correct = false, flag_g_correct = false, flag_p_correct = false;

        flag_g_correct = two_bits_predictor(&g_predictor[core][entry_index], taken);

        auto& p_history_entry = p_history[core][ref.pc & (p_entry_size - 1)];
        int   p_entry_index   = p_history_entry & (p_entry_size - 1);
        flag_p_correct  = three_bits_predictor(&p_predictor[core][p_entry_index], taken);
        p_history_entry = (p_history_entry << 1) | (taken == true);

        if (meta_predictor[core][entry_index] >= 2) {
            flag_correct = flag_g_correct;
        } else {
            flag_correct = flag_p_correct;
        }
        if (flag_correct) {
            counters.correct[core]++;
        } else {
            counters.wrong[core]++;
        }

        if (flag_g_correct && !flag_p_correct) {
            meta_predictor[core][entry_index] =
              (meta_predictor[core][entry_index] + 1) % 4;
        } else if (!flag_g_correct && flag_p_correct) {
            if (meta_predictor[core][entry_index] > 0)
                meta_predictor[core][entry_index] -= 1;
        }
        g_history[core] = (g_history[core] << 1) | (taken == true);
    }

    bool two_bits_predictor(uint8_t* entry, int taken)
    {
        bool flag = ((*entry >= 2) && (taken > 0)) || ((*entry < 2) && (taken == 0));

        if (taken) {
            if (*entry < 3) *entry += 1;
        } else {
            if (*entry > 0) *entry -= 1;
        }

        return flag;
    }

    bool three_bits_predictor(uint8_t* entry, int taken)
    {
        bool flag = ((*entry >= 4) && (taken > 0)) || ((*entry < 4) && (taken == 0));

        if (taken) {
            if (*entry < 7) *entry += 1;
        } else {
            if (*entry > 0) *entry -= 1;
        }

        return flag;
    }
};

#endif
