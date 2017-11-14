#ifndef __VPMU_SNAPSHOT_HPP_
#define __VPMU_SNAPSHOT_HPP_
#pragma once

extern "C" {
#include "vpmu-qemu.h" // VPMUPlatformInfo
}
#include "vpmu-packet.hpp" // All performance counter data types
#include "vpmu-insn.hpp"   // InsnStream
#include "vpmu-cache.hpp"  // CacheStream
#include "vpmu-branch.hpp" // BranchStream

#include <valarray>

// Sometimes, you have to go Insane, to Outsane, the Sane. You know what i'm Sanin?

class VPMUSnapshot
{
public:
    // TODO better interface for the input arguments
    /// Take a new snapshot at initialization following RAII form
    VPMUSnapshot(bool take_a_shot = false, int64_t core = -1)
    {
        if (take_a_shot) take_snapshot(core);
    }

    void take_snapshot(int64_t core = -1)
    {
        // TODO support per core data getter
        insn_data   = vpmu_insn_stream.get_data();
        branch_data = vpmu_branch_stream.get_data();
        cache_data  = vpmu_cache_stream.get_data();

        if (core > 0) {
            insn_data.mask_out_except(core);
            branch_data.mask_out_except(core);
            cache_data.mask_out_except(core);
            // TODO Should design two different snapshot classes
            // One with per-core info, the other without.
            sum_cores();
        }

        // TODO support per core time getter
        time_ns[0] = vpmu::target::cpu_time_ns();
        time_ns[1] = vpmu::target::branch_time_ns();
        time_ns[2] = vpmu::target::cache_time_ns();
        time_ns[3] = vpmu::target::memory_time_ns();
        time_ns[4] = vpmu::target::io_time_ns();
        time_ns[5] = vpmu::target::time_ns();
        time_ns[6] = vpmu::host::timestamp_ns();
    }

    void sum_cores(void)
    {
        insn_data.reduce();
        branch_data.reduce();
        cache_data.reduce();
    }

    void reset(void)
    {
        memset(&insn_data, 0, sizeof(insn_data));
        memset(&branch_data, 0, sizeof(branch_data));
        memset(&cache_data, 0, sizeof(cache_data));
        time_ns = 0;
    }

    VPMUSnapshot operator+(const VPMUSnapshot& rhs)
    {
        VPMUSnapshot out = {}; // Copy elision

        out.insn_data   = this->insn_data + rhs.insn_data;
        out.branch_data = this->branch_data + rhs.branch_data;
        out.cache_data  = this->cache_data + rhs.cache_data;
        out.time_ns     = this->time_ns + rhs.time_ns;

        return out;
    }

    VPMUSnapshot operator-(const VPMUSnapshot& rhs)
    {
        VPMUSnapshot out = {}; // Copy elision

        out.insn_data   = this->insn_data - rhs.insn_data;
        out.branch_data = this->branch_data - rhs.branch_data;
        out.cache_data  = this->cache_data - rhs.cache_data;
        out.time_ns     = this->time_ns - rhs.time_ns;

        return out;
    }

    VPMUSnapshot& operator+=(const VPMUSnapshot& rhs)
    {
        this->insn_data   = this->insn_data + rhs.insn_data;
        this->branch_data = this->branch_data + rhs.branch_data;
        this->cache_data  = this->cache_data + rhs.cache_data;
        this->time_ns     = this->time_ns + rhs.time_ns;

        return *this;
    }

public:
    VPMU_Insn::Data         insn_data   = {};
    VPMU_Branch::Data       branch_data = {};
    VPMU_Cache::Data        cache_data  = {};
    std::valarray<uint64_t> time_ns     = std::valarray<uint64_t>(7);
};

#endif
