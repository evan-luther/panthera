#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

struct profile_config {
	const char *root;
	int files;
	int leaf_dirs;
	int symlinks;
	int file_size;
	int group_size;
	int keep_tree;
};

static double
now_seconds(void)
{
	struct timeval tv;

	if (gettimeofday(&tv, NULL) != 0)
		return 0.0;
	return (double)tv.tv_sec + ((double)tv.tv_usec / 1000000.0);
}

static int
group_count(const struct profile_config *cfg)
{
	return (cfg->leaf_dirs + cfg->group_size - 1) / cfg->group_size;
}

static int
make_group_path(char *path, size_t path_size, const struct profile_config *cfg,
    int group)
{
	return snprintf(path, path_size, "%s/g%04d", cfg->root, group) >=
	    (int)path_size;
}

static int
make_dir_path(char *path, size_t path_size, const struct profile_config *cfg,
    int dir)
{
	return snprintf(path, path_size, "%s/g%04d/d%08d", cfg->root,
	    dir / cfg->group_size, dir) >= (int)path_size;
}

static int
make_file_path(char *path, size_t path_size, const struct profile_config *cfg,
    int file)
{
	int dir = cfg->leaf_dirs > 0 ? file % cfg->leaf_dirs : 0;

	return snprintf(path, path_size, "%s/g%04d/d%08d/f%08d", cfg->root,
	    dir / cfg->group_size, dir, file) >= (int)path_size;
}

static int
make_symlink_path(char *path, size_t path_size,
    const struct profile_config *cfg, int link)
{
	int dir = cfg->leaf_dirs > 0 ? link % cfg->leaf_dirs : 0;

	return snprintf(path, path_size, "%s/g%04d/d%08d/l%08d", cfg->root,
	    dir / cfg->group_size, dir, link) >= (int)path_size;
}

static void
print_stage(const char *stage, int count, double elapsed)
{
	double rate = elapsed > 0.0 ? (double)count / elapsed : 0.0;

	printf("PANTHERA_PKGSRC_META_%s_SECONDS:%.6f\n", stage, elapsed);
	printf("PANTHERA_PKGSRC_META_%s_COUNT:%d\n", stage, count);
	printf("PANTHERA_PKGSRC_META_%s_RATE:%.2f\n", stage, rate);
	fflush(stdout);
}

static void
fill_buffer(char *buffer, size_t size)
{
	size_t i;

	for (i = 0; i < size; i++)
		buffer[i] = (char)('a' + (i % 26));
}

static void
profile_create_measurement_overhead(const struct profile_config *cfg)
{
	char path[PATH_MAX];
	double start, path_elapsed, timing_elapsed;
	int i;

	start = now_seconds();
	path_elapsed = 0.0;
	timing_elapsed = 0.0;
	for (i = 0; i < cfg->files; i++) {
		double op_start;
		double path_start;

		path_start = now_seconds();
		if (make_file_path(path, sizeof(path), cfg, i)) {
			printf("PANTHERA_PKGSRC_META_PATH_TOO_LONG\n");
			return;
		}
		path_elapsed += now_seconds() - path_start;

		op_start = now_seconds();
		timing_elapsed += now_seconds() - op_start;
		op_start = now_seconds();
		timing_elapsed += now_seconds() - op_start;
		op_start = now_seconds();
		timing_elapsed += now_seconds() - op_start;
	}
	print_stage("CREATE_MEASURE_OVERHEAD", cfg->files,
	    now_seconds() - start);
	print_stage("CREATE_MEASURE_PATH", cfg->files, path_elapsed);
	print_stage("CREATE_MEASURE_CLOCK", cfg->files * 3, timing_elapsed);
}

static int
mkdir_one(const char *path)
{
	if (mkdir(path, 0777) == 0 || errno == EEXIST)
		return 0;
	printf("PANTHERA_PKGSRC_META_MKDIR_ERRNO:%s:%d\n", path, errno);
	return 1;
}

