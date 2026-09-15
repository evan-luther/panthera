#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

static double
now_seconds(void)
{
	struct timeval tv;

	if (gettimeofday(&tv, NULL) != 0)
		return 0.0;
	return (double)tv.tv_sec + ((double)tv.tv_usec / 1000000.0);
}

static void
fill_buffer(char *buffer, size_t size)
{
	size_t i;

	for (i = 0; i < size; i++)
		buffer[i] = (char)('A' + (i % 26));
}

static int
make_path(char *path, size_t path_size, const char *root, int fanout, int i)
{
	if (fanout > 0) {
		return snprintf(path, path_size, "%s/d%04d/f%08d",
		    root, i % fanout, i) >= (int)path_size;
	}
	return snprintf(path, path_size, "%s/f%08d", root, i) >=
	    (int)path_size;
}

static void
print_rate(const char *stage, int count, double elapsed)
{
	double rate = elapsed > 0.0 ? (double)count / elapsed : 0.0;

	printf("PANTHERA_ZFS_SMALLFILE_%s_SECONDS:%.6f\n", stage, elapsed);
	printf("PANTHERA_ZFS_SMALLFILE_%s_RATE:%.2f\n", stage, rate);
}

static int
join_path(char *path, size_t path_size, const char *parent, const char *name)
{
	return snprintf(path, path_size, "%s/%s", parent, name) >=
	    (int)path_size;
}

static int
walk_tree(const char *root, int do_stat, int *entries_out)
{
	char child[PATH_MAX];
	struct dirent *de;
	struct stat st;
	DIR *dir;
	int entries = 0;
	int error = 0;

	dir = opendir(root);
	if (dir == NULL) {
		printf("PANTHERA_ZFS_SMALLFILE_OPENDIR_ERRNO:%s:%d\n",
		    root, errno);
		return 1;
	}

	while ((de = readdir(dir)) != NULL) {
		if (strcmp(de->d_name, ".") == 0 ||
		    strcmp(de->d_name, "..") == 0) {
			continue;
		}
		if (join_path(child, sizeof(child), root, de->d_name)) {
			printf("PANTHERA_ZFS_SMALLFILE_PATH_TOO_LONG\n");
			error = 1;
			break;
		}
		if (lstat(child, &st) != 0) {
			printf("PANTHERA_ZFS_SMALLFILE_LSTAT_ERRNO:%s:%d\n",
			    child, errno);
			error = 1;
			break;
		}
		entries++;
		if (S_ISDIR(st.st_mode)) {
			int child_entries = 0;

			if (walk_tree(child, do_stat, &child_entries) != 0) {
				error = 1;
				break;
			}
			entries += child_entries;
		} else if (do_stat) {
			if (stat(child, &st) != 0) {
				printf("PANTHERA_ZFS_SMALLFILE_STAT_ERRNO:%s:%d\n",
				    child, errno);
				error = 1;
				break;
			}
		}
	}
	if (closedir(dir) != 0 && error == 0) {
		printf("PANTHERA_ZFS_SMALLFILE_CLOSEDIR_ERRNO:%s:%d\n",
		    root, errno);
		error = 1;
	}
	if (error != 0)
		return error;
	*entries_out = entries;
	return 0;
}

static int
create_dirs(const char *root, int fanout, double *elapsed_out)
{
	char path[512];
	double start;
	int i;

	start = now_seconds();
	if (mkdir(root, 0777) != 0 && errno != EEXIST) {
		printf("PANTHERA_ZFS_SMALLFILE_MKROOT_ERRNO:%d\n", errno);
		return 1;
	}
	for (i = 0; i < fanout; i++) {
		if (snprintf(path, sizeof(path), "%s/d%04d", root, i) >=
		    (int)sizeof(path)) {
			printf("PANTHERA_ZFS_SMALLFILE_PATH_TOO_LONG\n");
			return 1;
		}
		if (mkdir(path, 0777) != 0 && errno != EEXIST) {
			printf("PANTHERA_ZFS_SMALLFILE_MKDIR_ERRNO:%d:%d\n",
			    i, errno);
			return 1;
		}
	}
	*elapsed_out = now_seconds() - start;
	return 0;
}

