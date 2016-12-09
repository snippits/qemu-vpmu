#ifndef __VPMU_UTILS_HPP
#define __VPMU_UTILS_HPP
#include <vector>      // std::vector
#include <string>      // std::string
#include <thread>      // std::thread
#include "vpmu-conf.h" // Import the common configurations and QEMU config-host.h
#include "json.hpp"
#include "vpmu-log.hpp"

namespace vpmu
{
namespace utils
{

    void name_process(std::string new_name);
    void name_thread(std::thread &t, std::string new_name);
    int32_t clog2(uint32_t x);

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
}
} // End of namespace vpmu::utils

#endif
