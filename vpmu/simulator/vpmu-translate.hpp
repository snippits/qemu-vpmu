#ifndef __VPMU_TRANSLATE_HPP_
#define __VPMU_TRANSLATE_HPP_
#pragma once

/**
 * @file vpmu-translate.hpp
 * @author Medicine Yeh
 * @date 17 Jan 2017
 * @brief File contains the class handler using in QEMU translation stage.
 *
 * The main purpose of a translator class is to accumulate the cycles and
 * counters of a TB. It should accumulate and store the variables in
 * ExtraTBInfo with thread-safe insurance.
 *
 * The translator class got its name by when it's being executed instead of
 * what it's doing. As the name indicates, the class is called when ever
 * QEMU translate a TB and one should implement his/her own functions with
 * C++ to C interface functions to complete the tasks.
 * @see target-xxx/translate.c:gen_intermediate_code()
 */

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
    virtual ~VPMUARMTranslate() {}
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
    virtual ~VPMUi386Translate() {}
    // VPMUStream is not copyable.
    VPMUi386Translate(const VPMUi386Translate &) = delete;

    // TODO x86 models
    virtual uint16_t get_x86_64_ticks(uint64_t insn) { return 0; }
    virtual uint16_t get_i386_ticks(uint32_t insn) { return 0; }
};

#if defined(TARGET_ARM)
/// Define architecture dependent translator type to ARM translator
using VPMUArchTranslate = VPMUARMTranslate;
#elif defined(TARGET_I386)
/// Define architecture dependent translator type to i386 translator
using VPMUArchTranslate = VPMUi386Translate;
#elif defined(TARGET_X86_64)
/// Define architecture dependent translator type to x86_64 translator
using VPMUArchTranslate = VPMUi386Translate;
#endif

#endif // End of __VPMU_TRANSLATE_HPP_
