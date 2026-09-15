#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_DATASETS 8
#define MAX_NAME 64

static void
trim(char *s)
{
	char *p = s;
	size_t len;

	while (*p == ' ' || *p == '\t')
		p++;
	if (p != s)
		memmove(s, p, strlen(p) + 1);

	len = strlen(s);
	while (len > 0 &&
	    (s[len - 1] == '\n' || s[len - 1] == '\r' ||
	    s[len - 1] == ' ' || s[len - 1] == '\t')) {
		s[--len] = '\0';
	}
}

static int
run_wait(char *const argv[])
{
	pid_t pid;
	int status;

	pid = fork();
	if (pid < 0)
		return (-1);
	if (pid == 0) {
		execv(argv[0], argv);
		_exit(127);
	}

	do {
		if (waitpid(pid, &status, 0) < 0) {
			if (errno == EINTR)
				continue;
			return (-1);
		}
		break;
	} while (1);

	if (WIFEXITED(status))
		return (WEXITSTATUS(status));
	if (WIFSIGNALED(status))
		return (128 + WTERMSIG(status));
	return (1);
}

static int
path_is_zfs(const char *path)
{
	struct statfs sfs;

	if (statfs(path, &sfs) != 0)
		return (0);
	return (strcmp(sfs.f_fstypename, "zfs") == 0);
}

static void
ensure_dir(const char *path, mode_t mode)
{
	if (mkdir(path, mode) != 0 && errno != EEXIST)
		return;
	(void)chmod(path, mode);
}

static int
copy_file_if_missing(const char *src, const char *dst, mode_t mode)
{
	char buf[4096];
	int in_fd;
	int out_fd;
	ssize_t n;

	if (access(dst, F_OK) == 0)
		return (0);

	in_fd = open(src, O_RDONLY, 0);
	if (in_fd < 0)
		return (-1);

	out_fd = open(dst, O_WRONLY | O_CREAT | O_EXCL, mode);
	if (out_fd < 0) {
		close(in_fd);
		return (-1);
	}

	while ((n = read(in_fd, buf, sizeof(buf))) > 0) {
		char *p = buf;
		ssize_t remaining = n;

		while (remaining > 0) {
			ssize_t written = write(out_fd, p, (size_t)remaining);
			if (written < 0) {
				close(in_fd);
				close(out_fd);
				return (-1);
			}
			p += written;
			remaining -= written;
		}
	}

	close(in_fd);
	close(out_fd);
	return (n < 0 ? -1 : 0);
}

static char *
next_token(char **cursor)
{
	char *start;
	char *end;

	if (*cursor == NULL)
		return (NULL);

	start = *cursor;
	while (*start == ' ' || *start == '\t' || *start == ',')
		start++;
	if (*start == '\0') {
		*cursor = NULL;
		return (NULL);
	}

	end = start;
	while (*end != '\0' && *end != ' ' && *end != '\t' && *end != ',')
		end++;
	if (*end != '\0') {
		*end = '\0';
		*cursor = end + 1;
	} else {
		*cursor = NULL;
	}

	return (start);
}

static int
load_config(const char *path, char pool[MAX_NAME],
    char datasets[MAX_DATASETS][MAX_NAME], int *dataset_count)
{
	FILE *fp;
	char line[256];

	strlcpy(pool, "pantherazhybrid", MAX_NAME);
	*dataset_count = 0;

	fp = fopen(path, "r");
	if (fp == NULL)
		return (-1);

	while (fgets(line, sizeof(line), fp) != NULL) {
		trim(line);
		if (line[0] == '\0' || line[0] == '#')
			continue;

		if (strncmp(line, "POOL=", 5) == 0) {
			strlcpy(pool, line + 5, MAX_NAME);
			trim(pool);
		} else if (strncmp(line, "DATASETS=", 9) == 0) {
			char *cursor = line + 9;
			char *tok = next_token(&cursor);
			while (tok != NULL && *dataset_count < MAX_DATASETS) {
				strlcpy(datasets[*dataset_count], tok, MAX_NAME);
				trim(datasets[*dataset_count]);
				if (datasets[*dataset_count][0] != '\0')
					(*dataset_count)++;
				tok = next_token(&cursor);
			}
		}
	}

	fclose(fp);

	if (*dataset_count == 0) {
		static const char *defaults[] = {
			"var", "tmp", "Users", "Library"
		};
		for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++) {
			strlcpy(datasets[*dataset_count], defaults[i], MAX_NAME);
			(*dataset_count)++;
		}
	}

	return (pool[0] == '\0' ? -1 : 0);
}

