#ifndef _PANTHERA_PLATFORM_SHIMS_H_
#define _PANTHERA_PLATFORM_SHIMS_H_

#include <stdint.h>
#include <stdbool.h>

#ifndef __DARWIN_LITTLE_ENDIAN
#define __DARWIN_LITTLE_ENDIAN 1234
#endif
#ifndef __DARWIN_BIG_ENDIAN
#define __DARWIN_BIG_ENDIAN 4321
#endif
#ifndef __DARWIN_PDP_ENDIAN
#define __DARWIN_PDP_ENDIAN 3412
#endif
#ifndef __DARWIN_BYTE_ORDER
#define __DARWIN_BYTE_ORDER __DARWIN_LITTLE_ENDIAN
#endif

#ifndef __swift_nonisolated_unsafe
#define __swift_nonisolated_unsafe
#endif

#ifndef __OS_AVAILABILITY__
#define __OS_AVAILABILITY__
#endif

#ifndef __AVAILABILITY_INTERNAL__
#define __AVAILABILITY_INTERNAL__
#endif

#undef __API_AVAILABLE
#define __API_AVAILABLE(...)
#undef __API_DEPRECATED
#define __API_DEPRECATED(...)
#undef __API_DEPRECATED_WITH_REPLACEMENT
#define __API_DEPRECATED_WITH_REPLACEMENT(...)
#undef __API_UNAVAILABLE
#define __API_UNAVAILABLE(...)
#undef __SPI_AVAILABLE
#define __SPI_AVAILABLE(...)
#undef API_AVAILABLE
#define API_AVAILABLE(...)
#undef API_DEPRECATED
#define API_DEPRECATED(...)
#undef API_DEPRECATED_WITH_REPLACEMENT
#define API_DEPRECATED_WITH_REPLACEMENT(...)
#undef API_UNAVAILABLE
#define API_UNAVAILABLE(...)
#undef SPI_AVAILABLE
#define SPI_AVAILABLE(...)

#define __OS_CRASHLOG_PRIVATE__
#define OS_BUG_INTERNAL(ac, lib, msg) __builtin_trap()
#define OS_BUG_CLIENT(ac, lib, msg) __builtin_trap()
#define _os_set_crash_log_cause_and_message(ac, msg) ((void)0)
#define _os_set_crash_log_message(msg) ((void)0)
#define CRSetCrashLogMessage(msg) ((void)0)
#define __LIBPLATFORM_CLIENT_CRASH__(rc, msg) __builtin_trap()
#define __LIBPLATFORM_INTERNAL_CRASH__(rc, msg) __builtin_trap()

#include <mach/mach_types.h>
extern mach_port_t mach_task_self_;

#endif /* _PANTHERA_PLATFORM_SHIMS_H_ */
