#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/mman.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t saw_usr1;
static volatile sig_atomic_t saw_chld;

static void
say(const char *msg)
{
	(void)write(STDOUT_FILENO, msg, strlen(msg));
}

static void
handler(int signo)
{
	if (signo == SIGUSR1) {
		saw_usr1 = 1;
	} else if (signo == SIGCHLD) {
		saw_chld = 1;
	}
}

static int
run_usr1(void)
{
	struct sigaction sa;
	sigset_t block, pending;
	int rc;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handler;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGUSR1, &sa, NULL) != 0) {
		printf("SIGNAL_UNBLOCK usr1 sigaction errno=%d\n", errno);
		return 1;
	}
	sigemptyset(&block);
	sigaddset(&block, SIGUSR1);
	if (sigprocmask(SIG_BLOCK, &block, NULL) != 0) {
		printf("SIGNAL_UNBLOCK usr1 block errno=%d\n", errno);
		return 1;
	}
	say("SIGNAL_UNBLOCK usr1 before kill\n");
	errno = 0;
	rc = kill(getpid(), SIGUSR1);
	printf("SIGNAL_UNBLOCK usr1 kill rc=%d errno=%d saw=%d\n",
	    rc, errno, saw_usr1);
	sigemptyset(&pending);
	if (sigpending(&pending) != 0) {
		printf("SIGNAL_UNBLOCK usr1 sigpending errno=%d\n", errno);
		return 1;
	}
	printf("SIGNAL_UNBLOCK usr1 pending=%d saw=%d\n",
	    sigismember(&pending, SIGUSR1), saw_usr1);
	say("SIGNAL_UNBLOCK usr1 before unblock\n");
	errno = 0;
	rc = sigprocmask(SIG_UNBLOCK, &block, NULL);
	printf("SIGNAL_UNBLOCK usr1 unblock rc=%d errno=%d saw=%d\n",
	    rc, errno, saw_usr1);
	return rc != 0 || !saw_usr1;
}

static int
run_chld(void)
{
	struct sigaction sa;
	sigset_t block, pending;
	pid_t pid;
	int rc, status = 0;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handler;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGCHLD, &sa, NULL) != 0) {
		printf("SIGNAL_UNBLOCK chld sigaction errno=%d\n", errno);
		return 1;
	}
	sigemptyset(&block);
	sigaddset(&block, SIGCHLD);
	if (sigprocmask(SIG_BLOCK, &block, NULL) != 0) {
		printf("SIGNAL_UNBLOCK chld block errno=%d\n", errno);
		return 1;
	}
	say("SIGNAL_UNBLOCK chld before fork\n");
	pid = fork();
	if (pid < 0) {
		printf("SIGNAL_UNBLOCK chld fork errno=%d\n", errno);
		return 1;
	}
	if (pid == 0) {
		_exit(0);
	}
	usleep(100000);
	sigemptyset(&pending);
	if (sigpending(&pending) != 0) {
		printf("SIGNAL_UNBLOCK chld sigpending errno=%d\n", errno);
		return 1;
	}
	printf("SIGNAL_UNBLOCK chld pending=%d saw=%d\n",
	    sigismember(&pending, SIGCHLD), saw_chld);
	say("SIGNAL_UNBLOCK chld before unblock\n");
	errno = 0;
	rc = sigprocmask(SIG_UNBLOCK, &block, NULL);
	printf("SIGNAL_UNBLOCK chld unblock rc=%d errno=%d saw=%d\n",
	    rc, errno, saw_chld);
	errno = 0;
	do {
		rc = (int)waitpid(pid, &status, 0);
	} while (rc == -1 && errno == EINTR);
	printf("SIGNAL_UNBLOCK chld waitpid=%d errno=%d status=0x%x\n",
	    rc, errno, status);
	return rc != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0 ||
	    !saw_chld;
}

static int
run_suspend(void)
{
	/* Put SIGCHLD's bit in the pointer, but not in the mask it points to. */
	const size_t span = 2U << SIGCHLD;
	void *mapping = mmap(NULL, span, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANON, -1, 0);
	if (mapping == MAP_FAILED)
		return 1;
	uintptr_t boundary = ((uintptr_t)mapping + (1U << SIGCHLD) - 1) &
	    ~((uintptr_t)(1U << SIGCHLD) - 1);
	sigset_t *empty = (sigset_t *)(boundary + (1U << (SIGCHLD - 1)));
	sigset_t block, oldmask, restored;
	sigemptyset(empty);
	sigemptyset(&block);
	sigaddset(&block, SIGCHLD);
	if (sigprocmask(SIG_BLOCK, &block, &oldmask) != 0)
		return 1;
	saw_chld = 0;
	if (kill(getpid(), SIGCHLD) != 0)
		return 1;
	/* A broken wrapper leaves SIGCHLD blocked; SIGALRM terminates the probe. */
	alarm(5);
	int rc = sigsuspend(empty);
	int saved_errno = errno;
	alarm(0);
	int mask_rc = sigprocmask(SIG_SETMASK, NULL, &restored);
	int failed = rc != -1 || saved_errno != EINTR || !saw_chld ||
	    mask_rc != 0 || sigismember(&restored, SIGCHLD) != 1;
	printf("SIGNAL_SUSPEND rc=%d errno=%d chld=%d restored=%d result=%s\n",
	    rc, saved_errno, saw_chld, mask_rc == 0 &&
	    sigismember(&restored, SIGCHLD), failed ? "FAIL" : "PASS");
	sigprocmask(SIG_SETMASK, &oldmask, NULL);
	munmap(mapping, span);
	return failed;
}

int
main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	if (run_usr1() || run_chld() || run_suspend())
		return 1;
	printf("SIGNAL_UNBLOCK_DONE\n");
	return 0;
}
