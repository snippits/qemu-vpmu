#include <sys/prctl.h> // prctl

#include "vpmu-utils.hpp" // miscellaneous functions
#include "json.hpp"       // JSON support
#include "vpmu.h"         // VPMU printing/logging system

namespace vpmu
{
namespace utils
{
    void name_process(std::string new_name)
    {
        char process_name[LINUX_NAMELEN] = {0};
#ifdef CONFIG_VPMU_DEBUG_MSG
        if (new_name.size() >= LINUX_NAMELEN) {
            ERR_MSG(
              "Name %s is grater than kernel default name size. It would be truncated!\n",
              new_name.c_str());
        }
#endif
        snprintf(process_name, LINUX_NAMELEN, "%s", new_name.c_str());
        prctl(PR_SET_NAME, process_name);
    }

    void name_thread(std::thread &t, std::string new_name)
    {
        char thread_name[LINUX_NAMELEN] = {0};
#ifdef CONFIG_VPMU_DEBUG_MSG
        if (new_name.size() >= LINUX_NAMELEN) {
            ERR_MSG(
              "Name %s is grater than kernel default name size. It would be truncated!\n",
              new_name.c_str());
        }
#endif
        snprintf(thread_name, LINUX_NAMELEN, "%s", new_name.c_str());
        pthread_setname_np(t.native_handle(), thread_name);
    }

    int32_t clog2(uint32_t x)
    {
        int32_t i;
        for (i = -1; x != 0; i++) x >>= 1;
        return i;
    }

    void print_color_time(const char *str, uint64_t time)
    {
        CONSOLE_LOG(BASH_COLOR_GREEN); // Color Code - Green
        CONSOLE_LOG("%s: %0.3lf ms", str, (double)time / 1000000.0);
        CONSOLE_LOG(BASH_COLOR_NONE "\n\n"); // Terminate Color Code
    }
}
} // End of namespace vpmu::utils
