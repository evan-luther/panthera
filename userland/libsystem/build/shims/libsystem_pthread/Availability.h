/*
 * Availability.h shim for Panthera Darwin libsystem_pthread build.
 * Replaces the SDK availability macros with no-op definitions.
 */

#ifndef __AVAILABILITY__
#define __AVAILABILITY__

#ifndef __AVAILABILITY_INTERNAL__
#define __AVAILABILITY_INTERNAL__
#endif

#define __API_AVAILABLE(...)
#define __API_DEPRECATED(...)
#define __API_DEPRECATED_WITH_REPLACEMENT(...)
#define __API_UNAVAILABLE(...)
#define __SPI_AVAILABLE(...)

#define API_AVAILABLE(...)
#define API_DEPRECATED(...)
#define API_DEPRECATED_WITH_REPLACEMENT(...)
#define API_UNAVAILABLE(...)
#define SPI_AVAILABLE(...)

#define API_AVAILABLE_BEGIN(...)
#define API_AVAILABLE_END
#define API_UNAVAILABLE_BEGIN(...)
#define API_UNAVAILABLE_END
#define API_DEPRECATED_BEGIN(...)
#define API_DEPRECATED_END
#define SPI_AVAILABLE_BEGIN(...)
#define SPI_AVAILABLE_END

#define __OSX_AVAILABLE(...)
#define __OSX_AVAILABLE_STARTING(...)
#define __OSX_AVAILABLE_BUT_DEPRECATED(...)
#define __OSX_AVAILABLE_BUT_DEPRECATED_MSG(...)
#define __IOS_AVAILABLE(...)
#define __TVOS_AVAILABLE(...)
#define __WATCHOS_AVAILABLE(...)
#define __BRIDGEOS_AVAILABLE(...)
#define __DRIVERKIT_AVAILABLE(...)

#define __OSX_UNAVAILABLE
#define __IOS_UNAVAILABLE
#define __TVOS_UNAVAILABLE
#define __WATCHOS_UNAVAILABLE

#define __IOS_PROHIBITED
#define __TVOS_PROHIBITED
#define __WATCHOS_PROHIBITED

#define __OSX_DEPRECATED(...)
#define __IOS_DEPRECATED(...)
#define __TVOS_DEPRECATED(...)
#define __WATCHOS_DEPRECATED(...)

#define __OS_AVAILABILITY(...)
#define __OS_AVAILABILITY_MSG(...)

#define __SWIFT_UNAVAILABLE(...)
#define __SWIFT_UNAVAILABLE_MSG(...)

#define __API_AVAILABLE_GET_MACRO(...)
#define __API_UNAVAILABLE_GET_MACRO(...)

#define AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER
#define AVAILABLE_MAC_OS_X_VERSION_10_4_AND_LATER
#define AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER
#define AVAILABLE_MAC_OS_X_VERSION_10_6_AND_LATER
#define DEPRECATED_IN_MAC_OS_X_VERSION_10_4_AND_LATER
#define DEPRECATED_IN_MAC_OS_X_VERSION_10_6_AND_LATER

#ifndef __MAC_10_0
#define __MAC_10_0     1000
#define __MAC_10_1     1010
#define __MAC_10_2     1020
#define __MAC_10_3     1030
#define __MAC_10_4     1040
#define __MAC_10_5     1050
#define __MAC_10_6     1060
#define __MAC_10_7     1070
#define __MAC_10_8     1080
#define __MAC_10_9     1090
#define __MAC_10_10    101000
#define __MAC_10_11    101100
#define __MAC_10_12    101200
#define __MAC_10_13    101300
#define __MAC_10_14    101400
#define __MAC_10_15    101500
#define __MAC_10_16    101600
#define __MAC_11_0     110000
#define __MAC_11_1     110100
#define __MAC_11_3     110300
#define __MAC_11_4     110400
#define __MAC_12_0     120000
#define __MAC_12_1     120100
#define __MAC_13_0     130000
#define __MAC_14_0     140000
#endif

#ifndef MAC_OS_X_VERSION_MIN_REQUIRED
#define MAC_OS_X_VERSION_MIN_REQUIRED __MAC_14_0
#endif

#ifndef MAC_OS_X_VERSION_MAX_ALLOWED
#define MAC_OS_X_VERSION_MAX_ALLOWED __MAC_14_0
#endif

#endif /* __AVAILABILITY__ */
