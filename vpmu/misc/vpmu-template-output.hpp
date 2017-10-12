#ifndef __VPMU_TEMPLATE_CONSOLE_OUTPUT_
#define __VPMU_TEMPLATE_CONSOLE_OUTPUT_
#pragma once

#include "vpmu-log.hpp"       // CONSOLE_LOG
#include "vpmu-utils.hpp"     // vpmu::utils and vpmu::math
#include "vpmu-packet.hpp"    // All data format
#include "vpmu-snapshot.hpp"  // VPMUSnapshot
#include "vpmu/libs/json.hpp" // nlohmann::json

namespace vpmu
{

namespace output
{
    void u64_array(uint64_t value[]);
    void percentage_array(uint64_t first_val[], uint64_t second_val[]);
    void CPU_counters(VPMU_Insn::Model model, VPMU_Insn::Data data);
    void Branch_counters(VPMU_Branch::Model model, VPMU_Branch::Data data);
    void Cache_counters(VPMU_Cache::Model model, VPMU_Cache::Data data);

} // End of namespace vpmu::output

namespace dump
{
    void u64_array(FILE* fp, uint64_t value[]);
    void percentage_array(FILE* fp, uint64_t first_val[], uint64_t second_val[]);
    void CPU_counters(FILE* fp, VPMU_Insn::Model model, VPMU_Insn::Data data);
    void Branch_counters(FILE* fp, VPMU_Branch::Model model, VPMU_Branch::Data data);
    void Cache_counters(FILE* fp, VPMU_Cache::Model model, VPMU_Cache::Data data);
    void snapshot(FILE* fp, VPMUSnapshot snapshot);

} // End of namespace vpmu::dump

namespace dump_json
{
    using nlohmann::json;

    void CPU_counters(json& j, VPMU_Insn::Model model, VPMU_Insn::Data data);
    void Branch_counters(json& j, VPMU_Branch::Model model, VPMU_Branch::Data data);
    void Cache_counters(json& j, VPMU_Cache::Model model, VPMU_Cache::Data data);

    nlohmann::json snapshot(VPMUSnapshot snapshot);

} // End of namespace vpmu::dump_json
} // End of namespace vpmu

#endif // ifndef __VPMU_TEMPLATE_CONSOLE_OUTPUT_
