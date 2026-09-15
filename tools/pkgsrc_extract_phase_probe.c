#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

struct phase_result {
	int exited;
	int status;
	double wall_seconds;
	double user_seconds;
	double sys_seconds;
	long maxrss;
	unsigned long long lines;
};

struct probe_config {
	const char *tarball;
	const char *root;
	const char *member;
	const char *compression;
	const char *account_helper;
	const char *zpool;
	int no_metadata;
	int cleanup;
	int account_repeat;
};

static double
now_seconds(void)
{
	struct timeval tv;

	if (gettimeofday(&tv, NULL) != 0)
		return 0.0;
	return (double)tv.tv_sec + ((double)tv.tv_usec / 1000000.0);
}

static double
timeval_seconds(const struct timeval *tv)
{
	return (double)tv->tv_sec + ((double)tv->tv_usec / 1000000.0);
}

static int
status_code(int status)
{
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	if (WIFSIGNALED(status))
		return 128 + WTERMSIG(status);
	return 255;
}

static void
print_result(const char *phase, const struct phase_result *result)
{
	printf("PANTHERA_PKGSRC_PHASE_%s_RC:%d\n", phase,
	    result->status);
	printf("PANTHERA_PKGSRC_PHASE_%s_WALL_SECONDS:%.6f\n", phase,
	    result->wall_seconds);
	printf("PANTHERA_PKGSRC_PHASE_%s_USER_SECONDS:%.6f\n", phase,
	    result->user_seconds);
	printf("PANTHERA_PKGSRC_PHASE_%s_SYS_SECONDS:%.6f\n", phase,
	    result->sys_seconds);
	printf("PANTHERA_PKGSRC_PHASE_%s_MAXRSS:%ld\n", phase,
	    result->maxrss);
	if (result->lines != 0) {
		printf("PANTHERA_PKGSRC_PHASE_%s_LINES:%llu\n", phase,
		    result->lines);
	}
	fflush(stdout);
}

static int
wait_child(pid_t pid, double start, struct phase_result *result)
{
	struct rusage usage;
	int status;

	memset(&usage, 0, sizeof(usage));
	if (wait4(pid, &status, 0, &usage) < 0)
		return errno;
	result->exited = 1;
	result->status = status_code(status);
	result->wall_seconds = now_seconds() - start;
	result->user_seconds = timeval_seconds(&usage.ru_utime);
	result->sys_seconds = timeval_seconds(&usage.ru_stime);
	result->maxrss = usage.ru_maxrss;
	return 0;
}

static int
run_argv(const char *phase, char *const argv[], int stdout_fd,
    struct phase_result *result)
{
	double start;
	pid_t pid;
	int error;

	memset(result, 0, sizeof(*result));
	start = now_seconds();
	pid = fork();
	if (pid < 0)
		return errno;
	if (pid == 0) {
		if (stdout_fd >= 0 && stdout_fd != STDOUT_FILENO) {
			if (dup2(stdout_fd, STDOUT_FILENO) < 0)
				_exit(126);
		}
		execv(argv[0], argv);
		_exit(errno == ENOENT ? 127 : 126);
	}
	error = wait_child(pid, start, result);
	print_result(phase, result);
	return error != 0 ? error : result->status;
}

