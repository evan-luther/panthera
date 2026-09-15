#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/syscall.h>
#include <sys/mount.h>
#include <unistd.h>

extern ssize_t panthera_plain_read(int, void *, size_t) __asm__("_read");
extern ssize_t panthera_plain_write(int, const void *, size_t) __asm__("_write");

static void
timeout_handler(int signo)
{
	const char message[] = "PANTHERA_ZFS_WRITE_PROBE_TIMEOUT\n";

	(void)signo;
	(void)write(STDOUT_FILENO, message, sizeof(message) - 1);
	_exit(124);
}

static int
probe_write_size(size_t size)
{
	char buffer[16384];
	char path[128];
	ssize_t n;
	int fd;
	int saved_errno;

	if (size > sizeof(buffer)) {
		printf("PANTHERA_ZFS_WRITE_PROBE_TOO_LARGE:%lu\n",
		    (unsigned long)size);
		return 1;
	}

	memset(buffer, 'Z', size);
	snprintf(path, sizeof(path), "/var/tmp/panthera_zfs_write_%lu",
	    (unsigned long)size);

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		printf("PANTHERA_ZFS_WRITE_PROBE_OPEN:%lu:ERRNO:%d\n",
		    (unsigned long)size, errno);
		return 1;
	}

	printf("PANTHERA_ZFS_WRITE_PROBE_SIZE:%lu:ENTER\n",
	    (unsigned long)size);
	alarm(15);
	errno = 0;
	n = write(fd, buffer, size);
	saved_errno = errno;
	alarm(0);
	printf("PANTHERA_ZFS_WRITE_PROBE_SIZE:%lu:RC:%ld:ERRNO:%d\n",
	    (unsigned long)size, (long)n, saved_errno);

	printf("PANTHERA_ZFS_WRITE_PROBE_SIZE:%lu:CLOSE_ENTER\n",
	    (unsigned long)size);
	alarm(15);
	errno = 0;
	if (close(fd) != 0) {
		saved_errno = errno;
		alarm(0);
		printf("PANTHERA_ZFS_WRITE_PROBE_SIZE:%lu:CLOSE_ERRNO:%d\n",
		    (unsigned long)size, saved_errno);
		return 1;
	}
	alarm(0);
	printf("PANTHERA_ZFS_WRITE_PROBE_SIZE:%lu:CLOSE_OK\n",
	    (unsigned long)size);
	return (n == (ssize_t)size) ? 0 : 1;
}

static int
probe_pwrite_size(size_t size, off_t offset)
{
	char buffer[16384];
	char path[128];
	ssize_t n;
	int fd;
	int saved_errno;

	if (size > sizeof(buffer)) {
		printf("PANTHERA_ZFS_PWRITE_PROBE_TOO_LARGE:%lu\n",
		    (unsigned long)size);
		return 1;
	}

	memset(buffer, 'P', size);
	snprintf(path, sizeof(path), "/var/tmp/panthera_zfs_pwrite_%lu_%lld",
	    (unsigned long)size, (long long)offset);

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		printf("PANTHERA_ZFS_PWRITE_PROBE_OPEN:%lu:%lld:ERRNO:%d\n",
		    (unsigned long)size, (long long)offset, errno);
		return 1;
	}

	printf("PANTHERA_ZFS_PWRITE_PROBE_SIZE:%lu:OFFSET:%lld:ENTER\n",
	    (unsigned long)size, (long long)offset);
	alarm(15);
	errno = 0;
	n = pwrite(fd, buffer, size, offset);
	saved_errno = errno;
	alarm(0);
	printf("PANTHERA_ZFS_PWRITE_PROBE_SIZE:%lu:OFFSET:%lld:RC:%ld:ERRNO:%d\n",
	    (unsigned long)size, (long long)offset, (long)n, saved_errno);

	printf("PANTHERA_ZFS_PWRITE_PROBE_SIZE:%lu:OFFSET:%lld:CLOSE_ENTER\n",
	    (unsigned long)size, (long long)offset);
	alarm(15);
	errno = 0;
	if (close(fd) != 0) {
		saved_errno = errno;
		alarm(0);
		printf("PANTHERA_ZFS_PWRITE_PROBE_SIZE:%lu:OFFSET:%lld:CLOSE_ERRNO:%d\n",
		    (unsigned long)size, (long long)offset, saved_errno);
		return 1;
	}
	alarm(0);
	printf("PANTHERA_ZFS_PWRITE_PROBE_SIZE:%lu:OFFSET:%lld:CLOSE_OK\n",
	    (unsigned long)size, (long long)offset);
	return (n == (ssize_t)size) ? 0 : 1;
}

