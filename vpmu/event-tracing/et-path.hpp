#ifndef __VPMU_PATH_HPP_
#define __VPMU_PATH_HPP_

#include <string>    // std::string
#include <vector>    // std::vector
#include <utility>   // std::forward
#include <map>       // std::map
#include <algorithm> // std::remove_if

#include "vpmu.hpp"       // for vpmu-qemu.h and VPMU struct
#include "vpmu-utils.hpp" // miscellaneous functions

// TODO The current implementation of name comparison does not apply well
// when the number of monitored/traced binary is large
// Maybe use formally two names and two paths
class ET_Path
{
public:
    // This function return true to all full_path containing the name,
    // expecially when name is assigned as relative path.
    bool fuzzy_compare_name(std::string full_path)
    {
        // Compare the path first
        if (compare_path(full_path) == true) return true;
        // Compare bash name first
        if (compare_bash_name(full_path) == true) return true;
        // Compare file name latter
        if (compare_file_name(full_path) == true) return true;
        return false;
    }

    bool compare_name(std::string full_path)
    {
        if (path.size() != 0) { // Match full path if the path exists
            return compare_path(full_path);
        } else { // Match only the file name if path does not exist
            // Compare bash name first
            if (compare_bash_name(full_path) == true) return true;
            // Compare file name latter
            if (compare_file_name(full_path) == true) return true;
        }
        return false;
    }

    // Compare only the bash name of the name
    bool compare_bash_name(std::string name_or_path)
    {
        return compare_file_name_and_path(name, name_or_path);
    }

    // Compare only the filename of the name
    bool compare_file_name(std::string name_or_path)
    {
        return compare_file_name_and_path(filename, name_or_path);
    }

    // Compare only the full path
    inline bool compare_path(std::string path_only) { return (path == path_only); }

    void set_name_or_path(std::string& new_name)
    {
        int index = vpmu::utils::get_index_of_file_name(new_name.c_str());
        if (index < 0) return; // Maybe we should throw?
        filename             = new_name.substr(index);
        name                 = new_name.substr(index);
        if (index != 0) path = new_name;
    }

    void set_name(std::string& new_name) { name = new_name; }

public:
    // Used too often, make it public for speed and convenience.
    // if bash_name and its binary name are not the same, bash name would be used
    std::string name;
    // This is the file name in the path
    std::string filename;
    // This is always the true (not symbolic link) path to the file
    std::string path;

private:
    inline bool compare_file_name_and_path(std::string& name, std::string& path)
    {
        int i = name.size() - 1, j = path.size() - 1;
        // Empty string means no file name, return false in this case.
        if (i < 0 || j < 0) return false;
        for (; i >= 0 && j >= 0; i--, j--) {
            if (name[i] != path[j]) return false;
        }
        return true;
    }

    // This compares partial_path to full_path, with relative path awareness
    // Ex:  ../../test_set/matrix and /root/test_set/matrix are considered as true.
    // Ex:  /bin/bash and /usr/bin/bash are considered as false.
    inline bool compare_partial_path(std::string& partial_path, std::string& full_path)
    {
        int first_slash_index = 0;
        int i = partial_path.size() - 1, j = full_path.size() - 1, k = 0;

        // Empty string means no file name, return false in this case.
        if (i < 0 || j < 0) return false;
        // Find the first slash if the partial path is a relative path
        if (partial_path[0] == '.') {
            for (k = 0; k < partial_path.size(); k++) {
                if (partial_path[k] != '/' && partial_path[k] != '.')
                    break; // Break when hit the first character of non relative path
                if (partial_path[k] == '/') {
                    first_slash_index = k;
                }
            }
        } else {
            // If both are absolute path, it should be the same
            if (partial_path.size() != full_path.size()) return false;
        }
        // Compare the path to the root slash of partial_path
        for (; i >= first_slash_index && j >= 0; i--, j--) {
            if (partial_path[i] != full_path[j]) return false;
        }
        return true;
    }
};

#endif
