#ifndef _BEG_END_PAIR_HPP_
#define _BEG_END_PAIR_HPP_

extern "C" {
#include <stdint.h> // uint64_t types
};

class Pair_beg_end
{
public:
    inline bool operator<(const Pair_beg_end& rhs) const
    {
        return (this->beg < rhs.beg || ((this->beg == rhs.beg) && this->end < rhs.end));
    }
    inline bool operator>(const Pair_beg_end& rhs) const
    {
        return (this->beg > rhs.beg || ((this->beg == rhs.beg) && this->end > rhs.end));
    }
    inline bool operator==(const Pair_beg_end& rhs) const
    {
        return (this->beg == rhs.beg && this->end == rhs.end);
    }
    inline bool operator<=(const Pair_beg_end& rhs) const { return !(*this > rhs); }
    inline bool operator>=(const Pair_beg_end& rhs) const { return !(*this < rhs); }
    inline bool operator!=(const Pair_beg_end& rhs) const { return !(*this == rhs); }

    inline uint64_t size(void) { return end - beg; }
    inline void set_size(uint64_t size) { end = beg + size; }

public:
    // Data
    uint64_t beg = 0, end = 0;
};

#endif
