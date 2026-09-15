#include <errno.h>
#include <signal.h>
#include <sys/signal.h>
#include <stdint.h>

/* Keep in sync with xnu/bsd/sys/signal.h. */
#define SA_VALIDATE_SIGRETURN_FROM_SIGTRAMP 0x0400

extern int __sigaction(int, struct __sigaction * __restrict,
    struct sigaction * __restrict);
extern void _sigtramp(void);

int __in_sigtramp = 0;

int
__platform_sigaction(int sig, const struct sigaction * __restrict nsv,
    struct sigaction * __restrict osv)
{
    struct __sigaction sa;
    struct __sigaction *sap = 0;

    if (sig <= 0 || sig >= NSIG || sig == SIGKILL || sig == SIGSTOP) {
        errno = EINVAL;
        return -1;
    }

    if (nsv) {
        sa.sa_handler = nsv->sa_handler;
        sa.sa_tramp = (void (*)(void *, int, int, siginfo_t *, void *))_sigtramp;
        sa.sa_mask = nsv->sa_mask;
        sa.sa_flags = nsv->sa_flags | SA_VALIDATE_SIGRETURN_FROM_SIGTRAMP;
        sap = &sa;
    }

    return __sigaction(sig, sap, osv);
}

int32_t
OSAtomicAdd32(int32_t amount, volatile int32_t *value)
{
	return __sync_add_and_fetch(value, amount);
}
