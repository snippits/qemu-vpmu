#ifndef _BEG_END_PAIR_HPP_
#define _BEG_END_PAIR_HPP_

extern "C" {
#include <stdint.h> // uint64_t types
};

class Pair_beg_end
{
public:
    inline uint64_t size(void) { return end - beg; }
    inline void set_size(uint64_t size) { end = beg + size; }

    uint64_t beg = 0, end = 0;
};

#endif
