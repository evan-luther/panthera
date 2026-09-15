/*
 * availability_stub.h
 * Panthera Darwin project - stub out Apple availability macros and
 * provide missing declarations for cross-compilation of libsystem_pthread.
 *
 * Force-included before all other headers.
 */

#ifndef _PANTHERA_PTHREAD_AVAILABILITY_STUB_H
#define _PANTHERA_PTHREAD_AVAILABILITY_STUB_H

#include <stdbool.h>

/* External from libkernel, needed by pthread_mutex.c */
extern bool _os_xbs_chrooted;

/* Double-underscore prefixed versions (used in Apple headers) */
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

/* Non-underscore versions (used in user-facing headers) */
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

/* OS availability macros */
#undef __OSX_AVAILABLE
#define __OSX_AVAILABLE(...)

#undef __IOS_AVAILABLE
#define __IOS_AVAILABLE(...)

#undef __TVOS_AVAILABLE
#define __TVOS_AVAILABLE(...)

#undef __WATCHOS_AVAILABLE
#define __WATCHOS_AVAILABLE(...)

#undef __OSX_DEPRECATED
#define __OSX_DEPRECATED(...)

#undef __IOS_PROHIBITED
#define __IOS_PROHIBITED

#undef __TVOS_PROHIBITED
#define __TVOS_PROHIBITED

#undef __WATCHOS_PROHIBITED
#define __WATCHOS_PROHIBITED

#undef __OS_AVAILABILITY
#define __OS_AVAILABILITY(...)

#undef __OS_AVAILABILITY_MSG
#define __OS_AVAILABILITY_MSG(...)

/* Swift-related availability */
#undef __SWIFT_UNAVAILABLE
#define __SWIFT_UNAVAILABLE(...)

#undef __SWIFT_UNAVAILABLE_MSG
#define __SWIFT_UNAVAILABLE_MSG(...)

/* Availability begin/end (used in Swift interop) */
#undef API_AVAILABLE_BEGIN
#define API_AVAILABLE_BEGIN(...)

#undef API_AVAILABLE_END
#define API_AVAILABLE_END

#undef API_UNAVAILABLE_BEGIN
#define API_UNAVAILABLE_BEGIN(...)

#undef API_UNAVAILABLE_END
#define API_UNAVAILABLE_END

/* Version availability macros from AvailabilityMacros.h */
#undef AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER
#define AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER

#undef AVAILABLE_MAC_OS_X_VERSION_10_4_AND_LATER
#define AVAILABLE_MAC_OS_X_VERSION_10_4_AND_LATER

#undef DEPRECATED_IN_MAC_OS_X_VERSION_10_4_AND_LATER
#define DEPRECATED_IN_MAC_OS_X_VERSION_10_4_AND_LATER

#endif /* _PANTHERA_PTHREAD_AVAILABILITY_STUB_H */
