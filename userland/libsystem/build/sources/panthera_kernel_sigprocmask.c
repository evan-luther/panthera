#include <signal.h>

extern int __pthread_sigmask(int, const sigset_t *, sigset_t *);

int
sigprocmask(int how, const sigset_t *set, sigset_t *oset)
{
	return __pthread_sigmask(how, set, oset);
}