static int
pool_imported(const char *pool)
{
	char *const argv[] = { "/sbin/zpool", "status", (char *)pool, NULL };
	return (run_wait(argv) == 0);
}

static int
import_pool(const char *pool)
{
	char *const argv[] = {
		"/sbin/zpool", "import", "-N", "-d", "/dev", (char *)pool, NULL
	};

	if (pool_imported(pool))
		return (0);

	if (run_wait(argv) == 0)
		return (0);

	return (pool_imported(pool) ? 0 : -1);
}

static int
mount_dataset(const char *pool, const char *name)
{
	char dataset[160];
	char mountpoint[96];
	char *const argv[] = { "/sbin/zfs", "mount", dataset, NULL };

	if (snprintf(dataset, sizeof(dataset), "%s/%s", pool, name) >=
	    (int)sizeof(dataset))
		return (-1);
	if (snprintf(mountpoint, sizeof(mountpoint), "/%s", name) >=
	    (int)sizeof(mountpoint))
		return (-1);

	ensure_dir(mountpoint, strcmp(name, "tmp") == 0 ? 01777 : 0755);
	if (run_wait(argv) != 0 && !path_is_zfs(mountpoint))
		return (-1);
	if (!path_is_zfs(mountpoint))
		return (-1);

	if (strcmp(name, "var") == 0) {
		ensure_dir("/var/run", 0755);
		ensure_dir("/var/log", 0755);
		ensure_dir("/var/db", 0755);
		ensure_dir("/var/db/dhcpclient", 0755);
		ensure_dir("/var/db/launchd.db", 0755);
		ensure_dir("/var/db/launchd.db/com.apple.launchd", 0755);
		ensure_dir("/var/root", 0700);
		ensure_dir("/var/empty", 0755);
	} else if (strcmp(name, "tmp") == 0) {
		(void)chmod("/tmp", 01777);
	} else if (strcmp(name, "Users") == 0) {
		ensure_dir("/Users/Shared", 01777);
	} else if (strcmp(name, "Library") == 0) {
		ensure_dir("/Library/Preferences", 0755);
		ensure_dir("/Library/Preferences/SystemConfiguration", 0755);
		(void)copy_file_if_missing(
		    "/System/Library/PantheraSeed/Library/Preferences/"
		    "SystemConfiguration/preferences.plist",
		    "/Library/Preferences/SystemConfiguration/preferences.plist",
		    0644);
	}

	return (0);
}

int
main(int argc, char **argv)
{
	const char *config_path = argc > 1 ? argv[1] :
	    "/etc/panthera/zfs-hybrid.conf";
	char pool[MAX_NAME];
	char datasets[MAX_DATASETS][MAX_NAME];
	int dataset_count;

	if (load_config(config_path, pool, datasets, &dataset_count) != 0) {
		fprintf(stderr, "PANTHERA_ZFS_HYBRID_CONFIG:FAIL\n");
		return (2);
	}

	fprintf(stderr, "PANTHERA_ZFS_HYBRID_IMPORT:%s\n", pool);
	if (import_pool(pool) != 0) {
		fprintf(stderr, "PANTHERA_ZFS_HYBRID_IMPORT:FAIL\n");
		return (1);
	}

	for (int i = 0; i < dataset_count; i++) {
		if (mount_dataset(pool, datasets[i]) != 0) {
			fprintf(stderr, "PANTHERA_ZFS_HYBRID_MOUNT:%s:FAIL\n",
			    datasets[i]);
			return (1);
		}
		fprintf(stderr, "PANTHERA_ZFS_HYBRID_MOUNT:%s:OK\n", datasets[i]);
	}

	ensure_dir("/var/run", 0755);
	{
		int fd = open("/var/run/panthera_zfs_hybrid_mounted",
		    O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd >= 0) {
			(void)write(fd, pool, strlen(pool));
			(void)write(fd, "\n", 1);
			close(fd);
		}
	}

	fprintf(stderr, "PANTHERA_ZFS_HYBRID_READY\n");
	return (0);
}