static int
probe_lseek_write_size(size_t size, off_t offset)
{
	char buffer[16384];
	char path[128];
	off_t off;
	ssize_t n;
	int fd;
	int saved_errno;

	if (size > sizeof(buffer)) {
		printf("PANTHERA_ZFS_LSEEK_WRITE_TOO_LARGE:%lu\n",
		    (unsigned long)size);
		return 1;
	}

	memset(buffer, 'L', size);
	snprintf(path, sizeof(path), "/var/tmp/panthera_zfs_lseek_write_%lu_%lld",
	    (unsigned long)size, (long long)offset);

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		printf("PANTHERA_ZFS_LSEEK_WRITE_OPEN:%lu:%lld:ERRNO:%d\n",
		    (unsigned long)size, (long long)offset, errno);
		return 1;
	}

	printf("PANTHERA_ZFS_LSEEK_WRITE_SIZE:%lu:OFFSET:%lld:LSEEK_ENTER\n",
	    (unsigned long)size, (long long)offset);
	alarm(15);
	errno = 0;
	off = lseek(fd, offset, SEEK_SET);
	saved_errno = errno;
	alarm(0);
	printf("PANTHERA_ZFS_LSEEK_WRITE_SIZE:%lu:OFFSET:%lld:LSEEK_RC:%lld:ERRNO:%d\n",
	    (unsigned long)size, (long long)offset, (long long)off,
	    saved_errno);
	if (off == (off_t)-1) {
		close(fd);
		return 1;
	}

	printf("PANTHERA_ZFS_LSEEK_WRITE_SIZE:%lu:OFFSET:%lld:WRITE_ENTER\n",
	    (unsigned long)size, (long long)offset);
	alarm(15);
	errno = 0;
	n = write(fd, buffer, size);
	saved_errno = errno;
	alarm(0);
	printf("PANTHERA_ZFS_LSEEK_WRITE_SIZE:%lu:OFFSET:%lld:WRITE_RC:%ld:ERRNO:%d\n",
	    (unsigned long)size, (long long)offset, (long)n, saved_errno);

	printf("PANTHERA_ZFS_LSEEK_WRITE_SIZE:%lu:OFFSET:%lld:CLOSE_ENTER\n",
	    (unsigned long)size, (long long)offset);
	alarm(15);
	errno = 0;
	if (close(fd) != 0) {
		saved_errno = errno;
		alarm(0);
		printf("PANTHERA_ZFS_LSEEK_WRITE_SIZE:%lu:OFFSET:%lld:CLOSE_ERRNO:%d\n",
		    (unsigned long)size, (long long)offset, saved_errno);
		return 1;
	}
	alarm(0);
	printf("PANTHERA_ZFS_LSEEK_WRITE_SIZE:%lu:OFFSET:%lld:CLOSE_OK\n",
	    (unsigned long)size, (long long)offset);

	return (n == (ssize_t)size) ? 0 : 1;
}

