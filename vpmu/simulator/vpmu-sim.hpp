#ifndef __VPMU_SIM_HPP_
#define __VPMU_SIM_HPP_
#pragma once

#include <vector>             // std::vector
#include <string>             // std::string
#include <thread>             // std::thread
#include <vector>             // std::forward
#include "json.hpp"           // nlohmann::json
#include "vpmu-log.hpp"       // VPMULog
#include "vpmu-packet.hpp"    // Packet types
#include "vpmu-translate.hpp" // VPMUARMTranslate, etc.

/// @brief A simulator class which should be inherited by each timing simulator
/// @details This class defines the interface between VPMU and each timing simulator.
/// For instruction simulator, get_translator_handle() must be override in child class.
/// Template T might be one of the following: VPMU_Branch, VPMU_Cache, VPMU_Insn
/// @see vpmu-packet.hpp
///
/// The flow of initialization of each timing simulator is:
/// VPMUSimulator("some name") -> bind() -> build() -> packet_processor()
/// Note that only build() and packet_processor() are done by individual
/// implementation of hardware component simulator.
///
/// Please refer to build() and packet_processor() for what should be done
/// specifically in these two stages.
///
/// Finally, among component simulators, only CPU simulator needs to implement
/// a translator class due to the design of QEMU dynamic binary translation.
/// The following code is a demo of this requirement.
/*! @code
class CPUSimulator : public VPMUSimulator
    class Translation : public VPMUARMTranslate
    {
        ...
    };
    // The instance of Translator called from QEMU when doing binary translation
    Translation translator;
    VPMUARMTranslate& get_translator_handle(void) override { return translator; }
};
@endcode
*/
template <typename T>
class VPMUSimulator : public VPMULog
{
public:
    VPMUSimulator() {}
    VPMUSimulator(const char *module_name) { set_name(module_name); }
    VPMUSimulator(std::string module_name) { set_name(module_name); }
    /// @brief Do nothing here. Use destroy() to deallocate resources, instead.
    /// @see destroy()
    virtual ~VPMUSimulator() { log_debug("Destructed"); }

    /// @brief VPMUSimulator is not copyable.
    VPMUSimulator(const VPMUSimulator &) = delete;

    /// @brief Setting the emulated platform information for this timing simulator.
    /// @param[in] info The VPMUPlatformInfo to be set.
    void set_platform_info(VPMUPlatformInfo info) { platform_info = info; }
    /// @brief Setting the json configuration to this timing simulator.
    /// @param[in] j The json configuration to this simulator.
    void bind(nlohmann::json &j) { json_config = j; }

    /// @brief Initiate and allocate resource required by this timing simulator.
    /// @details This is where the formal initialization should be done.
    /// Conceptually, this is called by individual thread/process.
    /// Allocating big memory is allowed here.
    /// Private member VPMUSimulator<T>::json_config can be read in this function
    /// for the configurations to this timing simulator.
    ///
    /// __ATTENTION!__ Simulator need to tell VPMU required information
    /// by setting the input argument - model.
    /// @param[out] t The model configuration returned to VPMU.
    virtual void build(typename T::Model &model) {}

    /// @brief This is where to release/free/deallocate resources holded by simulator.
    /// @details It's designed for making programer being aware of releasing resource.
    /// One can also rely on destructor to release resources when it's safe.
    virtual void destroy() { LOG_FATAL("destroy function is not implemented!!"); }

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
    /// Clear all the counters/values of simulator data.
    ///
    /// Component dependent data packet types:
    /// @param[in] id The identity number to this simulator. Started from 0.
    /// @param[in] ref The input packet, it could be either control/data packet.
    /// The state bits of ref are and should be zeros.
    /// @param[out] data The variable for synchronizing data back to VPMU.
    /// @see Branch_One_Bit::packet_processor()
    virtual inline void
    packet_processor(int id, const typename T::Reference &ref, typename T::Data &data)
    {
        LOG_FATAL("packet_processor function is not implemented!!");
    }

    /// @brief The main function of each timing simulator for processing hot traces.
    /// @details
    /// Default behavior: Remove state bits of packets and pass it to packet_processor().
    /// This is an extention function of packet_processor() for accelerating simulation.
    /// When a packet is a hot packet, it means that it's from a frequently accessed BB.
    /// The implementation of simulator can override the default behavior and do some
    /// magic arithmetic way to accumulate the cycles and counters.
    /// The implementation of simulator can also fallback to packet_processor() after
    /// removing the states of packet by calling packet_bypass().
    /// @param[in] id The identity number to this simulator. Started from 0.
    /// @param[in] ref The input hot packet, it could be either control/data packet.
    /// The state bits of ref are set and should be checked.
    /// @param[out] data The variable for synchronizing data back to VPMU.
    /// @see Cache_Dinero::hot_packet_processor()
    virtual inline void
    hot_packet_processor(int id, const typename T::Reference &ref, typename T::Data &data)
    {
        this->packet_processor(id, packet_bypass(ref), data);
    }

    /// @brief Clone the packet and remove the state bits of the packet.
    /// @details This function is usually used in hot_packet_processor() for
    /// passing input reference to the packet_processor() without any issue.
    /// @param[in] id The identity number to this simulator. Started from 0.
    /// @param[in] ref The input hot packet, it could be either control/data packet.
    /// @param[out] data The variable for synchronizing data back to VPMU.
    /// @see Cache_Dinero::hot_packet_processor()
    inline typename T::Reference packet_bypass(const typename T::Reference &ref)
    {
        typename T::Reference cloned_ref = ref;
        // Remove states
        cloned_ref.type = cloned_ref.type & ~VPMU_PACKET_STATES_MASK;
        return cloned_ref;
    }

    /// @brief Return the translator handle for aquiring timing of instructions.
    /// @details Only used in CPU instruction model when QEMU translats codes.
    /// The returned handler will be used in individual QEMU thread when executing
    /// gen_intermediate_code() in order to get cycles and info. of each TB.
    /// If you need to identify which core this handle is running on,
    /// please use vpmu::get_core_id().
    /// @return VPMUArchTranslate The translator class used in QEMU binary translation.
    virtual VPMUArchTranslate &get_translator_handle(void)
    {
        LOG_FATAL("get_translator function is not implemented!!");
        return dummy_null_translator;
    }

protected:
    nlohmann::json json_config; ///< The json configuration to this simulator.
    // Use zero initializer for initial value.
    VPMUPlatformInfo platform_info = {}; ///< The emulated platform info.

public:
    // Ids for management
    uint32_t        id;  ///< Identity number used for identify simulator.
    std::thread::id tid; ///< The thread id of this simulator, if it's a thread.
    pid_t           pid; ///< The process id of this simulator, if it's a process.

private:
    /// A dummy that does not have any setting
    /// Used when get_translator_handle() is not overrided
    VPMUArchTranslate dummy_null_translator;
};

#endif