static int
run_count(const struct probe_config *cfg)
{
	struct phase_result result;
	struct rusage usage;
	const char *tar_args[16];
	char buffer[8192];
	double start;
	pid_t pid;
	ssize_t got;
	int argc = 0;
	int pipefd[2];
	int status;
	unsigned long long lines = 0;
	int last_was_newline = 1;

	if (pipe(pipefd) != 0)
		return errno;

	tar_args[argc++] = "/usr/bin/tar";
	tar_args[argc++] = strcmp(cfg->compression, "none") == 0 ? "-tf" : "-tzf";
	tar_args[argc++] = cfg->tarball;
	if (cfg->member != NULL)
		tar_args[argc++] = cfg->member;
	tar_args[argc] = NULL;

	memset(&result, 0, sizeof(result));
	start = now_seconds();
	pid = fork();
	if (pid < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		return errno;
	}
	if (pid == 0) {
		close(pipefd[0]);
		if (dup2(pipefd[1], STDOUT_FILENO) < 0)
			_exit(126);
		close(pipefd[1]);
		execv(tar_args[0], (char *const *)tar_args);
		_exit(errno == ENOENT ? 127 : 126);
	}
	close(pipefd[1]);
	while ((got = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
		ssize_t i;

		for (i = 0; i < got; i++) {
			if (buffer[i] == '\n') {
				lines++;
				last_was_newline = 1;
			} else {
				last_was_newline = 0;
			}
		}
	}
	close(pipefd[0]);
	if (!last_was_newline)
		lines++;

	memset(&usage, 0, sizeof(usage));
	if (wait4(pid, &status, 0, &usage) < 0)
		return errno;
	result.exited = 1;
	result.status = status_code(status);
	result.wall_seconds = now_seconds() - start;
	result.user_seconds = timeval_seconds(&usage.ru_utime);
	result.sys_seconds = timeval_seconds(&usage.ru_stime);
	result.maxrss = usage.ru_maxrss;
	result.lines = lines;
	print_result("COUNT", &result);
	return result.status;
}

static int
run_tar_stream(const struct probe_config *cfg)
{
	struct phase_result result;
	const char *tar_args[16];
	int argc = 0;
	int nullfd;
	int rc;

	nullfd = open("/dev/null", O_WRONLY);
	if (nullfd < 0)
		return errno;

	tar_args[argc++] = "/usr/bin/tar";
	if (cfg->no_metadata) {
		tar_args[argc++] = "--no-xattrs";
		tar_args[argc++] = "--no-acls";
		tar_args[argc++] = "--no-mac-metadata";
		tar_args[argc++] = "--no-same-owner";
		tar_args[argc++] = "--numeric-owner";
	}
	tar_args[argc++] = strcmp(cfg->compression, "none") == 0 ? "-xOf" : "-xOzf";
	tar_args[argc++] = cfg->tarball;
	if (cfg->member != NULL)
		tar_args[argc++] = cfg->member;
	tar_args[argc] = NULL;

	rc = run_argv("STREAM", (char *const *)tar_args, nullfd, &result);
	close(nullfd);
	return rc;
}

static int
run_tar_extract(const struct probe_config *cfg)
{
	struct phase_result result;
	const char *tar_args[20];
	int argc = 0;

	tar_args[argc++] = "/usr/bin/tar";
	if (cfg->no_metadata) {
		tar_args[argc++] = "--no-xattrs";
		tar_args[argc++] = "--no-acls";
		tar_args[argc++] = "--no-mac-metadata";
		tar_args[argc++] = "--no-same-owner";
		tar_args[argc++] = "--numeric-owner";
	}
	tar_args[argc++] = strcmp(cfg->compression, "none") == 0 ? "-xf" : "-xzf";
	tar_args[argc++] = cfg->tarball;
	tar_args[argc++] = "-C";
	tar_args[argc++] = cfg->root;
	if (cfg->member != NULL)
		tar_args[argc++] = cfg->member;
	tar_args[argc] = NULL;

	return run_argv("EXTRACT", (char *const *)tar_args, -1, &result);
}

static int
run_simple(const char *phase, const char *arg0, const char *arg1,
    const char *arg2, const char *arg3)
{
	struct phase_result result;
	char *const argv[] = {
		(char *)arg0,
		(char *)arg1,
		(char *)arg2,
		(char *)arg3,
		NULL
	};

	return run_argv(phase, argv, -1, &result);
}

static int
account_root_path(char *path, size_t path_size, const struct probe_config *cfg)
{
	char member[1024];
	size_t len;

	if (cfg->member == NULL) {
		return snprintf(path, path_size, "%s/pkgsrc", cfg->root) >=
		    (int)path_size;
	}
	len = strlen(cfg->member);
	if (len >= sizeof(member))
		return 1;
	memcpy(member, cfg->member, len + 1);
	while (len > 0 && member[len - 1] == '/') {
		member[--len] = '\0';
	}
	return snprintf(path, path_size, "%s/%s", cfg->root, member) >=
	    (int)path_size;
}

static int
run_account(const struct probe_config *cfg)
{
	struct phase_result result;
	char account_root[4096];
	int i;
	int rc;

	if (cfg->account_helper == NULL)
		return 0;
	if (account_root_path(account_root, sizeof(account_root), cfg) != 0) {
		printf("PANTHERA_PKGSRC_PHASE_ACCOUNT_PATH_TOO_LONG\n");
		return 1;
	}
	printf("PANTHERA_PKGSRC_PHASE_ACCOUNT_ROOT:%s\n", account_root);
	fflush(stdout);
	for (i = 0; i < cfg->account_repeat; i++) {
		char phase[64];
		char *const argv[] = {
			(char *)cfg->account_helper,
			account_root,
			"keep",
			"0",
			NULL
		};

		if (cfg->account_repeat == 1) {
			snprintf(phase, sizeof(phase), "ACCOUNT");
		} else {
			snprintf(phase, sizeof(phase), "ACCOUNT_%d", i + 1);
		}
		rc = run_argv(phase, argv, -1, &result);
		if (rc != 0)
			return rc;
	}
	return 0;
}

static void
usage(const char *prog)
{
	fprintf(stderr,
	    "Usage: %s --tarball PATH --root DIR [options]\n"
	    "Options:\n"
	    "  --compression gz|none\n"
	    "  --member PATH\n"
	    "  --account-helper PATH\n"
	    "  --account-repeat N\n"
	    "  --zpool NAME\n"
	    "  --no-metadata          Skip pkgsrc-irrelevant archive metadata; this is the default.\n"
	    "  --preserve-metadata    Restore archive metadata exactly.\n"
	    "  --cleanup\n",
	    prog);
}

int
main(int argc, char **argv)
{
	struct probe_config cfg;
	int i;
	int rc;

	memset(&cfg, 0, sizeof(cfg));
	cfg.compression = "gz";
	cfg.account_repeat = 1;
	cfg.no_metadata = 1;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--tarball") == 0 && i + 1 < argc) {
			cfg.tarball = argv[++i];
		} else if (strcmp(argv[i], "--root") == 0 && i + 1 < argc) {
			cfg.root = argv[++i];
		} else if (strcmp(argv[i], "--compression") == 0 && i + 1 < argc) {
			cfg.compression = argv[++i];
		} else if (strcmp(argv[i], "--member") == 0 && i + 1 < argc) {
			cfg.member = argv[++i];
		} else if (strcmp(argv[i], "--account-helper") == 0 && i + 1 < argc) {
			cfg.account_helper = argv[++i];
		} else if (strcmp(argv[i], "--account-repeat") == 0 && i + 1 < argc) {
			cfg.account_repeat = atoi(argv[++i]);
			if (cfg.account_repeat < 1)
				cfg.account_repeat = 1;
		} else if (strcmp(argv[i], "--zpool") == 0 && i + 1 < argc) {
			cfg.zpool = argv[++i];
		} else if (strcmp(argv[i], "--no-metadata") == 0) {
			cfg.no_metadata = 1;
		} else if (strcmp(argv[i], "--preserve-metadata") == 0) {
			cfg.no_metadata = 0;
		} else if (strcmp(argv[i], "--cleanup") == 0) {
			cfg.cleanup = 1;
		} else {
			usage(argv[0]);
			return 2;
		}
	}

	if (cfg.tarball == NULL || cfg.root == NULL ||
	    (strcmp(cfg.compression, "gz") != 0 &&
	    strcmp(cfg.compression, "none") != 0)) {
		usage(argv[0]);
		return 2;
	}

	printf("PANTHERA_PKGSRC_PHASE_TARBALL:%s\n", cfg.tarball);
	printf("PANTHERA_PKGSRC_PHASE_ROOT:%s\n", cfg.root);
	printf("PANTHERA_PKGSRC_PHASE_COMPRESSION:%s\n", cfg.compression);
	printf("PANTHERA_PKGSRC_PHASE_MEMBER:%s\n",
	    cfg.member != NULL ? cfg.member : "");
	printf("PANTHERA_PKGSRC_PHASE_NO_METADATA:%d\n", cfg.no_metadata);
	fflush(stdout);

	(void)run_simple("PRE_CLEAN", "/bin/rm", "-rf", cfg.root, NULL);
	rc = run_simple("MKDIR", "/bin/mkdir", "-p", cfg.root, NULL);
	if (rc != 0)
		return rc;
	if ((rc = run_count(&cfg)) != 0)
		return rc;
	if ((rc = run_tar_stream(&cfg)) != 0)
		return rc;
	if ((rc = run_tar_extract(&cfg)) != 0)
		return rc;
	(void)run_simple("SYNC", "/sbin/sync", NULL, NULL, NULL);
	if (cfg.zpool != NULL) {
		(void)run_simple("ZPOOL_SYNC", "/sbin/zpool", "sync",
		    cfg.zpool, NULL);
	}
	if ((rc = run_account(&cfg)) != 0)
		return rc;
	if (cfg.cleanup) {
		rc = run_simple("CLEANUP", "/bin/rm", "-rf", cfg.root, NULL);
		if (rc != 0)
			return rc;
		(void)run_simple("CLEANUP_SYNC", "/sbin/sync", NULL, NULL, NULL);
	}

	printf("PANTHERA_PKGSRC_PHASE_PROBE_OK\n");
	return 0;
}
