#ifndef __VPMU_SIM_HPP_
#define __VPMU_SIM_HPP_
#include <vector> // std::vector
#include <string> // std::string
#include <thread> // std::thread
#include <vector> // std::forward
#include "vpmu-packet.hpp"

template <typename T>
class VPMUSimulator : public VPMULog
{
public:
    VPMUSimulator() {}
    VPMUSimulator(const char *module_name) { set_name(module_name); }
    VPMUSimulator(std::string module_name) { set_name(module_name); }
    ~VPMUSimulator() { log_debug("Destructed"); }
    // VPMUStream is not copyable.
    VPMUSimulator(const VPMUSimulator &) = delete;

    void set_platform_info(VPMUPlatformInfo info) { platform_info = info; }
    void bind(nlohmann::json &j) { json_config = j; }

    // This is where the formal initialization should be done.
    // Conceptually, this is called by individual thread/process.
    // Allocating big memory is allowed here.
    // Also, You need to reply your simulator configurations to VPMU here!
    virtual void build(T &t) {}

    // To avoid unnecessary (virtual) function calls,
    // each simulator has to implement its packet processor
    virtual inline void packet_processor(int id, typename T::Reference &ref, T &t)
    {
        log_fatal("packet_processor function is not implemented!!");
    }

protected:
    nlohmann::json   json_config;
    VPMUPlatformInfo platform_info = {0};

public:
    // Ids for management
    uint32_t        id;
    std::thread::id tid;
    pid_t           pid;
};

#endif