static int
run_probe(const char *root, int count, size_t size, int fanout, int keep_tree)
{
	char path[512];
	char *buffer;
	double start;
	double elapsed;
	double mkdir_elapsed;
	double rmdir_elapsed;
	int walked_entries;
	struct stat st;
	int i;
	int fd;

	buffer = malloc(size == 0 ? 1 : size);
	if (buffer == NULL) {
		printf("PANTHERA_ZFS_SMALLFILE_MALLOC_ERRNO:%d\n", errno);
		return 1;
	}
	fill_buffer(buffer, size);

	printf("PANTHERA_ZFS_SMALLFILE_ROOT:%s\n", root);
	printf("PANTHERA_ZFS_SMALLFILE_COUNT:%d\n", count);
	printf("PANTHERA_ZFS_SMALLFILE_SIZE:%lu\n", (unsigned long)size);
	printf("PANTHERA_ZFS_SMALLFILE_FANOUT:%d\n", fanout);
	printf("PANTHERA_ZFS_SMALLFILE_KEEP:%d\n", keep_tree);

	if (create_dirs(root, fanout, &mkdir_elapsed) != 0) {
		free(buffer);
		return 1;
	}
	print_rate("MKDIR", fanout + 1, mkdir_elapsed);

	start = now_seconds();
	for (i = 0; i < count; i++) {
		if (make_path(path, sizeof(path), root, fanout, i)) {
			printf("PANTHERA_ZFS_SMALLFILE_PATH_TOO_LONG\n");
			free(buffer);
			return 1;
		}
		fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
		if (fd < 0) {
			printf("PANTHERA_ZFS_SMALLFILE_OPEN_ERRNO:%d:%d\n",
			    i, errno);
			free(buffer);
			return 1;
		}
		if (size > 0 && write(fd, buffer, size) != (ssize_t)size) {
			printf("PANTHERA_ZFS_SMALLFILE_WRITE_ERRNO:%d:%d\n",
			    i, errno);
			close(fd);
			free(buffer);
			return 1;
		}
		if (close(fd) != 0) {
			printf("PANTHERA_ZFS_SMALLFILE_CLOSE_ERRNO:%d:%d\n",
			    i, errno);
			free(buffer);
			return 1;
		}
	}
	elapsed = now_seconds() - start;
	print_rate("CREATE", count, elapsed);

	start = now_seconds();
	for (i = 0; i < count; i++) {
		if (make_path(path, sizeof(path), root, fanout, i)) {
			printf("PANTHERA_ZFS_SMALLFILE_PATH_TOO_LONG\n");
			free(buffer);
			return 1;
		}
		if (stat(path, &st) != 0) {
			printf("PANTHERA_ZFS_SMALLFILE_STAT_ERRNO:%d:%d\n",
			    i, errno);
			free(buffer);
			return 1;
		}
	}
	elapsed = now_seconds() - start;
	print_rate("STAT", count, elapsed);

	start = now_seconds();
	walked_entries = 0;
	if (walk_tree(root, 0, &walked_entries) != 0) {
		free(buffer);
		return 1;
	}
	elapsed = now_seconds() - start;
	print_rate("READDIR_LSTAT", walked_entries, elapsed);

	start = now_seconds();
	walked_entries = 0;
	if (walk_tree(root, 1, &walked_entries) != 0) {
		free(buffer);
		return 1;
	}
	elapsed = now_seconds() - start;
	print_rate("READDIR_STAT", walked_entries, elapsed);

	if (keep_tree) {
		free(buffer);
		printf("PANTHERA_ZFS_SMALLFILE_KEEP_TREE\n");
		printf("PANTHERA_ZFS_SMALLFILE_OK\n");
		return 0;
	}

	start = now_seconds();
	for (i = 0; i < count; i++) {
		if (make_path(path, sizeof(path), root, fanout, i)) {
			printf("PANTHERA_ZFS_SMALLFILE_PATH_TOO_LONG\n");
			free(buffer);
			return 1;
		}
		if (unlink(path) != 0) {
			printf("PANTHERA_ZFS_SMALLFILE_UNLINK_ERRNO:%d:%d\n",
			    i, errno);
			free(buffer);
			return 1;
		}
	}
	elapsed = now_seconds() - start;
	print_rate("UNLINK", count, elapsed);

	start = now_seconds();
	for (i = 0; i < fanout; i++) {
		if (snprintf(path, sizeof(path), "%s/d%04d", root, i) >=
		    (int)sizeof(path)) {
			printf("PANTHERA_ZFS_SMALLFILE_PATH_TOO_LONG\n");
			free(buffer);
			return 1;
		}
		(void)rmdir(path);
	}
	(void)rmdir(root);
	rmdir_elapsed = now_seconds() - start;
	print_rate("RMDIR", fanout + 1, rmdir_elapsed);

	free(buffer);
	printf("PANTHERA_ZFS_SMALLFILE_OK\n");
	return 0;
}

int
main(int argc, char **argv)
{
	const char *root = "/tmp/panthera_zfs_smallfile_probe";
	int count = 1000;
	size_t size = 64;
	int fanout = 100;
	int keep_tree = 0;

	if (argc > 1)
		root = argv[1];
	if (argc > 2)
		count = atoi(argv[2]);
	if (argc > 3)
		size = (size_t)strtoul(argv[3], NULL, 10);
	if (argc > 4)
		fanout = atoi(argv[4]);
	if (argc > 5)
		keep_tree = strcmp(argv[5], "keep") == 0;
	if (count <= 0 || fanout < 0) {
		fprintf(stderr,
		    "usage: %s [root] [count] [size] [fanout] [keep]\n",
		    argv[0]);
		return 2;
	}
	return run_probe(root, count, size, fanout, keep_tree);
}
