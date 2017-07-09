#ifndef __VPMU_ET_MEMORY_REGION_HPP_
#define __VPMU_ET_MEMORY_REGION_HPP_
extern "C" {
#include "vpmu/linux-mm.h" // VM_EXEC and other mmap() mode states
}

#include <algorithm> // std::sort
#include <vector>    // std::vector
#include <string>    // std::string
#include <mutex>     // Mutex

#include "et-program.hpp"   // ET_Program class
#include "region-info.hpp"  // RegionInfo class
#include "beg_eng_pair.hpp" // Pair_beg_end class

/// A region can be either region for file or region for named/un-named space
class ET_MemoryRegion : public ET_Path
{
public:
    ET_MemoryRegion() {}
    ~ET_MemoryRegion() {}

    void push_address(uint64_t start_addr, uint64_t end_addr)
    {
        // Do nothing if repeat
        if (this->find_region(start_addr, end_addr)) return;
        RegionInfo r = {};

        r.address = {start_addr, end_addr};
        regions.push_back(r);
    }

    void map_region(std::shared_ptr<ET_Program> prog,
                    uint64_t                    start_addr,
                    uint64_t                    end_addr,
                    uint64_t                    permission,
                    std::string                 pathname)
    {
        RegionInfo r = {};

        r.address    = {start_addr, end_addr};
        r.permission = permission;
        r.pathname   = pathname;
        r.program    = prog;

        // Overwrite repeated/overlapped regions
        this->update(start_addr, end_addr - start_addr, permission);
        RegionInfo& region = this->get(start_addr, end_addr);
        if (region == RegionInfo::not_found) {
            // Create a new entry if update can't help to get new memory range
            regions.push_back(r);
        } else {
            // Update the region info
            region = r;
        }
        this->sort_regions();
        regions_dirty = true;
    }

    void map_region(uint64_t    start_addr,
                    uint64_t    end_addr,
                    uint64_t    permission,
                    std::string pathname)
    {
        this->map_region({}, start_addr, end_addr, permission, pathname);
    }

    void split(uint64_t split_addr, bool tail_reset_rw, bool rebuild_cache_flag)
    {
        auto& region = this->get(split_addr);
        if (region == RegionInfo::not_found) return;
        RegionInfo new_regions_l = region;
        RegionInfo new_regions_r = region;

        // Set rw permission if this region is a tail cut from a bigger one
        if (tail_reset_rw) {
            new_regions_r.permission = VM_READ | VM_WRITE;
        }

        // start_addr <= vaddr <= end_addr
        new_regions_l.address.end = split_addr;
        new_regions_r.address.beg = split_addr;

        this->remove_region(region);
        regions.push_back(new_regions_l);
        regions.push_back(new_regions_r);
        if (rebuild_cache_flag) this->rebuild_cache();
        regions_dirty = true;
    }

    void split(uint64_t split_addr, bool tail_reset_rw)
    {
        this->split(split_addr, tail_reset_rw, true);
    }

    void merge(uint64_t start_addr, uint64_t end_addr, bool rebuild_cache_flag)
    {
        std::vector<Pair_beg_end> temp_addresses = {};
        // Store a sample region from the list
        RegionInfo temp_region = {};

        // Try merge contiguous pages first
        this->merge_contiguous();

        // Find address range included in [start_addr, end_addr]
        for (auto& reg : regions) {
            if (start_addr <= reg.address.beg && reg.address.end <= end_addr) {
                temp_addresses.push_back(reg.address);
                temp_region = reg;
            }
        }

        // Remove these address ranges in this->regions
        for (auto& address : temp_addresses) {
            this->remove_region(address);
        }
        if (temp_region.address.beg != 0) {
            // Update the range of temp_region and push back to this->regions
            temp_region.address.beg = start_addr;
            temp_region.address.end = end_addr;
            this->regions.push_back(temp_region);
        }

        if (rebuild_cache_flag) this->rebuild_cache();
        regions_dirty = true;
    }

    void merge(uint64_t start_addr, uint64_t end_addr)
    {
        this->merge(start_addr, end_addr, true);
    }

    void unmap(uint64_t start_addr, uint64_t end_addr)
    {
        update_mapping_table_for_region(start_addr, end_addr);
        this->remove_region({start_addr, end_addr});
    }

    void update(uint64_t start_addr, uint64_t end_addr, uint64_t mode)
    {
        update_mapping_table_for_region(start_addr, end_addr);
        // Update the permission of target region
        this->get(start_addr, end_addr).permission = mode;
        this->sort_regions();
        // Rebuild exec mode cache
        this->rebuild_cache();
    }

    bool find_exec_region(uint64_t vaddr)
    {
        if (regions_dirty) {
            this->rebuild_cache();
        }
        for (auto& reg : cached_exec_regions) {
            if (reg.beg <= vaddr && vaddr < reg.end) {
                return true;
            }
        }
        return false;
    }

