#include <sys/prctl.h> // prctl
#include <stdexcept>   // exception

#include "vpmu.hpp"       // VPMU common headers
#include "vpmu-utils.hpp" // miscellaneous functions
#include "json.hpp"       // nlohmann::json

#include "vpmu-insn.hpp"   // vpmu_insn_stream
#include "vpmu-cache.hpp"  // vpmu_cache_stream
#include "vpmu-branch.hpp" // vpmu_branch_stream

namespace vpmu
{

namespace math
{
    double l2_norm(const std::vector<double> &u)
    {
        double accum = 0.;
        for (double x : u) {
            accum += x * x;
        }
        return sqrt(accum);
    }

    void normalize(const std::vector<double> &in_v, std::vector<double> &out_v)
    {
        double l2n = l2_norm(in_v);

        if (in_v.size() != out_v.size())
            throw std::out_of_range("Two vectors size does not match");
        for (int i = 0; i < out_v.size(); i++) {
            out_v[i] = in_v[i] / l2n;
        }
        return;
    }

    void normalize(std::vector<double> &vec)
    {
        double l2n = l2_norm(vec);

        for (int i = 0; i < vec.size(); i++) {
            vec[i] /= l2n;
        }
        return;
    }

} // End of namespace vpmu::math

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

    void name_thread(std::string new_name)
    {
        char  thread_name[LINUX_NAMELEN] = {0};
#ifdef CONFIG_VPMU_DEBUG_MSG
        if (new_name.size() >= LINUX_NAMELEN) {
            ERR_MSG(
              "Name %s is grater than kernel default name size. It would be truncated!\n",
              new_name.c_str());
        }
#endif
        snprintf(thread_name, LINUX_NAMELEN, "%s", new_name.c_str());
        pthread_setname_np(pthread_self(), thread_name);
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

    std::ifstream::pos_type get_file_size(const char *filename)
    {
        std::ifstream fin(filename, std::ifstream::ate | std::ifstream::binary);
        if (fin)
            return fin.tellg();
        else
            return 0;
    }

    std::string read_text_content(const char *filename)
    {
        uint64_t size_byte = get_file_size(filename);
        // Open file
        std::ifstream fin(filename, std::ios::in);
        if (fin) {
            std::string contents;
            contents.resize(size_byte);
            fin.read(&contents[0], contents.size());
            return (contents);
        }
        ERR_MSG("File not found: %s\n", filename);
        exit(EXIT_FAILURE);
    }

    std::unique_ptr<char> read_binary_content(const char *filename)
    {
        uint64_t size_byte = get_file_size(filename);
        auto     buffer    = std::make_unique<char>(size_byte);
        // Open file
        std::ifstream fin(filename, std::ios::in | std::ios::binary);
        if (fin) {
            fin.read(buffer.get(), size_byte);
            return buffer;
        }
        ERR_MSG("File not found: %s\n", filename);
        exit(EXIT_FAILURE);
    }

    nlohmann::json load_json(const char *vpmu_config_file)
    {
        // Read file in
        std::string vpmu_config_str = read_text_content(vpmu_config_file);

        // Parse json
        auto j = nlohmann::json::parse(vpmu_config_str);
        // DBG("%s\n", j.dump(4).c_str());

        return j;
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
    double scale_factor(void) { return 1 / (VPMU.platform.cpu.frequency / 1000.0); }

    uint64_t cpu_cycles(void) { return vpmu_insn_stream.get_cycles(0); }

    uint64_t branch_cycles(void) { return vpmu_branch_stream.get_cycles(0); }

    uint64_t cache_cycles(void) { return vpmu_cache_stream.get_cache_cycles(0); }

    uint64_t in_cpu_cycles(void)
    {
        return cpu_cycles()      // CPU core execution time
               + branch_cycles() // Panelties from branch misprediction
               + cache_cycles(); // Panelties from cache misses
    }

    uint64_t cpu_time_ns(void) { return cpu_cycles() * vpmu::target::scale_factor(); }

    uint64_t branch_time_ns(void)
    {
        return branch_cycles() * vpmu::target::scale_factor();
    }

    uint64_t cache_time_ns(void) { return cache_cycles() * vpmu::target::scale_factor(); }

    uint64_t memory_time_ns(void) { return vpmu_cache_stream.get_memory_time_ns(0); }

    uint64_t io_time_ns(void)
    {
        // TODO IO Simulation is not supported yet
        return VPMU.iomem_count * 200 * vpmu::target::scale_factor();
    }

    uint64_t time_ns(void)
    {
        // TODO check if we really need to add idle time? Is it really correct?? Though
        // it's correct when running sleep in guest
        return in_cpu_cycles() * vpmu::target::scale_factor() // In-CPU time
               + memory_time_ns() + io_time_ns()              // Out-of-CPU time
               + VPMU.cpu_idle_time_ns;                       // Extra time
    }
} // End of namespace vpmu::target

} // End of namespace vpmu
