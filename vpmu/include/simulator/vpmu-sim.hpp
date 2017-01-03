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

template <typename T>
class VPMUSimulator : public VPMULog
{
public:
    VPMUSimulator() {}
    VPMUSimulator(const char *module_name) { set_name(module_name); }
    VPMUSimulator(std::string module_name) { set_name(module_name); }
    // Do nothing here. Use destroy to deallocate resources, instead.
    virtual ~VPMUSimulator() { log_debug("Destructed"); }

    // VPMUStream is not copyable.
    VPMUSimulator(const VPMUSimulator &) = delete;

    void set_platform_info(VPMUPlatformInfo info) { platform_info = info; }
    void bind(nlohmann::json &j) { json_config = j; }

    // This is where the formal initialization should be done.
    // Conceptually, this is called by individual thread/process.
    // Allocating big memory is allowed here.
    // Also, You need to reply your simulator configurations to VPMU here!
    virtual void build(T &t) {}

    // This is where to release/free/deallocate resources holded by simulator
    // It's designed for making programer being aware of releasing resource
    virtual void destroy() { log_fatal("destroy function is not implemented!!"); }

    // To avoid unnecessary (virtual) function calls,
    // each simulator has to implement its packet processor
    virtual inline void packet_processor(int id, typename T::Reference &ref, T &t)
    {
        log_fatal("packet_processor function is not implemented!!");
    }

    virtual inline void hot_packet_processor(int id, typename T::Reference &ref, T &t)
    {
        typename T::Reference p_ref = ref;
        // Remove states
        p_ref.type = p_ref.type & 0xF0FF;
        this->packet_processor(id, p_ref, t);
    }

    // This is used in instruction model only!
    virtual VPMUArchTranslate &get_translator_handle(void)
    {
        log_fatal("get_translator function is not implemented!!");
        return dummy_null_translator;
    }

protected:
    nlohmann::json   json_config;
    VPMUPlatformInfo platform_info = {0};

public:
    // Ids for management
    uint32_t        id;
    std::thread::id tid;
    pid_t           pid;

private:
    // A dummy that does not have any setting
    // Used when get_translator_handle is not overrided
    VPMUArchTranslate dummy_null_translator;
};

#endif
