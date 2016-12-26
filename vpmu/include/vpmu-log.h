#ifndef __VPMU_LOG_H_
#define __VPMU_LOG_H_
#include "vpmu-common.h" // Include common headers, macros
#include "vpmu-conf.h"   // Import the common configurations and QEMU config-host.h

#ifdef CONFIG_VPMU_DEBUG_MSG
#define DBG(str, ...)                                                                    \
    do {                                                                                 \
        fprintf(stderr, str, ##__VA_ARGS__);                                             \
        fflush(stderr);                                                                  \
    } while (0)
#else
#define DBG(str, ...)                                                                    \
    {                                                                                    \
    }
#endif

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#define ERR_MSG(str, ...)                                                                \
    do {                                                                                 \
        fprintf(stderr,                                                                  \
                BASH_COLOR_RED "ERROR" BASH_COLOR_NONE "(" BASH_COLOR_PURPLE             \
                               "%s: " BASH_COLOR_GREEN "%d" BASH_COLOR_NONE ") : ",      \
                __FILENAME__,                                                            \
                __LINE__);                                                               \
        fprintf(stderr, str, ##__VA_ARGS__);                                             \
        fflush(stderr);                                                                  \
    } while (0)

#define QEMU_MONITOR_LOG(str, ...)                                                       \
    do {                                                                                 \
        qemu_chr_printf(vpmu_t.vpmu_monitor, str "\r", ##__VA_ARGS__);                   \
        qemu_chr_printf(serial_hds[0], str "\r", ##__VA_ARGS__);                         \
    } while (0)

#define FILE_LOG(str, ...)                                                               \
    do {                                                                                 \
        fprintf(vpmu_log_file, str, ##__VA_ARGS__);                                      \
    } while (0)

#define CONSOLE_LOG(str, ...) fprintf(stderr, str, ##__VA_ARGS__)

// File struct to dump VPMU information
extern FILE *vpmu_log_file;
#endif
