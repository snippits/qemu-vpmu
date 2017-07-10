#ifndef __VPMU_PROGRAM_HPP_
#define __VPMU_PROGRAM_HPP_

#include <string>    // std::string
#include <vector>    // std::vector
#include <utility>   // std::forward
#include <map>       // std::map
#include <algorithm> // std::remove_if

#include "vpmu.hpp"         // VPMU common headers
#include "vpmu-utils.hpp"   // miscellaneous functions
#include "et-path.hpp"      // ET_Path class
#include "beg_eng_pair.hpp" // Pair_beg_end class

// TODO VPMU timing model switch
class ET_Program : public ET_Path
{
public:
    ET_Program(std::string new_name) { set_name_or_path(new_name); }
    ~ET_Program() {}

    inline bool operator==(const ET_Program& rhs) { return (this == &rhs); }
    inline bool operator!=(const ET_Program& rhs) { return !(this == &rhs); }

    void add_symbol(std::string name, uint64_t address)
    {
        std::lock_guard<std::mutex> lock(program_lock);
        sym_table.insert(std::pair<std::string, uint64_t>(name, address));
    }

    void push_binary(std::shared_ptr<ET_Program>& program)
    {
        std::lock_guard<std::mutex> lock(program_lock);
        if (program.get() == this) return; // No self include
        // Check repeated pointer
        for (auto& binary : library_list) {
            if (binary == program) return;
        }
        if (program != nullptr) library_list.push_back(program);
    }

    std::string find_code_line_number(uint64_t pc);

public:
    // Caution! All the data should be process independent!!!
    // An ET_Program instance could be shared by multiple processes.
    // All process dependent information should be stored in ET_Process

    // The timing model bind to this program
    uint64_t timing_model;
    // The section address table
    std::map<std::string, Pair_beg_end> section_table;
    // The function address table
    std::map<std::string, uint64_t> sym_table;
    // The dwarf file and line table
    std::map<uint64_t, std::string> line_table;
    // Lists of shared pointer objects of dependent libraries
    std::vector<std::shared_ptr<ET_Program>> library_list;
    // Used to identify the top process parent
    bool is_shared_library = false;
    // This mutex protects whole ET_Program
    std::mutex program_lock;
};

#endif
