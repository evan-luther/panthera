#ifndef _PANTHERA_LIBDISPATCH_OS_ASSUMES_H
#define _PANTHERA_LIBDISPATCH_OS_ASSUMES_H

#include <sys/cdefs.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef os_assumes
#define os_assumes(_x) ({ __typeof__(_x) _r = (_x); if (!_r) { } _r; })
#endif
#ifndef os_assert
#define os_assert(_x) assert(_x)
#endif
#ifndef os_assumes_zero
#define os_assumes_zero(_x) ({ __typeof__(_x) _r = (_x); if (_r) { } _r; })
#endif
#ifndef os_assert_zero
#define os_assert_zero(_x) ({ __typeof__(_x) _r = (_x); assert(_r == 0); _r; })
#endif
#ifndef os_crash
#define os_crash(msg) abort()
#endif
#ifndef os_log_error
#define os_log_error(log, fmt, ...) ((void)0)
#endif
#ifndef OS_CRASH_ENABLE_EXPERIMENTAL_LIBTRACE
#define OS_CRASH_ENABLE_EXPERIMENTAL_LIBTRACE 0
#endif

#endif