static int
create_dirs(const struct profile_config *cfg)
{
	char path[PATH_MAX];
	double start;
	int groups;
	int i;

	start = now_seconds();
	if (mkdir_one(cfg->root) != 0)
		return 1;
	groups = group_count(cfg);
	for (i = 0; i < groups; i++) {
		if (make_group_path(path, sizeof(path), cfg, i)) {
			printf("PANTHERA_PKGSRC_META_PATH_TOO_LONG\n");
			return 1;
		}
		if (mkdir_one(path) != 0)
			return 1;
	}
	for (i = 0; i < cfg->leaf_dirs; i++) {
		if (make_dir_path(path, sizeof(path), cfg, i)) {
			printf("PANTHERA_PKGSRC_META_PATH_TOO_LONG\n");
			return 1;
		}
		if (mkdir_one(path) != 0)
			return 1;
	}
	print_stage("MKDIR", cfg->leaf_dirs + groups + 1,
	    now_seconds() - start);
	return 0;
}

static int
create_files(const struct profile_config *cfg)
{
	char path[PATH_MAX];
	char *buffer;
	double start, open_elapsed, write_elapsed, close_elapsed;
	int i;

	buffer = malloc(cfg->file_size > 0 ? (size_t)cfg->file_size : 1);
	if (buffer == NULL) {
		printf("PANTHERA_PKGSRC_META_MALLOC_ERRNO:%d\n", errno);
		return 1;
	}
	fill_buffer(buffer, (size_t)cfg->file_size);
	if (getenv("PANTHERA_PKGSRC_META_PROFILE_OVERHEAD") != NULL)
		profile_create_measurement_overhead(cfg);
	start = now_seconds();
	open_elapsed = 0.0;
	write_elapsed = 0.0;
	close_elapsed = 0.0;
	for (i = 0; i < cfg->files; i++) {
		double op_start;
		ssize_t written;
		int fd;

		if (make_file_path(path, sizeof(path), cfg, i)) {
			printf("PANTHERA_PKGSRC_META_PATH_TOO_LONG\n");
			free(buffer);
			return 1;
		}
		op_start = now_seconds();
		fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
		open_elapsed += now_seconds() - op_start;
		if (fd < 0) {
			printf("PANTHERA_PKGSRC_META_OPEN_ERRNO:%s:%d\n",
			    path, errno);
			free(buffer);
			return 1;
		}
		op_start = now_seconds();
		written = write(fd, buffer, (size_t)cfg->file_size);
		write_elapsed += now_seconds() - op_start;
		if (written != (ssize_t)cfg->file_size) {
			printf("PANTHERA_PKGSRC_META_WRITE_ERRNO:%s:%d\n",
			    path, errno);
			close(fd);
			free(buffer);
			return 1;
		}
		op_start = now_seconds();
		if (close(fd) != 0) {
			close_elapsed += now_seconds() - op_start;
			printf("PANTHERA_PKGSRC_META_CLOSE_ERRNO:%s:%d\n",
			    path, errno);
			free(buffer);
			return 1;
		}
		close_elapsed += now_seconds() - op_start;
	}
	free(buffer);
	print_stage("CREATE_FILE", cfg->files, now_seconds() - start);
	print_stage("CREATE_FILE_OPEN", cfg->files, open_elapsed);
	print_stage("CREATE_FILE_WRITE", cfg->files, write_elapsed);
	print_stage("CREATE_FILE_CLOSE", cfg->files, close_elapsed);
	return 0;
}

static int
chmod_paths(const struct profile_config *cfg)
{
	char path[PATH_MAX];
	double start;
	int i;

	start = now_seconds();
	for (i = 0; i < cfg->files; i++) {
		if (make_file_path(path, sizeof(path), cfg, i)) {
			printf("PANTHERA_PKGSRC_META_PATH_TOO_LONG\n");
			return 1;
		}
		if (chmod(path, 0644) != 0) {
			printf("PANTHERA_PKGSRC_META_CHMOD_FILE_ERRNO:%s:%d\n",
			    path, errno);
			return 1;
		}
	}
	for (i = 0; i < cfg->leaf_dirs; i++) {
		if (make_dir_path(path, sizeof(path), cfg, i)) {
			printf("PANTHERA_PKGSRC_META_PATH_TOO_LONG\n");
			return 1;
		}
		if (chmod(path, 0755) != 0) {
			printf("PANTHERA_PKGSRC_META_CHMOD_DIR_ERRNO:%s:%d\n",
			    path, errno);
			return 1;
		}
	}
	print_stage("CHMOD", cfg->files + cfg->leaf_dirs,
	    now_seconds() - start);
	return 0;
}

