#ifndef __VPMU_SIM_HPP_
#define __VPMU_SIM_HPP_
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
///
/// The flow of initialization of each timing simulator is:
/// VPMUSimulator("some name") -> bind() -> build() -> packet_processor()
/// The sample code of ARM CPU timing simulator:
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
    /// by setting the variable of model.
    /// @param[out] t The model configuration returned to VPMU.
    virtual void build(typename T::Model &model) {}

    /// @brief This is where to release/free/deallocate resources holded by simulator.
    /// @details It's designed for making programer being aware of releasing resource.
    /// One can also rely on destructor to release resources.
    virtual void destroy() { LOG_FATAL("destroy function is not implemented!!"); }

    /// @brief The main function of each timing simulator for processing traces.
    /// @details This function would be called packet by packet.
    /// When synchronizing data, the implementation needs to write data to __t.data__.
    /// @param[in] id The identity number to this simulator. Started from 0.
    /// @param[in] ref The input packet, it could be either control/data packet.
    /// @param[out] data The variable for synchronizing data back to VPMU.
    /// @see Branch_One_Bit::packet_processor()
    virtual inline void
    packet_processor(int id, const typename T::Reference &ref, typename T::Data &data)
    {
        LOG_FATAL("packet_processor function is not implemented!!");
    }

    /// @brief To be removed
    virtual inline void
    hot_packet_processor(int id, const typename T::Reference &ref, typename T::Data &data)
    {
        typename T::Reference p_ref = ref;
        // Remove states
        p_ref.type = p_ref.type & 0xF0FF;
        this->packet_processor(id, p_ref, data);
    }

    /// @brief Return the translator handle for aquiring timing of instructions.
    /// @details Only used in CPU instruction model when QEMU translats codes.
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
