#ifndef __PHASE_HPP_
#define __PHASE_HPP_
#pragma once

extern "C" {
#include "vpmu/vpmu-extratb.h" // Insn_Counters, ExtraTBInfo
#include "phase.h"             // phasedet_ref (C interface function)
}
// #include <eigen3/Eigen/Dense> // Use Eigen for vector and its operations
#include <valarray> // Use std::valarray to accelerate the operations

#include "vpmu.hpp"          // Include types and basic headers
#include "phase-common.hpp"  // Common definitions of phase detection
#include "window.hpp"        // Window class
#include "vpmu-snapshot.hpp" // VPMUSanpshot
#include "beg_eng_pair.hpp"  // Pair_beg_end

class Phase
{
public:
    Phase() {}

    Phase(uint64_t id) { this->id = id; }

    Phase(Window window)
    {
        set_vector(window.branch_vector);
        // Set up other configurations
        num_windows     = 1;
        code_walk_count = window.code_walk_count;
        counters        = window.counters;
    }

    void set_vector(const std::valarray<double>& vec)
    {
        branch_vector   = vec;
        n_branch_vector = branch_vector;
        vpmu::math::normalize(n_branch_vector);
    }

    void update_vector(const std::valarray<double>& vec)
    {
        if (branch_vector.size() == 0) { // This was not initialized before
            set_vector(vec);
        }
        if (vec.size() != branch_vector.size()) {
            ERR_MSG("Vector size of phase and input does not match\n");
            return;
        }
        for (int i = 0; i < branch_vector.size(); i++) {
            branch_vector[i] += vec[i];
        }
        vpmu::math::normalize(branch_vector, n_branch_vector);
    }

    void update_counter(GPUFriendnessCounter w_counter)
    {
        counters.insn += w_counter.insn;
        counters.load += w_counter.load;
        counters.store += w_counter.store;
        counters.alu += w_counter.alu;
        counters.bit += w_counter.bit;
        counters.branch += w_counter.branch;
    }

    uint64_t get_insn_count(void) { return counters.insn; }

    void update_walk_count(std::map<Pair_beg_end, uint32_t>& new_walk_count)
    {
        for (auto&& wc : new_walk_count) {
            code_walk_count[wc.first] += wc.second;
        }
    }

    void update(Window& window)
    {
        update_vector(window.branch_vector);
        update_counter(window.counters);
        update_walk_count(window.code_walk_count);
        num_windows++;
    }

    void update(Phase& phase)
    {
        update_vector(phase.get_vector());
        update_counter(phase.get_counters());
        update_walk_count(phase.code_walk_count);
        snapshot += phase.snapshot;
        num_windows += phase.get_num_windows();
    }

    const uint64_t&              get_num_windows(void) const { return num_windows; }
    const GPUFriendnessCounter&  get_counters(void) const { return counters; }
    const std::valarray<double>& get_vector(void) const { return branch_vector; }
    const std::valarray<double>& get_normalized_vector(void) const
    {
        return n_branch_vector;
    }

    // Default comparison is pointer comparison
    inline bool operator==(const Phase& rhs) { return (this == &rhs); }
    inline bool operator!=(const Phase& rhs) { return !(this == &rhs); }

    static Phase not_found;

    void update_snapshot(VPMUSnapshot& process_snapshot);

    nlohmann::json json_counters(void);
    nlohmann::json json_fingerprint(void);

private:
    // Eigen::VectorXd branch_vector;
    std::valarray<double> branch_vector   = {};
    std::valarray<double> n_branch_vector = {};
    /// The number of windows included in this phase
    uint64_t num_windows = 0;
    /// The counters used for GPU friendliness prediction
    GPUFriendnessCounter counters = {};

public:
    /// An ID to identify the ID of this phase
    uint64_t id = 0;
    /// The snapshot of counters associated with this phase
    VPMUSnapshot snapshot = {};
    /// The walk-count associated with this phase. <<addr_beg, addr_end>, count>
    std::map<Pair_beg_end, uint32_t> code_walk_count = {};
};

#endif
