#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

extern int panthera_poll(struct pollfd *, nfds_t, int) __asm("_poll");

static int
panthera_compat_ppoll(struct pollfd *fds, nfds_t nfds, int timeout,
    const sigset_t *sigmask)
{
	sigset_t osig;
	int ret, saved_errno;

	if (sigmask != NULL && sigprocmask(SIG_SETMASK, sigmask, &osig) == -1) {
		return -1;
	}
	printf("PANTHERA:PPOLL_COMPAT after-sigprocmask errno=%d\n", errno);
	fflush(stdout);
	ret = panthera_poll(fds, nfds, timeout);
	printf("PANTHERA:PPOLL_COMPAT after-poll ret=%d errno=%d revents0=0x%x\n",
	    ret, errno, nfds > 0 ? fds[0].revents : 0);
	fflush(stdout);
	saved_errno = errno;
	if (sigmask != NULL) {
		(void)sigprocmask(SIG_SETMASK, &osig, NULL);
	}
	errno = saved_errno;
	return ret;
}

static int
run_external_probe(int use_compat_ppoll, int empty_sigmask)
{
	int lfd = -1, afd = -1;
	int one = 1;
	struct sockaddr_in addr;
	struct pollfd pfd;
	sigset_t mask;

	printf("PANTHERA:%s start\n",
	    use_compat_ppoll ? "PPOLL_HOSTFWD" : "POLL_HOSTFWD");
	fflush(stdout);

	lfd = socket(AF_INET, SOCK_STREAM, 0);
	if (lfd < 0) {
		printf("PANTHERA:%s socket errno=%d\n",
		    use_compat_ppoll ? "PPOLL_HOSTFWD" : "POLL_HOSTFWD", errno);
		return 1;
	}
	setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

	memset(&addr, 0, sizeof(addr));
	addr.sin_len = sizeof(addr);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(22);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		printf("PANTHERA:%s bind errno=%d\n",
		    use_compat_ppoll ? "PPOLL_HOSTFWD" : "POLL_HOSTFWD", errno);
		return 1;
	}
	if (listen(lfd, 8) < 0) {
		printf("PANTHERA:%s listen errno=%d\n",
		    use_compat_ppoll ? "PPOLL_HOSTFWD" : "POLL_HOSTFWD", errno);
		return 1;
	}

	memset(&pfd, 0, sizeof(pfd));
	pfd.fd = lfd;
	pfd.events = POLLIN;
	printf("PANTHERA:%s %s enter fd=%d\n",
	    use_compat_ppoll ? "PPOLL_HOSTFWD" : "POLL_HOSTFWD",
	    use_compat_ppoll ? "ppoll" : "poll", lfd);
	fflush(stdout);

	sigemptyset(&mask);
	if (!empty_sigmask) {
		sigaddset(&mask, SIGCHLD);
	}

	int rc = use_compat_ppoll ?
	    panthera_compat_ppoll(&pfd, 1, 45000, &mask) :
	    panthera_poll(&pfd, 1, 45000);
	printf("PANTHERA:%s %s rc=%d errno=%d revents=0x%x\n",
	    use_compat_ppoll ? "PPOLL_HOSTFWD" : "POLL_HOSTFWD",
	    use_compat_ppoll ? "ppoll" : "poll", rc, errno, pfd.revents);
	fflush(stdout);

	if (rc == 1 && (pfd.revents & POLLIN)) {
		afd = accept(lfd, NULL, NULL);
		printf("PANTHERA:%s accept fd=%d errno=%d\n",
		    use_compat_ppoll ? "PPOLL_HOSTFWD" : "POLL_HOSTFWD", afd, errno);
		if (afd >= 0) {
			if (use_compat_ppoll) {
				write(afd, "PANTHERA_PPOLL_HOSTFWD_PASS\n", 28);
			} else {
				write(afd, "PANTHERA_POLL_HOSTFWD_PASS\n", 27);
			}
			close(afd);
		}
	}

	close(lfd);
	if (rc == 1 && (pfd.revents & POLLIN) && afd >= 0) {
		printf("PANTHERA:%s PASS\n",
		    use_compat_ppoll ? "PPOLL_HOSTFWD" : "POLL_HOSTFWD");
		return 0;
	}
	printf("PANTHERA:%s FAIL\n",
	    use_compat_ppoll ? "PPOLL_HOSTFWD" : "POLL_HOSTFWD");
	return 1;
}

int
main(int argc, char **argv)
{
	int lfd = -1, cfd = -1, afd = -1;
	int one = 1;
	struct sockaddr_in addr;
	struct pollfd pfd;
	pid_t child;
	int status = 0;

	if (argc > 1 && strcmp(argv[1], "--external") == 0) {
		return run_external_probe(0, 0);
	}
	if (argc > 1 && strcmp(argv[1], "--external-ppoll") == 0) {
		return run_external_probe(1, 0);
	}
	if (argc > 1 && strcmp(argv[1], "--external-ppoll-empty") == 0) {
		return run_external_probe(1, 1);
	}

	printf("PANTHERA:POLL_LISTEN start\n");
	fflush(stdout);

	lfd = socket(AF_INET, SOCK_STREAM, 0);
	if (lfd < 0) {
		printf("PANTHERA:POLL_LISTEN socket errno=%d\n", errno);
		return 1;
	}
	setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

	memset(&addr, 0, sizeof(addr));
	addr.sin_len = sizeof(addr);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(2022);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		printf("PANTHERA:POLL_LISTEN bind errno=%d\n", errno);
		return 1;
	}
	if (listen(lfd, 8) < 0) {
		printf("PANTHERA:POLL_LISTEN listen errno=%d\n", errno);
		return 1;
	}

	child = fork();
	if (child < 0) {
		printf("PANTHERA:POLL_LISTEN fork errno=%d\n", errno);
		return 1;
	}
	if (child == 0) {
		usleep(250000);
		cfd = socket(AF_INET, SOCK_STREAM, 0);
		if (cfd < 0) {
			_exit(20);
		}
		if (connect(cfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
			_exit(21);
		}
		write(cfd, "x", 1);
		usleep(250000);
		close(cfd);
		_exit(0);
	}

	memset(&pfd, 0, sizeof(pfd));
	pfd.fd = lfd;
	pfd.events = POLLIN;
	printf("PANTHERA:POLL_LISTEN poll enter fd=%d\n", lfd);
	fflush(stdout);

	int rc = panthera_poll(&pfd, 1, 5000);
	printf("PANTHERA:POLL_LISTEN poll rc=%d errno=%d revents=0x%x\n",
	    rc, errno, pfd.revents);
	fflush(stdout);

	if (rc == 1 && (pfd.revents & POLLIN)) {
		afd = accept(lfd, NULL, NULL);
		printf("PANTHERA:POLL_LISTEN accept fd=%d errno=%d\n", afd, errno);
		if (afd >= 0) {
			close(afd);
		}
	}

	waitpid(child, &status, 0);
	printf("PANTHERA:POLL_LISTEN child status=0x%x\n", status);

	close(lfd);
	if (rc == 1 && (pfd.revents & POLLIN) && WIFEXITED(status) &&
	    WEXITSTATUS(status) == 0) {
		printf("PANTHERA:POLL_LISTEN PASS\n");
		return 0;
	}
	printf("PANTHERA:POLL_LISTEN FAIL\n");
	return 1;
}
