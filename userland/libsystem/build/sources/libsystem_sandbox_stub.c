/* Stub libsystem_sandbox — always allow, no enforcement. */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

int sandbox_init(const char *profile, uint64_t flags, char **errorbuf) {
    (void)profile;
    (void)flags;
    if (errorbuf != NULL)
        *errorbuf = NULL;
    return 0;
}

int sandbox_check(pid_t pid, const char *op, int type, ...) {
    (void)pid;
    (void)op;
    (void)type;
    return 0;
}

void sandbox_free_error(char *errorbuf) {
    (void)errorbuf;
}