static int
probe_plain_write_size(size_t size)
{
	char buffer[16384];
	char path[128];
	ssize_t n;
	int fd;
	int saved_errno;

	if (size > sizeof(buffer)) {
		printf("PANTHERA_ZFS_PLAIN_WRITE_TOO_LARGE:%lu\n",
		    (unsigned long)size);
		return 1;
	}

	memset(buffer, 'W', size);
	snprintf(path, sizeof(path), "/var/tmp/panthera_zfs_plain_write_%lu",
	    (unsigned long)size);

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		printf("PANTHERA_ZFS_PLAIN_WRITE_OPEN:%lu:ERRNO:%d\n",
		    (unsigned long)size, errno);
		return 1;
	}

	printf("PANTHERA_ZFS_PLAIN_WRITE_SIZE:%lu:ENTER\n",
	    (unsigned long)size);
	alarm(15);
	errno = 0;
	n = panthera_plain_write(fd, buffer, size);
	saved_errno = errno;
	alarm(0);
	printf("PANTHERA_ZFS_PLAIN_WRITE_SIZE:%lu:RC:%ld:ERRNO:%d\n",
	    (unsigned long)size, (long)n, saved_errno);

	printf("PANTHERA_ZFS_PLAIN_WRITE_SIZE:%lu:CLOSE_ENTER\n",
	    (unsigned long)size);
	alarm(15);
	errno = 0;
	if (close(fd) != 0) {
		saved_errno = errno;
		alarm(0);
		printf("PANTHERA_ZFS_PLAIN_WRITE_SIZE:%lu:CLOSE_ERRNO:%d\n",
		    (unsigned long)size, saved_errno);
		return 1;
	}
	alarm(0);
	printf("PANTHERA_ZFS_PLAIN_WRITE_SIZE:%lu:CLOSE_OK\n",
	    (unsigned long)size);
	return (n == (ssize_t)size) ? 0 : 1;
}

static int
probe_plain_read_write_size(size_t size)
{
	char inbuf[16384];
	char outbuf[16384];
	char path[128];
	ssize_t n;
	int fd;
	int saved_errno;

	if (size > sizeof(inbuf)) {
		printf("PANTHERA_ZFS_PLAIN_READ_WRITE_TOO_LARGE:%lu\n",
		    (unsigned long)size);
		return 1;
	}

	memset(inbuf, 'R', size);
	snprintf(path, sizeof(path), "/var/tmp/panthera_zfs_plain_rw_%lu",
	    (unsigned long)size);

	fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		printf("PANTHERA_ZFS_PLAIN_READ_WRITE_OPEN:%lu:ERRNO:%d\n",
		    (unsigned long)size, errno);
		return 1;
	}
	if (write(fd, inbuf, size) != (ssize_t)size ||
	    lseek(fd, 0, SEEK_SET) == (off_t)-1) {
		printf("PANTHERA_ZFS_PLAIN_READ_WRITE_SETUP:%lu:ERRNO:%d\n",
		    (unsigned long)size, errno);
		close(fd);
		return 1;
	}

	printf("PANTHERA_ZFS_PLAIN_READ_SIZE:%lu:ENTER\n",
	    (unsigned long)size);
	alarm(15);
	errno = 0;
	n = panthera_plain_read(fd, outbuf, size);
	saved_errno = errno;
	alarm(0);
	printf("PANTHERA_ZFS_PLAIN_READ_SIZE:%lu:RC:%ld:ERRNO:%d\n",
	    (unsigned long)size, (long)n, saved_errno);

	close(fd);
	return (n == (ssize_t)size) ? 0 : 1;
}