static int
utimes_paths(const struct profile_config *cfg)
{
	char path[PATH_MAX];
	struct timeval times[2];
	double start;
	int i;

	times[0].tv_sec = 1700000000;
	times[0].tv_usec = 0;
	times[1].tv_sec = 1700000000;
	times[1].tv_usec = 0;

	start = now_seconds();
	for (i = 0; i < cfg->files; i++) {
		if (make_file_path(path, sizeof(path), cfg, i)) {
			printf("PANTHERA_PKGSRC_META_PATH_TOO_LONG\n");
			return 1;
		}
		if (utimes(path, times) != 0) {
			printf("PANTHERA_PKGSRC_META_UTIMES_FILE_ERRNO:%s:%d\n",
			    path, errno);
			return 1;
		}
	}
	for (i = 0; i < cfg->leaf_dirs; i++) {
		if (make_dir_path(path, sizeof(path), cfg, i)) {
			printf("PANTHERA_PKGSRC_META_PATH_TOO_LONG\n");
			return 1;
		}
		if (utimes(path, times) != 0) {
			printf("PANTHERA_PKGSRC_META_UTIMES_DIR_ERRNO:%s:%d\n",
			    path, errno);
			return 1;
		}
	}
	print_stage("UTIMES", cfg->files + cfg->leaf_dirs,
	    now_seconds() - start);
	return 0;
}

static int
create_symlinks(const struct profile_config *cfg)
{
	char path[PATH_MAX];
	double start;
	int i;

	start = now_seconds();
	for (i = 0; i < cfg->symlinks; i++) {
		if (make_symlink_path(path, sizeof(path), cfg, i)) {
			printf("PANTHERA_PKGSRC_META_PATH_TOO_LONG\n");
			return 1;
		}
		if (symlink("f00000000", path) != 0) {
			printf("PANTHERA_PKGSRC_META_SYMLINK_ERRNO:%s:%d\n",
			    path, errno);
			return 1;
		}
	}
	print_stage("SYMLINK", cfg->symlinks, now_seconds() - start);
	return 0;
}

static int
lstat_all(const struct profile_config *cfg)
{
	char path[PATH_MAX];
	struct stat st;
	double start;
	int i;

	start = now_seconds();
	for (i = 0; i < cfg->files; i++) {
		if (make_file_path(path, sizeof(path), cfg, i)) {
			printf("PANTHERA_PKGSRC_META_PATH_TOO_LONG\n");
			return 1;
		}
		if (lstat(path, &st) != 0) {
			printf("PANTHERA_PKGSRC_META_LSTAT_FILE_ERRNO:%s:%d\n",
			    path, errno);
			return 1;
		}
	}
	for (i = 0; i < cfg->symlinks; i++) {
		if (make_symlink_path(path, sizeof(path), cfg, i)) {
			printf("PANTHERA_PKGSRC_META_PATH_TOO_LONG\n");
			return 1;
		}
		if (lstat(path, &st) != 0) {
			printf("PANTHERA_PKGSRC_META_LSTAT_SYMLINK_ERRNO:%s:%d\n",
			    path, errno);
			return 1;
		}
	}
	for (i = 0; i < cfg->leaf_dirs; i++) {
		if (make_dir_path(path, sizeof(path), cfg, i)) {
			printf("PANTHERA_PKGSRC_META_PATH_TOO_LONG\n");
			return 1;
		}
		if (lstat(path, &st) != 0) {
			printf("PANTHERA_PKGSRC_META_LSTAT_DIR_ERRNO:%s:%d\n",
			    path, errno);
			return 1;
		}
	}
	print_stage("LSTAT", cfg->files + cfg->symlinks + cfg->leaf_dirs,
	    now_seconds() - start);
	return 0;
}

