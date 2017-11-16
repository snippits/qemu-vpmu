#ifndef __VPMU_UTILS_HPP_
#define __VPMU_UTILS_HPP_
#pragma once

extern "C" {
#include "vpmu-qemu.h" // VPMU struct
}
#include <fstream>      // std::ifstream::pos_type, basic_ifstream
#include <vector>       // std::vector
#include <string>       // std::string
#include <thread>       // std::thread
#include "vpmu-conf.h"  // Import the common configurations and QEMU config-host.h
#include "json.hpp"     // nlohmann::json
#include "vpmu-log.hpp" // VPMULog

// Include other vpmu utilities
#include "vpmu-math.hpp" // vpmu::math

// A thread local storage for saving the running core id of each thread
extern thread_local uint64_t vpmu_running_core_id;

namespace vpmu
{
inline uint64_t get_core_id(void)
{
    return vpmu_running_core_id;
}

inline void set_core_id(uint64_t core_id)
{
    vpmu_running_core_id = core_id;
}

inline void enable_vpmu_on_core(uint64_t core_id)
{
    if (!VPMU.core[core_id].vpmu_enabled)
        DBG(STR_VPMU "VPMU is now enabled on core %lu\n", core_id);
    VPMU.core[core_id].vpmu_enabled = true;
    // Set the global/general flag as well
    VPMU.enabled = true;
}

inline void disable_vpmu_on_core(uint64_t core_id)
{
    if (VPMU.core[core_id].vpmu_enabled)
        DBG(STR_VPMU "VPMU is now disabled on core %lu\n", core_id);
    VPMU.core[core_id].vpmu_enabled = false;
    // Set the global/general flag as well
    VPMU.enabled = false;
    for (int i = 0; i < VPMU.platform.cpu.cores; i++) {
        VPMU.enabled |= VPMU.core[i].vpmu_enabled;
    }
}

inline void enable_vpmu_on_core(void)
{
    vpmu::enable_vpmu_on_core(get_core_id());
}

inline void disable_vpmu_on_core(void)
{
    vpmu::disable_vpmu_on_core(get_core_id());
}

namespace utils
{
    void load_linux_env(char *ptr, const char *env_name);

    uint64_t getpid(void);

    std::string get_version_from_vmlinux(const char *file_path);
    std::string get_random_hash_name(uint32_t string_length);

    std::string get_process_name(void);
    void name_process(std::string new_name);
    void name_thread(std::string new_name);
    void name_thread(std::thread &t, std::string new_name);

    inline int64_t time_difference(struct timespec *t1, struct timespec *t2)
    {
        uint64_t start_t = t1->tv_nsec + t1->tv_sec * 1e9;
        uint64_t end_t   = t2->tv_nsec + t2->tv_sec * 1e9;
        return end_t - start_t;
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
        } catch (nlohmann::detail::type_error e) {
            ERR_MSG("In field: \"%s\", %s\n", key, e.what());
        }
        // Should only reach here when there is something wrong.
        exit(EXIT_FAILURE);
    }

    template <typename T>
    auto get_json(nlohmann::json &j, const char *key, T default_value)
    {
        if (j == nullptr || j[key] == nullptr) {
            return default_value;
        }

        try {
            return j[key].get<T>();
        } catch (nlohmann::detail::type_error e) {
            ERR_MSG("In field: \"%s\", %s\n", key, e.what());
        }
        // Should only reach here when there is something wrong.
        exit(EXIT_FAILURE);
    }

    nlohmann::json load_json(const char *vpmu_config_file);
    int get_tty_columns(void);
    int get_tty_rows(void);

} // End of namespace vpmu::utils

namespace str
{
    std::vector<std::string> split(std::string const &input);
    std::vector<std::string> split(std::string const &input, const char *ch);
    bool simple_match(std::string path, const std::string pattern);
    std::string addr_to_str(uint64_t addr);
    std::string demangle(std::string sym_name);

    template <class... Args>
    std::string formated(const char *format, Args &&... args)
    {
        char s[4096] = {};
        snprintf(s, sizeof(s), format, std::forward<Args>(args)...);
        return s;
    }
} // End of namespace vpmu::str

namespace file
{
    std::string basename(std::string path);
    std::ifstream::pos_type get_file_size(const char *filename);
    std::string read_text_content(const char *filename);
    std::unique_ptr<char> read_binary_content(const char *filename);
} // End of namespace vpmu::file

namespace host
{
    uint64_t wall_clock_period(void);
    uint64_t timestamp_ns(void);
    uint64_t timestamp_us(void);
    uint64_t timestamp_ms(void);
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
    uint64_t time_us(void);
    uint64_t time_ms(void);
} // End of namespace vpmu::target

} // End of namespace vpmu

#endif
