#ifndef __VPMU_COMMON_H_
#define __VPMU_COMMON_H_

/* Older versions of C++ don't get definitions of various macros from
 * stdlib.h unless we define these macros before first inclusion of
 * that system header.
 */
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif
#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <stdlib.h>  // standard library
#include <stdio.h>   // standard I/O
#include <stdint.h>  // uint8_t, uint32_t, etc.
#include <time.h>    // time_spec
#include <stdbool.h> // bool, true, false

#include <limits.h>   // Limits of integer types: INT_MAX, ULONG_MAX, etc.
#include <inttypes.h> // Portable number types, ex: PRIu64, etc.
#include <locale.h>   // C localization functions for ' support in printf
#include <string.h>   // For some basic string operations
#include <stdarg.h>   // va_list, va_start, va_arg, va_end
#include <sys/stat.h> // open(const char *path, int oflag, ... );
#include <fcntl.h>    // O_RDONLY, O_WRONLY, O_RDWR

#ifdef __OpenBSD__
#include <sys/signal.h>
#endif

#endif
