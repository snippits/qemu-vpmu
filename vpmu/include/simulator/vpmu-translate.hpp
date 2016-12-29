#ifndef __VPMU_TRANSLATE_HPP_
#define __VPMU_TRANSLATE_HPP_
#include <vector>       // std::vector
#include <string>       // std::string
#include <thread>       // std::thread
#include "vpmu-log.hpp" // VPMULog
#include "json.hpp"     // nlohmann::json

class VPMUARMTranslate : public VPMULog
{
public:
    VPMUARMTranslate() { set_name("ARMTranslate"); }
    VPMUARMTranslate(const char *module_name) { set_name(module_name); }
    VPMUARMTranslate(std::string module_name) { set_name(module_name); }
    ~VPMUARMTranslate() { log_debug("Destructed"); }
    // VPMUStream is not copyable.
    VPMUARMTranslate(const VPMUARMTranslate &) = delete;

    virtual uint16_t get_arm64_ticks(uint64_t insn) { return 0; }
    virtual uint16_t get_arm_ticks(uint32_t insn) { return 0; }
    virtual uint16_t get_thumb_ticks(uint32_t insn) { return 0; }
    virtual uint16_t get_cp14_ticks(uint32_t insn) { return 0; }
#ifdef CONFIG_VPMU_VFP
    virtual uint16_t get_vfp_ticks(uint32_t insn, uint64_t vfp_vec_len) { return 0; }
#endif
};

class VPMUi386Translate : public VPMULog
{
public:
    VPMUi386Translate() { set_name("i386Translate"); }
    VPMUi386Translate(const char *module_name) { set_name(module_name); }
    VPMUi386Translate(std::string module_name) { set_name(module_name); }
    ~VPMUi386Translate() { log_debug("Destructed"); }
    // VPMUStream is not copyable.
    VPMUi386Translate(const VPMUi386Translate &) = delete;

    // TODO x86 models
};


#if defined(TARGET_ARM)
using VPMUArchTranslate = VPMUARMTranslate;
#elif defined(TARGET_I386)
using VPMUArchTranslate = VPMUi386Translate;
#elif defined(TARGET_X86_64)
using VPMUArchTranslate = VPMUi386Translate;
#endif

#endif // End of __VPMU_TRANSLATE_HPP_
