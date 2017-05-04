#ifndef __WINDOW_HPP_
#define __WINDOW_HPP_

// #include <eigen3/Eigen/Dense> // Use Eigen for vector and its operations

#include "phase-common.hpp" // Common definitions of phase detection
#include "vpmu.hpp"         // Include types and basic headers

class Window
{
public:
    Window() { branch_vector.resize(DEFAULT_VECTOR_SIZE); }
    Window(int  vector_length) { branch_vector.resize(vector_length); }
    inline void update_vector(uint64_t pc);
    inline void update_counter(const ExtraTBInfo* extra_tb_info);
    inline void update(const ExtraTBInfo* extra_tb_info);

    void reset(void)
    {
        timestamp         = 0;
        instruction_count = 0;
        memset(&branch_vector[0], 0, branch_vector.size() * sizeof(branch_vector[0]));
        code_walk_count.clear();
        memset(&counters, 0, sizeof(counters));
    }

    // The timestamp of begining of this window
    uint64_t timestamp = 0;
    // Eigen::VectorXd branch_vector;
    std::vector<double> branch_vector;
    // Instruction count
    uint64_t instruction_count = 0;
    std::map<CodeRange, uint32_t> code_walk_count;
    GPUFriendnessCounter counters = {};
};

#endif
