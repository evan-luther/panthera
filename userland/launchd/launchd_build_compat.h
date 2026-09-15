#ifndef PANTHERA_LAUNCHD_BUILD_COMPAT_H
#define PANTHERA_LAUNCHD_BUILD_COMPAT_H

#ifndef AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER_BUT_DEPRECATED_IN_MAC_OS_X_VERSION_10_5
#define AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER_BUT_DEPRECATED_IN_MAC_OS_X_VERSION_10_5
#endif

/* XPC export macros — needed by SDK's servers/bootstrap.h */
#ifndef XPC_EXPORT
#define XPC_EXPORT extern
#endif
#ifndef XPC_WARN_RESULT
#define XPC_WARN_RESULT
#endif
#ifndef XPC_NONNULL1
#define XPC_NONNULL1
#endif
#ifndef XPC_NONNULL2
#define XPC_NONNULL2
#endif
#ifndef XPC_NONNULL3
#define XPC_NONNULL3
#endif
#ifndef XPC_NONNULL4
#define XPC_NONNULL4
#endif
#ifndef XPC_NONNULL5
#define XPC_NONNULL5
#endif

#ifndef SO_EXECPATH
#define SO_EXECPATH 0x1085
#endif

#include <string.h>

char *strdup(const char *);
char *strerror(int);
char *strsignal(int);

#endif