static int
probe_syscall_pwrite_size(size_t size, off_t offset)
{
	char buffer[8192];
	char path[128];
	long n;
	int fd;
	int saved_errno;

	if (size > sizeof(buffer)) {
		printf("PANTHERA_ZFS_SYSCALL_PWRITE_TOO_LARGE:%lu\n",
		    (unsigned long)size);
		return 1;
	}

	memset(buffer, 'S', size);
	snprintf(path, sizeof(path), "/var/tmp/panthera_zfs_sys_pwrite_%lu_%lld",
	    (unsigned long)size, (long long)offset);

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		printf("PANTHERA_ZFS_SYSCALL_PWRITE_OPEN:%lu:%lld:ERRNO:%d\n",
		    (unsigned long)size, (long long)offset, errno);
		return 1;
	}

	printf("PANTHERA_ZFS_SYSCALL_PWRITE_SIZE:%lu:OFFSET:%lld:ENTER\n",
	    (unsigned long)size, (long long)offset);
	alarm(15);
	errno = 0;
	n = syscall(SYS_pwrite, fd, buffer, size, offset);
	saved_errno = errno;
	alarm(0);
	printf("PANTHERA_ZFS_SYSCALL_PWRITE_SIZE:%lu:OFFSET:%lld:RC:%ld:ERRNO:%d\n",
	    (unsigned long)size, (long long)offset, n, saved_errno);

	printf("PANTHERA_ZFS_SYSCALL_PWRITE_SIZE:%lu:OFFSET:%lld:CLOSE_ENTER\n",
	    (unsigned long)size, (long long)offset);
	alarm(15);
	(void)close(fd);
	alarm(0);
	printf("PANTHERA_ZFS_SYSCALL_PWRITE_SIZE:%lu:OFFSET:%lld:CLOSE_OK\n",
	    (unsigned long)size, (long long)offset);

	return (n == (long)size) ? 0 : 1;
}

static int
probe_pipe_nonblock_partial(void)
{
	char buffer[8192];
	fd_set wfds;
	struct timeval tv;
	ssize_t n;
	size_t total = 0;
	int fds[2];
	int flags;
	int rc;
	int saved_errno;

	memset(buffer, 'Q', sizeof(buffer));
	if (pipe(fds) != 0) {
		printf("PANTHERA_PIPE_PROBE_PIPE_ERRNO:%d\n", errno);
		return 1;
	}

	flags = fcntl(fds[1], F_GETFL, 0);
	if (flags < 0 || fcntl(fds[1], F_SETFL, flags | O_NONBLOCK) != 0) {
		printf("PANTHERA_PIPE_PROBE_NONBLOCK_ERRNO:%d\n", errno);
		close(fds[0]);
		close(fds[1]);
		return 1;
	}

	for (;;) {
		errno = 0;
		n = write(fds[1], buffer, 1024);
		saved_errno = errno;
		if (n > 0) {
			total += (size_t)n;
			if (total > 131072) {
				printf("PANTHERA_PIPE_PROBE_FILL_TOO_LARGE:%lu\n",
				    (unsigned long)total);
				close(fds[0]);
				close(fds[1]);
				return 1;
			}
			continue;
		}
		if (n < 0 && (saved_errno == EAGAIN || saved_errno == EWOULDBLOCK)) {
			break;
		}
		printf("PANTHERA_PIPE_PROBE_FILL_RC:%ld:ERRNO:%d\n",
		    (long)n, saved_errno);
		close(fds[0]);
		close(fds[1]);
		return 1;
	}
	printf("PANTHERA_PIPE_PROBE_CAPACITY:%lu\n", (unsigned long)total);

	FD_ZERO(&wfds);
	FD_SET(fds[1], &wfds);
	memset(&tv, 0, sizeof(tv));
	rc = select(fds[1] + 1, NULL, &wfds, NULL, &tv);
	printf("PANTHERA_PIPE_PROBE_SELECT_FULL_RC:%d:SET:%d:ERRNO:%d\n",
	    rc, FD_ISSET(fds[1], &wfds) ? 1 : 0, errno);
	if (rc != 0) {
		close(fds[0]);
		close(fds[1]);
		return 1;
	}

	errno = 0;
	n = read(fds[0], buffer, 4096);
	saved_errno = errno;
	printf("PANTHERA_PIPE_PROBE_DRAIN_RC:%ld:ERRNO:%d\n",
	    (long)n, saved_errno);
	if (n <= 0) {
		close(fds[0]);
		close(fds[1]);
		return 1;
	}

	FD_ZERO(&wfds);
	FD_SET(fds[1], &wfds);
	memset(&tv, 0, sizeof(tv));
	errno = 0;
	rc = select(fds[1] + 1, NULL, &wfds, NULL, &tv);
	saved_errno = errno;
	printf("PANTHERA_PIPE_PROBE_SELECT_DRAINED_RC:%d:SET:%d:ERRNO:%d\n",
	    rc, FD_ISSET(fds[1], &wfds) ? 1 : 0, saved_errno);
	if (rc != 1 || !FD_ISSET(fds[1], &wfds)) {
		close(fds[0]);
		close(fds[1]);
		return 1;
	}

	errno = 0;
	n = write(fds[1], buffer, sizeof(buffer));
	saved_errno = errno;
	printf("PANTHERA_PIPE_PROBE_PARTIAL_WRITE_RC:%ld:ERRNO:%d\n",
	    (long)n, saved_errno);

	close(fds[0]);
	close(fds[1]);
	return (n > 0) ? 0 : 1;
}

