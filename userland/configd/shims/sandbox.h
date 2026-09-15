#ifndef PANTHERA_CONFIGD_SANDBOX_H
#define PANTHERA_CONFIGD_SANDBOX_H

#include <stdint.h>
#include <sys/types.h>

#define SANDBOX_NAMED 0x0001

enum sandbox_filter_type {
    SANDBOX_FILTER_NONE = 0,
    SANDBOX_FILTER_PATH = 1,
    SANDBOX_FILTER_NOTIFICATION = 2,
    SANDBOX_FILTER_GLOBAL_NAME = 3,
    SANDBOX_CHECK_NO_REPORT = 0x40000000
};

static inline int
sandbox_check(pid_t pid, const char *operation, enum sandbox_filter_type type, ...)
{
    (void)pid;
    (void)operation;
    (void)type;
    return 0;
}

#endif /* PANTHERA_CONFIGD_SANDBOX_H */
