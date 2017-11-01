#ifndef __WINDOW_HPP_
#define __WINDOW_HPP_
#pragma once

// #include <eigen3/Eigen/Dense> // Use Eigen for vector and its operations
#include <valarray> // Use std::valarray to accelerate the operations

#include "beg_eng_pair.hpp" // Pair_beg_end
#include "phase-common.hpp" // Common definitions of phase detection
#include "vpmu.hpp"         // Include types and basic headers

class Window
{
public:
    Window() { branch_vector.resize(DEFAULT_VECTOR_SIZE); }
    Window(size_t vector_length) { branch_vector.resize(vector_length); }

    void update(const ExtraTBInfo* extra_tb_info)
    {
        uint64_t pc     = extra_tb_info->start_addr;
        uint64_t pc_end = pc + extra_tb_info->counters.size_bytes;
        // Update timestamp if this window is cleared before,
        // which means this is a new window.
        if (flag_reset) {
            this->timestamp = vpmu_get_timestamp_us();
        }
        flag_reset = false;
        update_bbv(pc);
        update_counter(extra_tb_info);
        instruction_count += extra_tb_info->counters.total;

        code_walk_count[Pair_beg_end({pc, pc_end})] += 1;
    }

    void reset(void)
    {
        flag_reset        = true;
        timestamp         = 0;
        instruction_count = 0;
        counters          = {};
        memset(&branch_vector[0], 0, branch_vector.size() * sizeof(branch_vector[0]));
        code_walk_count.clear();
    }

private:
    inline void update_bbv(uint64_t pc)
    {
        // Get the hased index for current pc address
        uint64_t hashed_key = simple_hash(pc / 4, branch_vector.size());
        branch_vector[hashed_key]++;
    }

    inline void update_counter(const ExtraTBInfo* extra_tb_info)
    {
        counters.insn += extra_tb_info->counters.total;
        counters.load += extra_tb_info->counters.load;
        counters.store += extra_tb_info->counters.store;
        counters.alu += extra_tb_info->counters.alu;
        counters.bit += extra_tb_info->counters.bit;
        counters.branch += extra_tb_info->has_branch;
    }

    inline uint64_t simple_hash(uint64_t key, uint64_t m) { return (key % m); }

    // http://zimbry.blogspot.tw/2011/09/better-bit-mixing-improving-on.html
    inline uint64_t bitmix_hash(uint64_t key)
    {
        key = (key ^ (key >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        key = (key ^ (key >> 27)) * UINT64_C(0x94d049bb133111eb);
        key = key ^ (key >> 31);

        return key;
    }

public:
    /// Flag to indicate whether the window is reset before
    bool flag_reset = true;
    /// The timestamp of begining of this window
    uint64_t timestamp = 0;
    /// The basic block vector. (Perhapse using Eigen::VectorXd?)
    std::valarray<double> branch_vector = {};
    /// Instruction count
    uint64_t instruction_count = 0;
    /// Walk count of basic blocks <<BB beg, BB end>, count>
    std::map<Pair_beg_end, uint32_t> code_walk_count;
    /// Extra information carried in a phase for GPU prediction
    GPUFriendnessCounter counters = {};
};

#endif
