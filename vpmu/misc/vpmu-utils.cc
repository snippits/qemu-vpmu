#include <sys/prctl.h> // prctl
#include <sys/ioctl.h> // ioctl
#include <stdexcept>   // exception

#include "vpmu.hpp"       // VPMU common headers
#include "vpmu-utils.hpp" // miscellaneous functions
#include "json.hpp"       // nlohmann::json

#include "vpmu-insn.hpp"   // vpmu_insn_stream
#include "vpmu-cache.hpp"  // vpmu_cache_stream
#include "vpmu-branch.hpp" // vpmu_branch_stream

#include <boost/algorithm/string.hpp> // String processing

// Pointer to argv[0] for modifying process name in htop
extern char *global_argv_0;

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
    void load_linux_env(char *ptr, const char *env_name)
    {
        char *env_str = getenv(env_name);
        if (env_str) strcpy(ptr, env_str);
    }

    std::string get_version_from_vmlinux(const char *file_path)
    {
        char version_string[1024] = {};
        // Open file with read only permission
        int fd = open(file_path, O_RDONLY);
        // Variable to capture return status of file
        struct stat s = {};

        if (fd < 0) {
            ERR_MSG("File %s not found!\n", file_path);
            return "";
        }
        fstat(fd, &s);

        char *content = (char *)mmap(0, s.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

        char *str_beg = NULL;
        for (int i = 0; i < s.st_size; i++) {
            if ((strncmp(&content[i], "Linux version", strlen("Linux version")) == 0)) {
                str_beg = &content[i];
                break;
            }
        }
        if (str_beg == NULL) return "";
        for (int i = 0; i < sizeof(version_string); i++) {
            if (str_beg[i] < ' ') {
                version_string[i] = 0;
                break;
            }
            version_string[i] = str_beg[i];
        }
        DBG(STR_VPMU "Loading Kernel version: %s\n", version_string);

        close(fd);

        return std::string(version_string);
    }

    std::string get_random_hash_name(uint32_t string_length)
    {
        std::string chars("ABCDEF1234567890");
        std::string output = "";

        srand(time(0));
        for (int i = 0; i < string_length; i++) {
            output += chars[rand() % chars.size()];
        }

        return output;
    }

    std::string get_process_name(void)
    {
        char name[32] = {};

        prctl(PR_GET_NAME, name);
        return name;
    }

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
        // This force over-write the cmdline
        strcpy(global_argv_0, process_name);
    }

    void name_thread(std::string new_name)
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

    void print_color_time(const char *str, uint64_t time)
    {
        CONSOLE_LOG(BASH_COLOR_GREEN); // Color Code - Green
        CONSOLE_LOG("%s: %0.3lf ms", str, (double)time / 1000000.0);
        CONSOLE_LOG(BASH_COLOR_NONE "\n\n"); // Terminate Color Code
    }

    uint64_t time_difference(struct timespec *t1, struct timespec *t2)
    {
        uint64_t period = 0;

        // Never return big positive number when using unsigned
        if (t1->tv_sec > t2->tv_sec
            || (t1->tv_sec == t2->tv_sec && t1->tv_nsec >= t2->tv_nsec)) {
            return 0;
        }
        period = t2->tv_nsec - t1->tv_nsec;
        period += (t2->tv_sec - t1->tv_sec) * 1000000000;

        return period;
    }

    nlohmann::json load_json(const char *vpmu_config_file)
    {
        // Read file in
        std::string vpmu_config_str = vpmu::file::read_text_content(vpmu_config_file);

        // Parse json
        auto j = nlohmann::json::parse(vpmu_config_str);
        // DBG("%s\n", j.dump(4).c_str());

        return j;
    }

    int get_tty_columns(void)
    {
        struct winsize w;
        ioctl(0, TIOCGWINSZ, &w);
        return w.ws_col;
    }

    int get_tty_rows(void)
    {
        struct winsize w;
        ioctl(0, TIOCGWINSZ, &w);
        return w.ws_row;
    }

} // End of namespace vpmu::utils

namespace str
{
    std::vector<std::string> split(std::string const &input)
    {
        std::istringstream       buffer(input);
        std::vector<std::string> ret((std::istream_iterator<std::string>(buffer)),
                                     std::istream_iterator<std::string>());
        return ret;
    }

    std::vector<std::string> split(std::string const &input, const char *ch)
    {
        char *txt = strdup(input.c_str());
        // return vector of string
        std::vector<std::string> ret;

        char *pch;
        pch = strtok(txt, ch);
        while (pch != NULL) {
            if (pch[0] == 0) continue;
            ret.push_back(pch);
            pch = strtok(NULL, ch);
        }

        free(txt);
        return ret;
    }

    bool simple_match(std::string path, const std::string pattern)
    {
        if (path.length() == 0 || pattern.length() == 0) return false;
        std::vector<std::string> sub_patterns;
        boost::split(sub_patterns, pattern, boost::is_any_of("*"));

        std::size_t idx = 0;

        for (auto &pat : sub_patterns) {
            if (pat.length() == 0) continue;
            idx = path.find(pat, idx);
            if (idx == std::string::npos) return false;
            idx += pat.length();
        }
        // All sub-patterns are found
        return true;
    }

    std::string addr_to_str(uint64_t addr)
    {
        char tmp_str[32] = {};
        snprintf(tmp_str, sizeof(tmp_str), "%" PRIx64, addr);
        return tmp_str;
    }
} // End of namespace vpmu::str

namespace file
{
    std::string basename(std::string path)
    {
        int index = 0; // Default name of file starts from the first letter

        if (path.length() == 0) return "";
        for (int i = 0; i < path.length(); i++) {
            if (path[i] == '\\') i++;
            if (path[i] == '/') index = i + 1;
        }
        if (index == path.length()) {
            // The path ends with '/' without a file name
            return "";
        }

        return path.substr(index);
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
} // End of namespace vpmu::file

namespace host
{
    uint64_t wall_clock_period(void)
    {
        return vpmu::utils::time_difference(&VPMU.start_time, &VPMU.end_time);
    }

    uint64_t get_timestamp_ns(void)
    {
        struct timespec t_now;
        clock_gettime(CLOCK_REALTIME, &t_now);

        return vpmu::utils::time_difference(&VPMU.program_start_time, &t_now);
    }

    uint64_t get_timestamp_us(void) { return get_timestamp_ns() / 1000; }

    uint64_t get_timestamp_ms(void) { return get_timestamp_ns() / 1000000; }

} // End of namespace vpmu::host

namespace target
{
    double scale_factor(void) { return 1 / (VPMU.platform.cpu.frequency / 1000.0); }

    uint64_t cpu_cycles(void) { return vpmu_insn_stream.get_cycles(); }

    uint64_t branch_cycles(void) { return vpmu_branch_stream.get_cycles(); }

    uint64_t cache_cycles(void) { return vpmu_cache_stream.get_cache_cycles(); }

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
