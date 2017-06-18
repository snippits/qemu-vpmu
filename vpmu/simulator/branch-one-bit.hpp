#ifndef __BRANCH_ONE_BIT_HPP__
#define __BRANCH_ONE_BIT_HPP__
#include "vpmu-sim.hpp"             // VPMUSimulator
#include "vpmu-packet.hpp"          // VPMU_Branch::Reference
#include "vpmu-template-output.hpp" // Template output format

/// @brief One bit branch predictor class
/// @details This class demonstrates the use of VPMUSimulator class.
/// The implementations of a component simulator should override
/// build() and packet_processor() classes as the basic requirement.
///
/// The name of this simulator can be set when inherent VPMUSimulator class and
/// pass the name as the input argument. For example:
/*! @code
class Branch_One_Bit : public VPMUSimulator<VPMU_Branch>
{
public:
    Branch_One_Bit() : VPMUSimulator("One Bit") {}
}
@endcode
*/
class Branch_One_Bit : public VPMUSimulator<VPMU_Branch>
{
public:
    /// @brief Do nothing here but only assign the name.
    Branch_One_Bit() : VPMUSimulator("One Bit") {}
    /// @brief Do nothing here. Use destroy() to deallocate resources, instead.
    /// @see destroy()
    ~Branch_One_Bit() {}

    /// @brief This is where to release/free/deallocate resources holded by simulator.
    /// @details In this example, there is no dynamic allocated resources
    /// should be released.
    void destroy() override { ; }

    /// @brief Initiate and allocate resource required by this timing simulator.
    /// @details There are two pricate member you can access here:
    /// VPMUSimulator::json_config and VPMUSimulator::platform_info.
    /// These two variable are component simulator independent.
    /// You can read the configuration to this simulator by using json_config directly,
    /// or through vpmu::utils::get_json<T> function for field/type checking.
    /// The platform_info is used to read info. such as # of CPU cores, etc.
    ///
    /// If there is any error or the configuration is not complete, please inform user
    /// and use exit(EXIT_FAILURE) to stop simulation. If this function returns,
    /// Snippits will assume everything is fine and continue.
    ///
    /// __ATTENTION!__ Simulator need to tell VPMU required information
    /// by setting the input argument - model.
    /// @param[out] t The model configuration returned to VPMU.
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

    /// @brief The main function of each timing simulator for processing traces.
    /// @details This function would be called packet by packet.
    /// When synchronizing data, the implementation needs to write data to __t.data__.
    ///
    /// The packet types of control packet are:
    ///
    /// __VPMU_PACKET_BARRIER__ and __VPMU_PACKET_SYNC_DATA__:
    /// The individual component simulator is required to store latest data
    /// into __data__. All the packets before this packet must be done.
    /// There is no difference to what a component simulator should do.
    ///
    /// __VPMU_PACKET_DUMP_INFO__:
    /// Use CONSOLE_LOG() or log() to print simulator data in a nice form.
    ///
    /// __VPMU_PACKET_RESET__:
    /// Clear all the branch_data/values of simulator data.
    ///
    /// Component dependent data packet types:
    /// @param[in] id The identity number to this simulator. Started from 0.
    /// @param[in] ref The input packet, it could be either control/data packet.
    /// The state bits of ref are and should be zeros.
    /// @param[out] data The variable for synchronizing data back to VPMU.
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
            CONSOLE_LOG("  [%d] type : One Bit Predictor\n", id);
            vpmu::output::Branch_counters(branch_model, branch_data);

            break;
        case VPMU_PACKET_RESET:
            branch_data = {}; // Zero initializer
            break;
        case VPMU_PACKET_DATA:
            one_bit_branch_predictor(ref);
            break;
        default:
            LOG_FATAL("Unexpected packet");
        }
    }

private:
#ifdef CONFIG_VPMU_DEBUG_MSG
    /// The total number of packets counter for debugging
    uint64_t debug_packet_num_cnt = 0;
#endif
    /// The predictor per core (the states of branch predictors)
    uint64_t predictor[VPMU_MAX_CPU_CORES] = {};
    /// The tempory data storing the data needs by this branch predictor.
    /// In this case, the data equals to the branch data format in Snippits.
    VPMU_Branch::Data branch_data = {};
    /// The tempory data storing the model configuration of this branch predictor.
    VPMU_Branch::Model branch_model = {};
    /// Rename platform_info. The CPU configurations for timing model
    using VPMUSimulator::platform_info;

    /// @brief The implementation of one bit branch predictor.
    /// @details This function is a private function since it is used only in this calss.
    /// The definition of VPMUSimulator does not require this function, but one can use
    /// a separate function for better maintenance of codes, just like this.
    void one_bit_branch_predictor(const VPMU_Branch::Reference& ref)
    {
        int taken = ref.taken;
        int core  = ref.core;

        switch (predictor[core]) {
        case 0: // predict not taken
            if (taken) {
                predictor[core] = 1;
                branch_data.wrong[core]++;
            } else
                branch_data.correct[core]++;
            break;
        case 1: // predict taken
            if (taken)
                branch_data.correct[core]++;
            else {
                predictor[core] = 0;
                branch_data.wrong[core]++;
            }
            break;
        default:
            ERR_MSG("predictor[core] error\n");
            exit(1);
        }
    }
};

#endif
