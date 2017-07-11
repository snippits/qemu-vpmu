#ifndef _REGION_INFO_HPP_
#define _REGION_INFO_HPP_

#include <memory> // std::shared_ptr
#include <string> // std::string

#include "et-program.hpp"   // ET_Program class
#include "beg_eng_pair.hpp" // Pair_beg_end

class RegionInfo
{
public:
    inline bool operator<(const RegionInfo& rhs) const
    {
        return (this->address < rhs.address);
    }
    inline bool operator>(const RegionInfo& rhs) const
    {
        return (this->address > rhs.address);
    }

    inline bool operator==(const RegionInfo& rhs) const { return (this == &rhs); }
    inline bool operator!=(const RegionInfo& rhs) const { return !(this == &rhs); }

    Pair_beg_end address      = {};
    uint64_t     permission   = 0;
    std::string  pathname     = "";
    bool         removed_flag = false;
    // Target program belonging to this region
    std::shared_ptr<ET_Program> program = nullptr;

    // This is used for returning not found in search
    static RegionInfo not_found;
};

#endif
