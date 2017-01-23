#ifndef __PHASE_HPP_
#define __PHASE_HPP_

extern "C" {
#include "phase.h"
}
// #include <eigen3/Eigen/Dense> // Use Eigen for vector and its operations
#include "vpmu.hpp"        // Include types and basic headers
#include "vpmu-log.hpp"    // VPMULog
#include "vpmu-utils.hpp"  // Various functions
#include "vpmu-packet.hpp" // All performance counter data types

#include "vpmu-snapshot.hpp" // VPMUSanpshot

using CodeRange = std::pair<uint64_t, uint64_t>;

class Window
{
public:
    Window() { branch_vector.resize(DEFAULT_VECTOR_SIZE); }
    Window(int  vector_length) { branch_vector.resize(vector_length); }
    inline void update_vector(uint64_t pc);

    void reset(void)
    {
        instruction_count = 0;
        memset(&branch_vector[0], 0, branch_vector.size() * sizeof(branch_vector[0]));
        code_walk_count.clear();
    }

    // Eigen::VectorXd branch_vector;
    std::vector<double> branch_vector;
    // Instruction count
    uint64_t instruction_count = 0;
    std::map<CodeRange, uint32_t> code_walk_count;
};

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
    }

    void set_vector(std::vector<double>& vec)
    {
        m_vector_dirty = true;
        branch_vector  = vec;
    }

    void update_vector(std::vector<double>& vec)
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

    void update_walk_count(std::map<CodeRange, uint32_t>& new_walk_count)
    {
        for (auto&& wc : new_walk_count) {
            code_walk_count[wc.first] += wc.second;
        }
    }

    void update(Window& window)
    {
        update_vector(window.branch_vector);
        update_walk_count(window.code_walk_count);
        num_windows++;
    }

    const std::vector<double>& get_vector(void) { return branch_vector; }
    std::vector<double>&       get_normalized_vector(void)
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

public: // FIXME, make it private
    VPMUSnapshot snapshot = {};
    std::map<CodeRange, uint32_t> code_walk_count;
};

class Classifier : public VPMULog
{
public:
    Classifier() : VPMULog("Classifier"){};

    void set_similarity_threshold(uint64_t new_threshold)
    {
        similarity_threshold = new_threshold;
    }

    virtual Phase& classify(std::vector<Phase>& phase_list, const Window& window)
    {
        log_fatal("classify() is not implemented");
        return Phase::not_found;
    }

protected:
    uint64_t similarity_threshold = 1;
};

class PhaseDetect
{
public:
    // No default constructor for this class
    PhaseDetect() = delete;

    PhaseDetect(uint64_t new_window_size, std::unique_ptr<Classifier>&& new_classifier)
    {
        // window size can be changed in the code
        window_size = new_window_size;
        classifier  = std::move(new_classifier);
    }

    void change_classifier(std::unique_ptr<Classifier>&& new_classifier)
    {
        classifier = std::move(new_classifier);
    }

    inline Phase& classify(std::vector<Phase>& phase_list, const Window& window)
    {
        return classifier->classify(phase_list, window);
    }

    inline uint64_t get_window_size(void) { return window_size; }

    void dump_data(FILE* fp, VPMU_Insn::Data data);
    void dump_data(FILE* fp, VPMU_Cache::Data data);
    void dump_data(FILE* fp, VPMU_Branch::Data data);

private:
    uint64_t window_size;
    // C++ must use pointer in order to call the derived class virtual functions
    std::unique_ptr<Classifier> classifier;
};

extern PhaseDetect phase_detect;
#endif
