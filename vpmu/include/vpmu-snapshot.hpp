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
    VPMUSnapshot() { reset(); }
    // Copy constructor
    VPMUSnapshot(const VPMUSnapshot& rhs)
    {
        insn_data   = rhs.insn_data;
        branch_data = rhs.branch_data;
        cache_data  = rhs.cache_data;
        memcpy(time_ns, rhs.time_ns, sizeof(time_ns));
    }

    void take_snapshot(void)
    {
        insn_data   = vpmu_insn_stream.get_data();
        branch_data = vpmu_branch_stream.get_data();
        cache_data  = vpmu_cache_stream.get_data();

        time_ns[0] = vpmu::target::cpu_time_ns();
        time_ns[1] = vpmu::target::branch_time_ns();
        time_ns[2] = vpmu::target::cache_time_ns();
        time_ns[3] = vpmu::target::memory_time_ns();
        time_ns[4] = vpmu::target::io_time_ns();
        time_ns[5] = vpmu::target::time_ns();
    }

    void reset(void)
    {
        memset(&insn_data, 0, sizeof(insn_data));
        memset(&branch_data, 0, sizeof(branch_data));
        memset(&cache_data, 0, sizeof(cache_data));
        memset(&time_ns, 0, sizeof(time_ns));
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
                    VPMU_Insn::Data&   _insn_data,
                    VPMU_Branch::Data& _branch_data,
                    VPMU_Cache::Data&  _cache_data,
                    uint64_t           _time_ns[])
    {
        accumulate_counter(insn_data, new_snapshot.insn_data, _insn_data);
        accumulate_counter(branch_data, new_snapshot.branch_data, _branch_data);
        accumulate_counter(cache_data, new_snapshot.cache_data, _cache_data);

        _time_ns[0] += vpmu::target::cpu_time_ns() - time_ns[0];
        _time_ns[1] += vpmu::target::branch_time_ns() - time_ns[1];
        _time_ns[2] += vpmu::target::cache_time_ns() - time_ns[2];
        _time_ns[3] += vpmu::target::memory_time_ns() - time_ns[3];
        _time_ns[4] += vpmu::target::io_time_ns() - time_ns[4];
        _time_ns[5] += vpmu::target::time_ns() - time_ns[5];
    }

    void add(VPMUSnapshot& another_snapshot)
    {
        VPMU_Insn::Data   _insn_data   = {};
        VPMU_Branch::Data _branch_data = {};
        VPMU_Cache::Data  _cache_data  = {};

        accumulate_counter(_insn_data, another_snapshot.insn_data, insn_data);
        accumulate_counter(_branch_data, another_snapshot.branch_data, branch_data);
        accumulate_counter(_cache_data, another_snapshot.cache_data, cache_data);

        for (int i = 0; i < sizeof(time_ns) / sizeof(time_ns[0]); i++) {
            time_ns[i] += another_snapshot.time_ns[i];
        }
    }

    VPMU_Insn::Data   insn_data   = {};
    VPMU_Branch::Data branch_data = {};
    VPMU_Cache::Data  cache_data  = {};
    uint64_t          time_ns[6]  = {};
};

#endif
