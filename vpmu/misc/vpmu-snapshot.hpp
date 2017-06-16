#ifndef __VPMU_SNAPSHOT_HPP_
#define __VPMU_SNAPSHOT_HPP_
extern "C" {
#include "vpmu-qemu.h" // VPMUPlatformInfo
}
#include "vpmu-packet.hpp" // All performance counter data types
#include "vpmu-insn.hpp"   // InsnStream
#include "vpmu-cache.hpp"  // CacheStream
#include "vpmu-branch.hpp" // BranchStream

// Sometimes, you have to go Insane, to Outsane, the Sane. You know what i'm Sanin?

class VPMUSnapshot
{
public:
    /// Do nothing for default constructor
    VPMUSnapshot() {}

    /// Take a new snapshot at initialization following RAII form
    VPMUSnapshot(bool take_a_shot_flag)
    {
        if (take_a_shot_flag) take_snapshot();
    }

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

    VPMUSnapshot operator+(const VPMUSnapshot& rhs)
    {
        VPMUSnapshot out = {}; // Copy elision

        out.insn_data   = this->insn_data + rhs.insn_data;
        out.branch_data = this->branch_data + rhs.branch_data;
        out.cache_data  = this->cache_data + rhs.cache_data;

        for (int i = 0; i < sizeof(time_ns) / sizeof(uint64_t); i++) {
            out.time_ns[i] = this->time_ns[i] + rhs.time_ns[i];
        }

        return out;
    }

    VPMUSnapshot operator-(const VPMUSnapshot& rhs)
    {
        VPMUSnapshot out = {}; // Copy elision

        out.insn_data   = this->insn_data - rhs.insn_data;
        out.branch_data = this->branch_data - rhs.branch_data;
        out.cache_data  = this->cache_data - rhs.cache_data;

        for (int i = 0; i < sizeof(time_ns) / sizeof(uint64_t); i++) {
            out.time_ns[i] = this->time_ns[i] - rhs.time_ns[i];
        }

        return out;
    }

    VPMUSnapshot& operator+=(const VPMUSnapshot& rhs)
    {
        this->insn_data   = this->insn_data + rhs.insn_data;
        this->branch_data = this->branch_data + rhs.branch_data;
        this->cache_data  = this->cache_data + rhs.cache_data;

        for (int i = 0; i < sizeof(time_ns) / sizeof(uint64_t); i++) {
            this->time_ns[i] += rhs.time_ns[i];
        }

        return *this;
    }

public:
    VPMU_Insn::Data   insn_data   = {};
    VPMU_Branch::Data branch_data = {};
    VPMU_Cache::Data  cache_data  = {};
    uint64_t          time_ns[6]  = {};
};

#endif
