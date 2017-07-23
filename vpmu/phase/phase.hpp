#ifndef __PHASE_HPP_
#define __PHASE_HPP_
#pragma once

extern "C" {
#include "vpmu/vpmu-extratb.h" // Insn_Counters, ExtraTBInfo
#include "phase.h"             // phasedet_ref (C interface function)
}
// #include <eigen3/Eigen/Dense> // Use Eigen for vector and its operations

#include "vpmu.hpp"          // Include types and basic headers
#include "phase-common.hpp"  // Common definitions of phase detection
#include "window.hpp"        // Window class
#include "vpmu-snapshot.hpp" // VPMUSanpshot
#include "beg_eng_pair.hpp"  // Pair_beg_end

class Phase
{
public:
    Phase() {}

    Phase(Window window)
    {
        branch_vector = window.branch_vector;
        // Allocate slots as same size as branch_vector
        n_branch_vector.resize(branch_vector.size());
        vpmu::math::normalize(branch_vector, n_branch_vector);
        num_windows = 1;
        snapshot.reset();
        code_walk_count = window.code_walk_count;
        counters        = window.counters;
    }

    void set_vector(std::vector<double>& vec)
    {
        m_vector_dirty = true;
        branch_vector  = vec;
    }

    void update_vector(const std::vector<double>& vec)
    {
        m_vector_dirty = true;
        if (vec.size() != branch_vector.size()) {
            ERR_MSG("Vector size does not match\n");
            return;
        }
        for (int i = 0; i < branch_vector.size(); i++) {
            branch_vector[i] += vec[i];
        }
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

    const uint64_t&             get_num_windows(void) { return num_windows; }
    const GPUFriendnessCounter& get_counters(void) { return counters; }
    const std::vector<double>&  get_vector(void) const { return branch_vector; }

    const std::vector<double>& get_normalized_vector(void)
    {
        if (m_vector_dirty) {
            // Update normalized vector only when it's dirty
            vpmu::math::normalize(branch_vector, n_branch_vector);
        }
        return n_branch_vector;
    }

    // Default comparison is pointer comparison
    inline bool operator==(const Phase& rhs) { return (this == &rhs); }
    inline bool operator!=(const Phase& rhs) { return !(this == &rhs); }

    static Phase not_found;

    void update_snapshot(VPMUSnapshot& process_snapshot);

    void dump_result(FILE* fp);
    void dump_metadata(FILE* fp);

private:
    bool m_vector_dirty = false;
    // Eigen::VectorXd branch_vector;
    std::vector<double> branch_vector;
    std::vector<double> n_branch_vector;
    uint64_t            num_windows = 0;

    GPUFriendnessCounter counters = {};

public: // FIXME, make it private
    VPMUSnapshot snapshot = {};
    std::map<Pair_beg_end, uint32_t> code_walk_count;
    // An ID to identify the number of this phase
    uint64_t id             = 0;
    bool     sub_phase_flag = false;
};

#endif
