#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t saw_sigchld;

static void
say(const char *msg)
{
	(void)write(STDOUT_FILENO, msg, strlen(msg));
}

static void
sigchld_handler(int signo)
{
	(void)signo;
	saw_sigchld = 1;
}

static void
fdset_raw(fd_set *set, int fd)
{
	unsigned char *p = (unsigned char *)set;

	p[fd / 8] |= (unsigned char)(1U << (fd % 8));
}

static int
fdisset_raw(const fd_set *set, int fd)
{
	const unsigned char *p = (const unsigned char *)set;

	return (p[fd / 8] >> (fd % 8)) & 1U;
}

static int
run_immediate_eof(void)
{
	int fds[2], rc;
	fd_set rfds;
	struct timespec ts = { .tv_sec = 2, .tv_nsec = 0 };
	char ch = 0;

	if (pipe(fds) != 0) {
		printf("PSELECT_PIPE immediate pipe errno=%d\n", errno);
		return 1;
	}
	close(fds[1]);

	memset(&rfds, 0, sizeof(rfds));
	fdset_raw(&rfds, fds[0]);
	say("PSELECT_PIPE immediate before\n");
	errno = 0;
	rc = pselect(fds[0] + 1, &rfds, NULL, NULL, &ts, NULL);
	printf("PSELECT_PIPE immediate rc=%d errno=%d isset=%d\n",
	    rc, errno, fdisset_raw(&rfds, fds[0]));
	if (rc > 0) {
		errno = 0;
		rc = (int)read(fds[0], &ch, 1);
		printf("PSELECT_PIPE immediate read=%d errno=%d\n", rc, errno);
	}
	close(fds[0]);
	return 0;
}

static int
run_delayed_eof(void)
{
	int fds[2], rc, status = 0;
	pid_t pid;
	fd_set rfds;
	struct timespec ts = { .tv_sec = 4, .tv_nsec = 0 };
	char ch = 0;

	if (pipe(fds) != 0) {
		printf("PSELECT_PIPE delayed pipe errno=%d\n", errno);
		return 1;
	}

	pid = fork();
	if (pid < 0) {
		printf("PSELECT_PIPE delayed fork errno=%d\n", errno);
		close(fds[0]);
		close(fds[1]);
		return 1;
	}
	if (pid == 0) {
		close(fds[0]);
		say("PSELECT_PIPE child before close\n");
		close(fds[1]);
		say("PSELECT_PIPE child after close\n");
		say("PSELECT_PIPE child before exit\n");
		_exit(0);
	}

	close(fds[1]);
	memset(&rfds, 0, sizeof(rfds));
	fdset_raw(&rfds, fds[0]);
	say("PSELECT_PIPE delayed before\n");
	errno = 0;
	rc = pselect(fds[0] + 1, &rfds, NULL, NULL, &ts, NULL);
	printf("PSELECT_PIPE delayed rc=%d errno=%d isset=%d\n",
	    rc, errno, fdisset_raw(&rfds, fds[0]));
	if (rc > 0) {
		errno = 0;
		rc = (int)read(fds[0], &ch, 1);
		printf("PSELECT_PIPE delayed read=%d errno=%d\n", rc, errno);
	}
	close(fds[0]);
	errno = 0;
	rc = waitpid(pid, &status, WNOHANG);
	printf("PSELECT_PIPE delayed waitpid=%d errno=%d child_status=0x%x\n",
	    rc, errno, status);
	return 0;
}

static int
run_sigchld_pselect(void)
{
	struct sigaction sa;
	sigset_t block, empty;
	struct timespec ts = { .tv_sec = 4, .tv_nsec = 0 };
	pid_t pid;
	int rc, status = 0;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sigchld_handler;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGCHLD, &sa, NULL) != 0) {
		printf("PSELECT_SIGCHLD sigaction errno=%d\n", errno);
		return 1;
	}
	sigemptyset(&block);
	sigaddset(&block, SIGCHLD);
	if (sigprocmask(SIG_BLOCK, &block, NULL) != 0) {
		printf("PSELECT_SIGCHLD sigprocmask errno=%d\n", errno);
		return 1;
	}

	pid = fork();
	if (pid < 0) {
		printf("PSELECT_SIGCHLD fork errno=%d\n", errno);
		return 1;
	}
	if (pid == 0)
		_exit(0);

	sigemptyset(&empty);
	say("PSELECT_SIGCHLD before\n");
	errno = 0;
	rc = pselect(0, NULL, NULL, NULL, &ts, &empty);
	printf("PSELECT_SIGCHLD rc=%d errno=%d saw=%d\n",
	    rc, errno, saw_sigchld);
	errno = 0;
	rc = waitpid(pid, &status, WNOHANG);
	printf("PSELECT_SIGCHLD waitpid=%d errno=%d child_status=0x%x\n",
	    rc, errno, status);
	(void)sigprocmask(SIG_UNBLOCK, &block, NULL);
	return 0;
}

static int
run_sigchld_pipe_pselect(void)
{
	struct sigaction sa;
	sigset_t block, empty;
	struct timespec ts = { .tv_sec = 4, .tv_nsec = 0 };
	int fds[2], rc, status = 0;
	pid_t pid;
	fd_set rfds;
	char ch = 0;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sigchld_handler;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGCHLD, &sa, NULL) != 0) {
		printf("PSELECT_SIGCHLD_PIPE sigaction errno=%d\n", errno);
		return 1;
	}
	sigemptyset(&block);
	sigaddset(&block, SIGCHLD);
	if (sigprocmask(SIG_BLOCK, &block, NULL) != 0) {
		printf("PSELECT_SIGCHLD_PIPE sigprocmask errno=%d\n", errno);
		return 1;
	}
	if (pipe(fds) != 0) {
		printf("PSELECT_SIGCHLD_PIPE pipe errno=%d\n", errno);
		return 1;
	}
	pid = fork();
	if (pid < 0) {
		printf("PSELECT_SIGCHLD_PIPE fork errno=%d\n", errno);
		close(fds[0]);
		close(fds[1]);
		return 1;
	}
	if (pid == 0) {
		close(fds[0]);
		for (volatile int i = 0; i < 10000000; i++) {
		}
		close(fds[1]);
		_exit(0);
	}
	close(fds[1]);
	FD_ZERO(&rfds);
	FD_SET(fds[0], &rfds);
	sigemptyset(&empty);
	say("PSELECT_SIGCHLD_PIPE before\n");
	errno = 0;
	rc = pselect(fds[0] + 1, &rfds, NULL, NULL, &ts, &empty);
	printf("PSELECT_SIGCHLD_PIPE rc=%d errno=%d saw=%d isset=%d\n",
	    rc, errno, saw_sigchld, FD_ISSET(fds[0], &rfds));
	(void)sigprocmask(SIG_UNBLOCK, &block, NULL);
	printf("PSELECT_SIGCHLD_PIPE after_unblock saw=%d\n", saw_sigchld);
	if (rc > 0) {
		errno = 0;
		rc = (int)read(fds[0], &ch, 1);
		printf("PSELECT_SIGCHLD_PIPE read=%d errno=%d\n", rc, errno);
	}
	close(fds[0]);
	errno = 0;
	rc = waitpid(pid, &status, WNOHANG);
	printf("PSELECT_SIGCHLD_PIPE waitpid=%d errno=%d child_status=0x%x\n",
	    rc, errno, status);
	return 0;
}

int
main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	run_immediate_eof();
	run_delayed_eof();
	run_sigchld_pipe_pselect();
	run_sigchld_pselect();
	printf("PSELECT_PIPE_DONE\n");
	return 0;
}
