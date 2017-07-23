#ifndef __VPMU_TEMPLATE_CONSOLE_OUTPUT_
#define __VPMU_TEMPLATE_CONSOLE_OUTPUT_
#pragma once

#include "vpmu-log.hpp"    // CONSOLE_LOG
#include "vpmu-utils.hpp"  // vpmu::utils and vpmu::math
#include "vpmu-packet.hpp" // All data format

namespace vpmu
{

namespace output
{
    void print_u64_array(uint64_t value[]);
    void print_percentage_array(uint64_t first_val[], uint64_t second_val[]);
    void CPU_counters(VPMU_Insn::Model model, VPMU_Insn::Data data);
    void Branch_counters(VPMU_Branch::Model model, VPMU_Branch::Data data);
    void Cache_counters(VPMU_Cache::Model model, VPMU_Cache::Data data);

} // End of namespace vpmu::output
} // End of namespace vpmu

#endif // ifndef __VPMU_TEMPLATE_CONSOLE_OUTPUT_
