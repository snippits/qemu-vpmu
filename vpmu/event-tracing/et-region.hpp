#ifndef __VPMU_ET_REGION_HPP_
#define __VPMU_ET_REGION_HPP_
extern "C" {
#include "vpmu/linux-mm.h" // VM_EXEC and other mmap() mode states
}
#include <vector> // std::vector
#include <string> // std::string
#include <mutex>  // Mutex

#include "et-program.hpp"   // ET_Program class
#include "beg_eng_pair.hpp" // Pair_beg_end class

/// A region can be either region for file or region for named/un-named space
class ET_Region : public ET_Path
{
public:
    ET_Region() {}
    ~ET_Region() {}

    /*
    ET_Region(const ET_Region& rhs)
    {
        for (auto& reg : rhs.regions) {
            regions.push_back(reg);
        }

        for (auto& reg : rhs.cached_exec_regions) {
            cached_exec_regions.push_back(reg);
        }
    }
    */

    void push_address(uint64_t address_start, uint64_t address_end)
    {
        std::lock_guard<std::mutex> lock(memory_region_lock);
        // Critical section
        Pair_beg_end addr = {};
        regionInfo   r    = {};

        // Remove repeated regions
        this->find_and_remove_region(address_start, address_end);

        addr.beg  = address_start;
        addr.end  = address_end;
        r.address = addr;
        regions.push_back(r);
    }

    void push_range(uint64_t    address_start,
                    uint64_t    address_end,
                    uint64_t    permission,
                    std::string pathname)
    {
        std::lock_guard<std::mutex> lock(memory_region_lock);
        // Critical section
        Pair_beg_end addr = {};
        regionInfo   r    = {};

        // Remove repeated regions
        this->find_and_remove_region(address_start, address_end);

        addr.beg     = address_start;
        addr.end     = address_end;
        r.address    = addr;
        r.permission = permission;
        r.pathname   = pathname;
        if (permission & VM_EXEC) {
            r.walk_count_vector.resize(address_end - address_start);
            cached_exec_regions.push_back(addr);
        }
        regions.push_back(r);
    }

    void push_range(std::shared_ptr<ET_Program> prog,
                    uint64_t                    address_start,
                    uint64_t                    address_end,
                    uint64_t                    permission,
                    std::string                 pathname)
    {
        std::lock_guard<std::mutex> lock(memory_region_lock);
        // Critical section
        Pair_beg_end addr = {};
        regionInfo   r    = {};

        // Remove repeated regions
        this->find_and_remove_region(address_start, address_end);

        addr.beg     = address_start;
        addr.end     = address_end;
        r.address    = addr;
        r.permission = permission;
        r.pathname   = pathname;
        r.program    = prog;
        if (permission & VM_EXEC) {
            r.walk_count_vector.resize(address_end - address_start);
            cached_exec_regions.push_back(addr);
        }
        regions.push_back(r);
    }

    void update_region(uint64_t prev_addr, uint64_t addr)
    {
        std::lock_guard<std::mutex> lock(memory_region_lock);
        // Critical section
        for (auto& reg : cached_exec_regions) {
            if (reg.beg == prev_addr) {
                uint64_t len = reg.end - reg.beg;
                reg.beg      = addr;
                reg.end      = addr + len;
            }
        }
        for (auto& reg : regions) {
            if (reg.address.beg == prev_addr) {
                uint64_t len    = reg.address.end - reg.address.beg;
                reg.address.beg = addr;
                reg.address.end = addr + len;
            }
        }
    }

    void reset_walk_counts_in_all_regions(void)
    {
        std::lock_guard<std::mutex> lock(memory_region_lock);
        // Critical section
        for (auto& reg : regions) {
            // Reset all elements to zeros
            std::fill(reg.walk_count_vector.begin(), reg.walk_count_vector.end(), 0);
        }
    }

    bool find_exec_region(uint64_t addr)
    {
        for (auto& reg : cached_exec_regions) {
            if (reg.beg <= addr && addr < reg.end) {
                return true;
            }
        }
        return false;
    }

    bool find_region(uint64_t addr)
    {
        for (auto& reg : regions) {
            if (reg.address.beg <= addr && addr < reg.address.end) {
                return true;
            }
        }
        return false;
    }

    uint64_t get_end_address(uint64_t addr)
    {
        for (auto& reg : regions) {
            if (reg.address.beg == addr) {
                return reg.address.end;
            }
        }
        return 0;
    }

    uint64_t find_address_beg(std::shared_ptr<ET_Program>& program, uint64_t mode)
    {
        for (auto& reg : regions) {
            // Skip regions with unmatched permissions
            if ((reg.permission & mode) != mode) continue;
            if (reg.program == program) {
                return reg.address.beg;
            }
        }
        return 0;
    }

    uint64_t find_address_end(std::shared_ptr<ET_Program>& program, uint64_t mode)
    {
        for (auto& reg : regions) {
            // Skip regions with unmatched permissions
            if ((reg.permission & mode) != mode) continue;
            if (reg.program == program) {
                return reg.address.end;
            }
        }
        return 0;
    }

    Pair_beg_end find_address(std::shared_ptr<ET_Program>& program, uint64_t mode)
    {
        for (auto& reg : regions) {
            // Skip regions with unmatched permissions
            if ((reg.permission & mode) != mode) continue;
            if (reg.program == program) {
                return reg.address;
            }
        }
        return {};
    }

    void find_and_remove_region(uint64_t address_start, uint64_t address_end)
    {
        cached_exec_regions.erase(std::remove_if(cached_exec_regions.begin(),
                                                 cached_exec_regions.end(),
                                                 [&](Pair_beg_end& p) {
                                                     return (p.beg == address_start)
                                                            && (p.end == address_end);
                                                 }),
                                  cached_exec_regions.end());

        regions.erase(std::remove_if(regions.begin(),
                                     regions.end(),
                                     [&](ET_Region::regionInfo& p) {
                                         return (p.address.beg == address_start)
                                                && (p.address.end == address_end);
                                     }),
                      regions.end());
    }

public:
    typedef struct {
        Pair_beg_end address;
        uint64_t     permission;
        std::string  pathname;
        bool         updatd_flag;
        // Used to count the walk count
        std::vector<uint32_t> walk_count_vector;
        // Target program belonging to this region
        std::shared_ptr<ET_Program> program;
    } regionInfo;

    // Used to identify the mapped virtual address of this program
    std::vector<regionInfo>   regions;
    std::vector<Pair_beg_end> cached_exec_regions;
    // This mutex protects regions and cached_exec_regions
    std::mutex memory_region_lock;
};

#endif
