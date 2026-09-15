#ifndef PANTHERA_SC_PREFIX_H
#define PANTHERA_SC_PREFIX_H

#include <Availability.h>
#include <os/availability.h>

#ifndef __API_AVAILABLE_PLATFORM_bridgeos
#define __API_AVAILABLE_PLATFORM_bridgeos(x) bridgeos,introduced=x
#endif

#ifndef KERNEL_PRIVATE
#define KERNEL_PRIVATE 0
#endif

#ifndef PRIVATE
#define PRIVATE 1
#endif

typedef unsigned int user32_addr_t;
typedef unsigned long long user64_addr_t;

#ifndef CF_ASSUME_NONNULL_BEGIN
#define CF_ASSUME_NONNULL_BEGIN
#endif

#ifndef CF_ASSUME_NONNULL_END
#define CF_ASSUME_NONNULL_END
#endif

#ifndef CF_IMPLICIT_BRIDGING_ENABLED
#define CF_IMPLICIT_BRIDGING_ENABLED
#endif

#ifndef CF_IMPLICIT_BRIDGING_DISABLED
#define CF_IMPLICIT_BRIDGING_DISABLED
#endif

#ifndef CF_RETURNS_RETAINED
#define CF_RETURNS_RETAINED
#endif

#ifndef CF_RETURNS_NOT_RETAINED
#define CF_RETURNS_NOT_RETAINED
#endif

#ifndef CF_CONSUMED
#define CF_CONSUMED
#endif

#ifndef CF_BRIDGED_TYPE
#define CF_BRIDGED_TYPE(_type)
#endif

#ifndef __nullable
#define __nullable
#endif

#ifndef __nonnull
#define __nonnull
#endif

#ifdef API_AVAILABLE
#undef API_AVAILABLE
#endif
#define API_AVAILABLE(...)

#ifdef API_UNAVAILABLE
#undef API_UNAVAILABLE
#endif
#define API_UNAVAILABLE(...)

#ifdef API_DEPRECATED
#undef API_DEPRECATED
#endif
#define API_DEPRECATED(...)

#ifdef API_DEPRECATED_WITH_REPLACEMENT
#undef API_DEPRECATED_WITH_REPLACEMENT
#endif
#define API_DEPRECATED_WITH_REPLACEMENT(...)

#ifdef SPI_AVAILABLE
#undef SPI_AVAILABLE
#endif
#define SPI_AVAILABLE(...)

#ifdef SPI_UNAVAILABLE
#undef SPI_UNAVAILABLE
#endif
#define SPI_UNAVAILABLE(...)

#ifdef SPI_DEPRECATED
#undef SPI_DEPRECATED
#endif
#define SPI_DEPRECATED(...)

#ifdef SPI_DEPRECATED_WITH_REPLACEMENT
#undef SPI_DEPRECATED_WITH_REPLACEMENT
#endif
#define SPI_DEPRECATED_WITH_REPLACEMENT(...)

#include <CoreFoundation/CFMachPort.h>
#include <mach/mach.h>

#ifndef PANTHERA_FILEPORT_T
#define PANTHERA_FILEPORT_T
typedef mach_port_t fileport_t;
int fileport_makeport(int fd, fileport_t *port);
#endif

#ifndef SIOCGCONNINFO
#define SIOCGCONNINFO _IOWR('s', 152, struct so_cinforeq)
#endif

#endif /* PANTHERA_SC_PREFIX_H */
