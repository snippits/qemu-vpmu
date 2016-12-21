#include <sys/prctl.h> // prctl

#include "vpmu-utils.hpp" // miscellaneous functions
#include "json.hpp"       // JSON support
#include "vpmu.h"         // VPMU printing/logging system

#include "vpmu-inst.hpp"   // vpmu_inst_stream
#include "vpmu-cache.hpp"  // vpmu_cache_stream
#include "vpmu-branch.hpp" // vpmu_branch_stream

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

    inline uint64_t time_difference(struct timespec *t1, struct timespec *t2)
    {
        uint64_t period = 0;

        period = t2->tv_nsec - t1->tv_nsec;
        period += (t2->tv_sec - t1->tv_sec) * 1000000000;

        return period;
    }
} // End of namespace vpmu::utils

namespace host
{
    uint64_t wall_clock_period(void)
    {
        return vpmu::utils::time_difference(&VPMU.start_time, &VPMU.end_time);
    }
} // End of namespace vpmu::host

namespace target
{
    double scale_factor(void) { return 1 / (VPMU.cpu_model.frequency / 1000.0); }

    uint64_t memory_cycles(void) { return vpmu_cache_stream.get_memory_cycles(0); }

    uint64_t io_cycles(void) { return VPMU.iomem_count; }

    uint64_t cpu_cycles(void) { return vpmu_inst_stream.get_total_cycle_count(); }

    uint64_t cycles(void)
    {
        return VPMU.ticks + vpmu_cache_stream.get_total_cycles()
               + io_cycles();
    }

    uint64_t memory_time_ns(void)
    {
        /* FIXME...
         * Use last_pipeline_cycle_count to save tmp data
         * as did above by using VPMU.last_ticks.
         */
        return memory_cycles() * vpmu::target::scale_factor();
    }

    uint64_t cpu_time_ns(void)
    {
        /* FIXME...
         * Use last_pipeline_cycle_count to save tmp data
         * as did above by using VPMU.last_ticks.
         */
        return cpu_cycles() * vpmu::target::scale_factor();
    }

    uint64_t io_time_ns(void)
    {
        /* FIXME...
         * Use last_pipeline_cycle_count to save tmp data
         * as did above by using VPMU.last_ticks.
         */
        return io_cycles() * vpmu::target::scale_factor();
    }

    uint64_t time_ns(void)
    {
        // TODO check if we really need to add idle time? Is it really correct?? Though
        // it's correct when running sleep in guest
        return cycles() * vpmu::target::scale_factor() + VPMU.cpu_idle_time_ns;
    }
} // End of namespace vpmu::target

} // End of namespace vpmu