int
main(void)
{
	struct sigaction sa;
	struct statfs sfs;
	const char *probe = "/tmp/panthera_zfs_root_statfs_probe";
	int fd;
	int failed = 0;

	setvbuf(stdout, NULL, _IONBF, 0);

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = timeout_handler;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGALRM, &sa, NULL) != 0) {
		printf("PANTHERA_ZFS_WRITE_PROBE_SIGACTION_ERR:%d\n", errno);
		return 1;
	}

	memset(&sfs, 0, sizeof(sfs));
	if (statfs("/", &sfs) != 0) {
		printf("PANTHERA_ZFS_ROOT_STATFS_ERR:%d\n", errno);
		return 1;
	}

	printf("PANTHERA_ZFS_ROOT_FSTYPE:%s\n", sfs.f_fstypename);
	printf("PANTHERA_ZFS_ROOT_MNTON:%s\n", sfs.f_mntonname);
	printf("PANTHERA_ZFS_ROOT_MNTFROM:%s\n", sfs.f_mntfromname);
	printf("PANTHERA_ZFS_ROOT_FLAGS:0x%lx\n", (unsigned long)sfs.f_flags);

	if (strcmp(sfs.f_fstypename, "zfs") != 0) {
		printf("PANTHERA_ZFS_ROOT_STATFS_NOT_ZFS\n");
		return 2;
	}

	fd = open(probe, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		printf("PANTHERA_ZFS_ROOT_WRITE_ERR:%d\n", errno);
		return 3;
	}
	if (write(fd, "zfs-root-write-ok\n", 18) != 18) {
		printf("PANTHERA_ZFS_ROOT_WRITE_SHORT:%d\n", errno);
		close(fd);
		return 4;
	}
	close(fd);

	failed |= probe_write_size(4096);
	failed |= probe_write_size(8192);
	failed |= probe_write_size(16384);
	failed |= probe_plain_write_size(4096);
	failed |= probe_plain_write_size(8192);
	failed |= probe_plain_write_size(16384);
	failed |= probe_plain_read_write_size(4096);
	failed |= probe_plain_read_write_size(8192);
	failed |= probe_plain_read_write_size(16384);
	failed |= probe_lseek_write_size(4096, 0);
	failed |= probe_lseek_write_size(8192, 0);
	failed |= probe_lseek_write_size(8192, 4096);
	failed |= probe_lseek_write_size(16384, 0);
	failed |= probe_lseek_write_size(16384, 8192);
	failed |= probe_syscall_pwrite_size(4096, 0);
	failed |= probe_pwrite_size(4096, 0);
	failed |= probe_pwrite_size(8192, 0);
	failed |= probe_pwrite_size(8192, 4096);
	failed |= probe_pwrite_size(16384, 0);
	failed |= probe_pwrite_size(16384, 8192);
	failed |= probe_pipe_nonblock_partial();
	if (failed != 0) {
		printf("PANTHERA_ZFS_WRITE_PROBE_FAIL\n");
		return 5;
	}

	printf("PANTHERA_ZFS_WRITE_PROBE_OK\n");
	printf("PANTHERA_ZFS_ROOT_STATFS_OK\n");
	return 0;
}
