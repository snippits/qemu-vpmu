#ifndef __VPMU_UTILS_HPP
#define __VPMU_UTILS_HPP
#include <fstream>      // std::ifstream::pos_type, basic_ifstream
#include <vector>       // std::vector
#include <string>       // std::string
#include <thread>       // std::thread
#include "vpmu-conf.h"  // Import the common configurations and QEMU config-host.h
#include "json.hpp"     // nlohmann::json
#include "vpmu-log.hpp" // VPMULog

namespace vpmu
{
namespace utils
{

    void name_process(std::string new_name);
    void name_thread(std::string new_name);
    void name_thread(std::thread &t, std::string new_name);
    int32_t clog2(uint32_t x);

    inline int get_index_of_file_name(const char *path)
    {
        int index = 0; // Default name of file starts from the first letter
        int i     = 0;

        if (path == nullptr) return -1;
        for (i = 0; path[i] != '\0'; i++) {
            if (path[i] == '/') {
                index = i + 1;
            }
        }
        i -= 1; // Set i to the length of string
        if (index == i) {
            // The path ends with '/' without a file name
            return -1;
        }

        return index;
    }

    inline void json_check_or_exit(nlohmann::json config, std::string field)
    {
        if (config[field] == nullptr) {
            ERR_MSG("\"%s\" does not exist in config file\n", field.c_str());
            exit(EXIT_FAILURE);
        }
    }

    template <typename T>
    auto get_json(nlohmann::json &j, const char *key)
    {
        if (j == nullptr || j[key] == nullptr) {
            ERR_MSG("\"%s\" does not exist in config file\n", key);
            exit(EXIT_FAILURE);
        }

        try {
            return j[key].get<T>();
        } catch (std::domain_error e) {
            ERR_MSG("In field: \"%s\", %s\n", key, e.what());
        }
        // Should only reach here when there is something wrong.
        exit(EXIT_FAILURE);
    }

    std::ifstream::pos_type get_file_size(const char *filename);
    std::string read_text_content(const char *filename);
    std::unique_ptr<char> read_binary_content(const char *filename);
    nlohmann::json load_json(const char *vpmu_config_file);
} // End of namespace vpmu::utils

namespace host
{
    uint64_t wall_clock_period(void);
} // End of namespace vpmu::host

namespace target
{
    double scale_factor(void);
    // Cycles
    uint64_t cpu_cycles(void);
    uint64_t branch_cycles(void);
    uint64_t cache_cycles(void);
    uint64_t in_cpu_cycles(void);
    // Time
    uint64_t cpu_time_ns(void);
    uint64_t branch_time_ns(void);
    uint64_t cache_time_ns(void);
    uint64_t memory_time_ns(void);
    uint64_t io_time_ns(void);
    uint64_t time_ns(void);
} // End of namespace vpmu::target

} // End of namespace vpmu

#endif