    bool find_region(uint64_t vaddr)
    {
        return (this->get(vaddr) != RegionInfo::not_found);
    }

    bool find_region(uint64_t start_addr, uint64_t end_addr)
    {
        auto& region = this->get(start_addr);
        return (region != RegionInfo::not_found && region.address.end == end_addr);
    }

    Pair_beg_end find_address(std::shared_ptr<ET_Program>& program, uint64_t mode)
    {
        const auto& region = this->get(program, mode);
        if (region == RegionInfo::not_found) return {};
        return region.address;
    }

    void remove_region(RegionInfo& region)
    {
        regions.erase(std::remove_if(regions.begin(),
                                     regions.end(),
                                     [&](RegionInfo& p) { return (&p == &region); }),
                      regions.end());
        regions_dirty = true;
    }

    void remove_region(Pair_beg_end address)
    {
        regions.erase(std::remove_if(regions.begin(),
                                     regions.end(),
                                     [&](RegionInfo& p) {
                                         return (p.address.beg == address.beg)
                                                && (p.address.end == address.end);
                                     }),
                      regions.end());
        regions_dirty = true;
    }

    RegionInfo& get(uint64_t vaddr)
    {
        for (auto& reg : regions) {
            if (reg.address.beg <= vaddr && vaddr < reg.address.end) {
                return reg;
            }
        }
        return RegionInfo::not_found;
    }

    RegionInfo& get(uint64_t start_addr, uint64_t end_addr)
    {
        for (auto& reg : regions) {
            if (reg.address.beg == start_addr && reg.address.end == end_addr) {
                return reg;
            }
        }
        return RegionInfo::not_found;
    }

    RegionInfo& get(Pair_beg_end addr) { return this->get(addr.beg, addr.end); }

    RegionInfo& get(std::shared_ptr<ET_Program> program, uint64_t mode)
    {
        for (auto& reg : regions) {
            // Skip regions with unmatched permissions
            if ((reg.permission & mode) != mode) continue;
            if (reg.program == program) return reg;
        }
        return RegionInfo::not_found;
    }

    void debug_print_vm_map()
    {
        for (auto& reg : regions) {
            std::string out_str   = "";
            std::string prog_name = (reg.program) ? reg.program->name : "";
            out_str += (reg.permission & VM_READ) ? "r" : "-";
            out_str += (reg.permission & VM_WRITE) ? "w" : "-";
            out_str += (reg.permission & VM_EXEC) ? "x" : "-";
            out_str += (reg.permission & VM_SHARED) ? "-" : "p";
            DBG("%16s - %-16s  "
                "%-5s %-40s %-20s"
                "\n",
                vpmu::utils::addr_to_str(reg.address.beg).c_str(),
                vpmu::utils::addr_to_str(reg.address.end).c_str(),
                out_str.c_str(),
                reg.pathname.c_str(),
                prog_name.c_str());
        }
    }

private:
    void sort_regions(void)
    {
        std::sort(regions.begin(), regions.end(), [](RegionInfo a, RegionInfo b) {
            return a.address.beg < b.address.beg;
        });
    }

    void rebuild_cache(void)
    {
        cached_exec_regions.clear();
        for (const auto& reg : regions) {
            if (reg.permission & VM_EXEC) {
                cached_exec_regions.push_back(reg.address);
            }
        }
        regions_dirty = false;
    }

    void merge_contiguous(void)
    {
        std::vector<RegionInfo> temp_region = {};

        // Check size. regions.size() is unsigned, 0-1 would be a big number.
        if (regions.size() < 2) return;
        for (int i = 0; i < regions.size() - 1; i++) {
            auto& p = regions[i];
            auto& n = regions[i + 1];
            if (p.address.end == n.address.beg && p.permission == n.permission
                && p.pathname == n.pathname) {
                p.address.end  = n.address.end;
                n.removed_flag = true;
            }
        }

        regions.erase(std::remove_if(regions.begin(),
                                     regions.end(),
                                     [](RegionInfo& p) { return p.removed_flag; }),
                      regions.end());
        regions_dirty = true;
    }

    inline void update_mapping_table_for_region(uint64_t start_addr, uint64_t end_addr)
    {
        RegionInfo region = this->get(start_addr);

        // Try merge two VM region first...
        this->merge(start_addr, end_addr, false);

        // Then try split regions
        if (region.address.beg != 0 && region.address.beg != start_addr) {
            this->split(start_addr, true, false);
        }
        if (region.address.end != 0 && region.address.end != end_addr) {
            this->split(end_addr, (region.address.beg != start_addr), false);
        }
    }

private:
    std::vector<Pair_beg_end> cached_exec_regions = {};

    bool regions_dirty = true;

public:
    // Used to identify the mapped virtual address of this program
    std::vector<RegionInfo> regions = {};
};

#endif