static int
remove_tree(const struct profile_config *cfg)
{
	char path[PATH_MAX];
	double start;
	int groups;
	int i;

	start = now_seconds();
	for (i = 0; i < cfg->symlinks; i++) {
		if (make_symlink_path(path, sizeof(path), cfg, i)) {
			printf("PANTHERA_PKGSRC_META_PATH_TOO_LONG\n");
			return 1;
		}
		if (unlink(path) != 0) {
			printf("PANTHERA_PKGSRC_META_UNLINK_SYMLINK_ERRNO:%s:%d\n",
			    path, errno);
			return 1;
		}
	}
	print_stage("UNLINK_SYMLINK", cfg->symlinks, now_seconds() - start);

	start = now_seconds();
	for (i = 0; i < cfg->files; i++) {
		if (make_file_path(path, sizeof(path), cfg, i)) {
			printf("PANTHERA_PKGSRC_META_PATH_TOO_LONG\n");
			return 1;
		}
		if (unlink(path) != 0) {
			printf("PANTHERA_PKGSRC_META_UNLINK_FILE_ERRNO:%s:%d\n",
			    path, errno);
			return 1;
		}
	}
	print_stage("UNLINK_FILE", cfg->files, now_seconds() - start);

	start = now_seconds();
	for (i = cfg->leaf_dirs - 1; i >= 0; i--) {
		if (make_dir_path(path, sizeof(path), cfg, i)) {
			printf("PANTHERA_PKGSRC_META_PATH_TOO_LONG\n");
			return 1;
		}
		if (rmdir(path) != 0) {
			printf("PANTHERA_PKGSRC_META_RMDIR_LEAF_ERRNO:%s:%d\n",
			    path, errno);
			return 1;
		}
	}
	groups = group_count(cfg);
	for (i = groups - 1; i >= 0; i--) {
		if (make_group_path(path, sizeof(path), cfg, i)) {
			printf("PANTHERA_PKGSRC_META_PATH_TOO_LONG\n");
			return 1;
		}
		if (rmdir(path) != 0) {
			printf("PANTHERA_PKGSRC_META_RMDIR_GROUP_ERRNO:%s:%d\n",
			    path, errno);
			return 1;
		}
	}
	if (rmdir(cfg->root) != 0) {
		printf("PANTHERA_PKGSRC_META_RMDIR_ROOT_ERRNO:%s:%d\n",
		    cfg->root, errno);
		return 1;
	}
	print_stage("RMDIR", cfg->leaf_dirs + groups + 1,
	    now_seconds() - start);
	return 0;
}

static int
run_probe(const struct profile_config *cfg)
{
	printf("PANTHERA_PKGSRC_META_ROOT:%s\n", cfg->root);
	printf("PANTHERA_PKGSRC_META_FILES:%d\n", cfg->files);
	printf("PANTHERA_PKGSRC_META_DIRS:%d\n", cfg->leaf_dirs);
	printf("PANTHERA_PKGSRC_META_SYMLINKS:%d\n", cfg->symlinks);
	printf("PANTHERA_PKGSRC_META_FILE_SIZE:%d\n", cfg->file_size);
	printf("PANTHERA_PKGSRC_META_GROUP_SIZE:%d\n", cfg->group_size);
	printf("PANTHERA_PKGSRC_META_KEEP:%d\n", cfg->keep_tree);
	fflush(stdout);

	if (create_dirs(cfg) != 0 ||
	    create_files(cfg) != 0 ||
	    chmod_paths(cfg) != 0 ||
	    utimes_paths(cfg) != 0 ||
	    create_symlinks(cfg) != 0 ||
	    lstat_all(cfg) != 0) {
		return 1;
	}
	if (!cfg->keep_tree && remove_tree(cfg) != 0)
		return 1;
	printf("PANTHERA_PKGSRC_META_OK\n");
	return 0;
}

int
main(int argc, char **argv)
{
	struct profile_config cfg;

	cfg.root = "/tmp/panthera_pkgsrc_metadata_profile";
	cfg.files = 5000;
	cfg.leaf_dirs = 1400;
	cfg.symlinks = 450;
	cfg.file_size = 64;
	cfg.group_size = 128;
	cfg.keep_tree = 0;

	if (argc > 1)
		cfg.root = argv[1];
	if (argc > 2)
		cfg.files = atoi(argv[2]);
	if (argc > 3)
		cfg.leaf_dirs = atoi(argv[3]);
	if (argc > 4)
		cfg.symlinks = atoi(argv[4]);
	if (argc > 5)
		cfg.file_size = atoi(argv[5]);
	if (argc > 6)
		cfg.group_size = atoi(argv[6]);
	if (argc > 7)
		cfg.keep_tree = strcmp(argv[7], "keep") == 0;
	if (cfg.files < 0 || cfg.leaf_dirs <= 0 || cfg.symlinks < 0 ||
	    cfg.file_size < 0 || cfg.group_size <= 0) {
		fprintf(stderr,
		    "usage: %s [root] [files] [dirs] [symlinks] [file_size] "
		    "[group_size] [keep]\n",
		    argv[0]);
		return 2;
	}
	return run_probe(&cfg);
}
