#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static volatile sig_atomic_t timed_out;

static void
on_alarm(int signo)
{
	(void)signo;
	timed_out = 1;
}

static int
probe_read(const char *path)
{
	char buf[64];
	ssize_t n;
	size_t total = 0;
	int fd;
	int rounds = 0;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		printf("PANTHERA_FILE_EOF_READ_OPEN_ERRNO:%d\n", errno);
		return 1;
	}

	alarm(10);
	while (!timed_out) {
		n = read(fd, buf, sizeof(buf));
		rounds++;
		if (n < 0) {
			printf("PANTHERA_FILE_EOF_READ_ERRNO:%d\n", errno);
			close(fd);
			return 1;
		}
		if (n == 0) {
			alarm(0);
			close(fd);
			printf("PANTHERA_FILE_EOF_READ_TOTAL:%zu\n", total);
			printf("PANTHERA_FILE_EOF_READ_ROUNDS:%d\n", rounds);
			return 0;
		}
		total += (size_t)n;
		if (rounds > 100000) {
			printf("PANTHERA_FILE_EOF_READ_MAX_ROUNDS\n");
			close(fd);
			return 1;
		}
	}

	printf("PANTHERA_FILE_EOF_READ_TIMEOUT:%zu:%d\n", total, rounds);
	close(fd);
	return 1;
}

static int
probe_fgetc(const char *path)
{
	FILE *fp;
	size_t total = 0;
	int c;
	int rounds = 0;

	timed_out = 0;
	fp = fopen(path, "r");
	if (fp == NULL) {
		printf("PANTHERA_FILE_EOF_FOPEN_ERRNO:%d\n", errno);
		return 1;
	}

	alarm(10);
	while (!timed_out) {
		c = fgetc(fp);
		rounds++;
		if (c == EOF) {
			if (ferror(fp)) {
				printf("PANTHERA_FILE_EOF_FGETC_ERROR:%d\n",
				    errno);
				fclose(fp);
				return 1;
			}
			alarm(0);
			fclose(fp);
			printf("PANTHERA_FILE_EOF_FGETC_TOTAL:%zu\n", total);
			printf("PANTHERA_FILE_EOF_FGETC_ROUNDS:%d\n", rounds);
			return 0;
		}
		total++;
		if (rounds > 100000) {
			printf("PANTHERA_FILE_EOF_FGETC_MAX_ROUNDS\n");
			fclose(fp);
			return 1;
		}
	}

	printf("PANTHERA_FILE_EOF_FGETC_TIMEOUT:%zu:%d\n", total, rounds);
	fclose(fp);
	return 1;
}

int
main(int argc, char **argv)
{
	struct stat st;
	const char *path;
	int rc = 0;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <file>\n", argv[0]);
		return 2;
	}
	path = argv[1];

	if (signal(SIGALRM, on_alarm) == SIG_ERR) {
		perror("signal");
		return 1;
	}
	if (stat(path, &st) != 0) {
		printf("PANTHERA_FILE_EOF_STAT_ERRNO:%d\n", errno);
		return 1;
	}
	printf("PANTHERA_FILE_EOF_PATH:%s\n", path);
	printf("PANTHERA_FILE_EOF_SIZE:%lld\n", (long long)st.st_size);

	rc |= probe_read(path);
	rc |= probe_fgetc(path);
	if (rc == 0) {
		printf("PANTHERA_FILE_EOF_OK\n");
	}
	return rc;
}
