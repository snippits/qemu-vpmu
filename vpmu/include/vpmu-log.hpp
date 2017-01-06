#ifndef __VPMU_LOG_HPP_
#define __VPMU_LOG_HPP_
extern "C" {
#include "vpmu-log.h"
}
#include <string>   // std::string
#include <iostream> // Basic I/O related C++ header

class VPMULog
{
public:
    VPMULog() { set_name("VPMULOG"); }
    VPMULog(std::string module_name) { set_name(module_name); }
    VPMULog(const char *module_name) { set_name(module_name); }

    void set_name(std::string module_name)
    {
        name     = std::string(module_name);
        int size = LOG_PREFIX_LEN - name.length();
        size     = (size < 0) ? 0 : size;
        spaces   = std::string(size, ' ');
    }

    std::string get_name(void) { return name; }
    void set_name(const char *module_name) { set_name(std::string(module_name)); }

protected:
    std::string name, spaces;

    void log(const char *fmt, ...)
    {
        // Print the prefix of log
        snprintf(o_str,
                 sizeof(o_str),
                 LOG_PREFIX_FORMAT "%s", // Specify the format
                 name.c_str(),
                 spaces.c_str());
        // Use the magic of standard function
        va_list arg;
        va_start(arg, fmt);
        vsnprintf(o_str + strlen(o_str), sizeof(o_str) - strlen(o_str), fmt, arg);
        va_end(arg);
        // Final output
        CONSOLE_LOG("%s\n", o_str);
    }

    void log_debug(const char *fmt, ...)
    {
#ifdef CONFIG_VPMU_DEBUG_MSG
        // Print the prefix of log
        snprintf(o_str,
                 sizeof(o_str),
                 LOG_PREFIX_FORMAT "%s", // Specify the format
                 name.c_str(),
                 spaces.c_str());
        // Use the magic of standard function
        va_list arg;
        va_start(arg, fmt);
        vsnprintf(o_str + strlen(o_str), sizeof(o_str) - strlen(o_str), fmt, arg);
        va_end(arg);
        // Final output
        CONSOLE_LOG("%s\n", o_str);
        fflush(stderr);
#endif
    }

    void log_fatal(const char *fmt, ...)
    {
        // Print the prefix of log
        snprintf(o_str,
                 sizeof(o_str),
                 LOG_PREFIX_FORMAT "%s" BASH_COLOR_RED "FATAL: " BASH_COLOR_NONE,
                 name.c_str(),
                 spaces.c_str());
        // Use the magic of standard function
        va_list arg;
        va_start(arg, fmt);
        vsnprintf(o_str + strlen(o_str), sizeof(o_str) - strlen(o_str), fmt, arg);
        va_end(arg);
        // Final output
        CONSOLE_LOG("%s\n", o_str);
        fflush(stderr);
    }

private:
    // 4KB buffer for printing messages
    char o_str[4096] = {0};
};

#endif
