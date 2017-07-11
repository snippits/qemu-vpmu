#ifndef __PHASE_COMMON_HPP_
#define __PHASE_COMMON_HPP_

#define DEFAULT_WINDOW_SIZE 200000 // 200k instructions
#define DEFAULT_VECTOR_SIZE 2048   // 2048 buckets per BBV

#include <utility> // std::pair

struct GPUFriendnessCounter {
    uint64_t insn;
    uint64_t load;
    uint64_t store;
    uint64_t alu;
    uint64_t bit; // shift, and, or, xor
    uint64_t branch;
};

#endif
