#ifndef __VPMU_SNAPSHOT_HPP_
#define __VPMU_SNAPSHOT_HPP_
#include "vpmu-packet.hpp" // All performance counter data types
#include "vpmu-insn.hpp"   // InsnStream
#include "vpmu-cache.hpp"  // CacheStream
#include "vpmu-branch.hpp" // BranchStream

// Sometimes, you have to go Insane, to Outsane, the Sane. You know what i'm Sanin?

class VPMUSnapshot
{
public:
    // Create a snapshot at creation
    VPMUSnapshot()
    {
        _insn_data   = vpmu_insn_stream.get_data();
        _branch_data = vpmu_branch_stream.get_data();
        _cache_data  = vpmu_cache_stream.get_data();
    }
    // Copy constructor
    VPMUSnapshot(const VPMUSnapshot& rhs)
    {
        _insn_data   = rhs._insn_data;
        _branch_data = rhs._branch_data;
        _cache_data  = rhs._cache_data;
    }

    void accumulate_counter(VPMU_Branch::Data& in_old,
                            VPMU_Branch::Data& in_new,
                            VPMU_Branch::Data& out)
    {
#define UPDATE_INT(__attri_name)                                                         \
    for (int i = 0; i < VPMU.platform.cpu.cores; i++) {                                  \
        out.__attri_name[i] += in_new.__attri_name[i] - in_old.__attri_name[i];          \
    }

        UPDATE_INT(wrong);
        UPDATE_INT(correct);
#undef UPDATE_INT
    }

    void accumulate_counter(VPMU_Cache::Data& in_old,
                            VPMU_Cache::Data& in_new,
                            VPMU_Cache::Data& out)
    {
#define UPDATE_INT(__attri_name)                                                         \
    for (int m = 0; m < VPMU_Cache::MAX_LEVEL; m++) {                                    \
        for (int i = 0; i < VPMU.platform.cpu.cores; i++) {                              \
            for (int j = 0; j < VPMU_Cache::SIZE_OF_INDEX; j++) {                        \
                out.__attri_name[PROCESSOR_CPU][m][i][j] +=                              \
                  in_new.__attri_name[PROCESSOR_CPU][m][i][j]                            \
                  - in_old.__attri_name[PROCESSOR_CPU][m][i][j];                         \
            }                                                                            \
        }                                                                                \
    }

#define UPDATE_INT_VAR(__attri_name)                                                     \
    out.__attri_name += in_new.__attri_name - in_old.__attri_name;

        UPDATE_INT(insn_cache);
        UPDATE_INT(data_cache);
        UPDATE_INT_VAR(memory_accesses);
        UPDATE_INT_VAR(memory_time_ns);

#undef UPDATE_INT_VAR
#undef UPDATE_INT
    }

    void accumulate_counter(VPMU_Insn::Data& in_old,
                            VPMU_Insn::Data& in_new,
                            VPMU_Insn::Data& out)
    {
#define UPDATE_INT(__attri_name)                                                         \
    for (int i = 0; i < VPMU.platform.cpu.cores; i++) {                                  \
        out.__attri_name[i] += in_new.__attri_name[i] - in_old.__attri_name[i];          \
    }

#define UPDATE_INT_VAR(__attri_name)                                                     \
    out.__attri_name += in_new.__attri_name - in_old.__attri_name;

#define UPDATE_INT_CELL(__mode_name)                                                     \
    UPDATE_INT_VAR(__mode_name.total_insn);                                              \
    UPDATE_INT_VAR(__mode_name.load);                                                    \
    UPDATE_INT_VAR(__mode_name.store);                                                   \
    UPDATE_INT_VAR(__mode_name.branch);

        UPDATE_INT_CELL(user);
        UPDATE_INT_CELL(system);
        UPDATE_INT_CELL(interrupt);
        UPDATE_INT_CELL(system_call);
        UPDATE_INT_CELL(rest);
        UPDATE_INT_CELL(fpu);
        UPDATE_INT_CELL(co_processor);
        UPDATE_INT(cycles);
        UPDATE_INT(insn_cnt);

#undef UPDATE_INT_CELL
#undef UPDATE_INT_VAR
#undef UPDATE_INT
    }

    void accumulate(VPMUSnapshot&      new_snapshot,
                    VPMU_Insn::Data&   insn_data,
                    VPMU_Branch::Data& branch_data,
                    VPMU_Cache::Data&  cache_data)
    {
        accumulate_counter(_insn_data, new_snapshot._insn_data, insn_data);
        accumulate_counter(_branch_data, new_snapshot._branch_data, branch_data);
        accumulate_counter(_cache_data, new_snapshot._cache_data, cache_data);
    }

    VPMU_Insn::Data   _insn_data   = {};
    VPMU_Branch::Data _branch_data = {};
    VPMU_Cache::Data  _cache_data  = {};
};

#endif
