/* Panthera shim: sandbox.h
 * Provides sandbox types and stubs.
 * Panthera has no sandbox — all checks return "allowed".
 */
#ifndef _SANDBOX_H_PANTHERA
#define _SANDBOX_H_PANTHERA

#include <stdint.h>
#include <bsm/libbsm.h>

#define SANDBOX_NAMED 0x0001

enum sandbox_filter_type {
    SANDBOX_FILTER_NONE = 0,
    SANDBOX_FILTER_PATH = 1,
    SANDBOX_FILTER_NOTIFICATION = 2,
    SANDBOX_CHECK_NO_REPORT = 0x40000000,
};

static inline int
sandbox_init(const char *profile, uint64_t flags, char **errorbuf)
{
    (void)profile; (void)flags; (void)errorbuf;
    return 0;
}

static inline void
sandbox_free_error(char *errorbuf)
{
    (void)errorbuf;
}

static inline int
sandbox_check(pid_t pid, const char *operation, int type, ...)
{
    (void)pid; (void)operation; (void)type;
    return 0; /* allowed */
}

static inline int
sandbox_check_by_audit_token(audit_token_t token, const char *operation, ...)
{
    (void)token; (void)operation;
    return 0; /* allowed */
}

#endif /* _SANDBOX_H_PANTHERA */
