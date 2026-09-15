/* Panthera shim: sandbox/private.h
 * No sandbox enforcement on Panthera.
 */
#ifndef _SANDBOX_PRIVATE_H
#define _SANDBOX_PRIVATE_H

#include <stdint.h>
#include <unistd.h>

#define SANDBOX_FILTER_PATH 1
#define SANDBOX_CHECK_NO_REPORT 0x0001

static inline int
sandbox_check(pid_t pid, const char *operation, int type, ...)
{
    (void)pid;
    (void)operation;
    (void)type;
    return 0; /* 0 = allowed */
}

#endif /* _SANDBOX_PRIVATE_H */
