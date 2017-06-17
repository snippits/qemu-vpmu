#ifndef __TODO_MISC_HPP_
#define __TODO_MISC_HPP_

inline uint64_t vpmu_sum_u64_array(uint64_t value[]);
// TODO The following functions need to be refactored
#define sum_all_modes(_D, _N) _D.user._N[i] + _D.system._N[i]

/// A function to help sum up instruction count of each mode (user, sys, ...)
inline uint64_t vpmu_total_insn_count(VPMU_Insn::Data &insn_data)
{
    int sum = 0;

    for (int i = 0; i < VPMU.platform.cpu.cores; i++) {
        sum += sum_all_modes(insn_data, total_insn);
    }
    return sum;
}

/// A function to help sum up ld/st count of each mode (user, sys, ...)
inline uint64_t vpmu_total_ldst_count(VPMU_Insn::Data &insn_data)
{
    int sum = 0;

    for (int i = 0; i < VPMU.platform.cpu.cores; i++) {
        sum += sum_all_modes(insn_data, load) + sum_all_modes(insn_data, store);
    }
    return sum;
}

/// A function to help sum up load count of each mode (user, sys, ...)
inline uint64_t vpmu_total_load_count(VPMU_Insn::Data &insn_data)
{
    int sum = 0;

    for (int i = 0; i < VPMU.platform.cpu.cores; i++) {
        sum += sum_all_modes(insn_data, load);
    }
    return sum;
}

/// A function to help sum up store count of each mode (user, sys, ...)
inline uint64_t vpmu_total_store_count(VPMU_Insn::Data &insn_data)
{
    int sum = 0;

    for (int i = 0; i < VPMU.platform.cpu.cores; i++) {
        sum += sum_all_modes(insn_data, store);
    }
    return sum;
}

/// A function to help sum up branch insn. count of each mode (user, sys, ...)
inline uint64_t vpmu_branch_insn_count(VPMU_Insn::Data &insn_data)
{
    int sum = 0;

    for (int i = 0; i < VPMU.platform.cpu.cores; i++) {
        sum += sum_all_modes(insn_data, branch);
    }
    return sum;
}

inline void vpmu_print_u64_array(uint64_t value[])
{
    int i = 0;
    for (i = 0; i < VPMU.platform.cpu.cores - 1; i++) {
        CONSOLE_LOG("%'" PRIu64 ", ", value[i]);
    }
    CONSOLE_LOG("%'" PRIu64 "\n", value[i]);
}

inline uint64_t vpmu_sum_u64_array(uint64_t value[])
{
    uint64_t sum = 0;
    for (int i = 0; i < VPMU.platform.cpu.cores; i++) {
        sum += value[i];
    }
    return sum;
}

#undef sum_all_modes
#endif
