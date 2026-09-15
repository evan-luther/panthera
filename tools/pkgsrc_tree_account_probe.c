#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

struct account_state {
	unsigned long long files;
	unsigned long long dirs;
	unsigned long long symlinks;
	unsigned long long other;
	unsigned long long bytes;
	unsigned long long entries;
	unsigned long long progress_step;
	int remove_tree;
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
join_path(char *path, size_t path_size, const char *parent, const char *name)
{
	return snprintf(path, path_size, "%s/%s", parent, name) >=
	    (int)path_size;
}

static void
progress(struct account_state *state, const char *path)
{
	if (state->progress_step == 0)
		return;
	if ((state->entries % state->progress_step) != 0)
		return;
	printf("PANTHERA_PKGSRC_TREE_PROGRESS:%llu:%s\n",
	    state->entries, path);
	fflush(stdout);
}

static int
walk_tree(const char *path, struct account_state *state)
{
	char child[PATH_MAX];
	struct dirent *de;
	struct stat st;
	DIR *dir;
	int error = 0;

	if (lstat(path, &st) != 0) {
		printf("PANTHERA_PKGSRC_TREE_LSTAT_ERRNO:%s:%d\n", path, errno);
		return 1;
	}

	state->entries++;
	state->bytes += (unsigned long long)st.st_size;
	if (S_ISDIR(st.st_mode)) {
		state->dirs++;
	} else if (S_ISREG(st.st_mode)) {
		state->files++;
		progress(state, path);
		if (state->remove_tree && unlink(path) != 0) {
			printf("PANTHERA_PKGSRC_TREE_UNLINK_ERRNO:%s:%d\n",
			    path, errno);
			return 1;
		}
		return 0;
	} else if (S_ISLNK(st.st_mode)) {
		state->symlinks++;
		progress(state, path);
		if (state->remove_tree && unlink(path) != 0) {
			printf("PANTHERA_PKGSRC_TREE_UNLINK_ERRNO:%s:%d\n",
			    path, errno);
			return 1;
		}
		return 0;
	} else {
		state->other++;
		progress(state, path);
		if (state->remove_tree && unlink(path) != 0) {
			printf("PANTHERA_PKGSRC_TREE_UNLINK_ERRNO:%s:%d\n",
			    path, errno);
			return 1;
		}
		return 0;
	}

	progress(state, path);
	dir = opendir(path);
	if (dir == NULL) {
		printf("PANTHERA_PKGSRC_TREE_OPENDIR_ERRNO:%s:%d\n", path, errno);
		return 1;
	}
	while ((de = readdir(dir)) != NULL) {
		if (strcmp(de->d_name, ".") == 0 ||
		    strcmp(de->d_name, "..") == 0) {
			continue;
		}
		if (join_path(child, sizeof(child), path, de->d_name)) {
			printf("PANTHERA_PKGSRC_TREE_PATH_TOO_LONG:%s/%s\n",
			    path, de->d_name);
			error = 1;
			break;
		}
		if (walk_tree(child, state) != 0) {
			error = 1;
			break;
		}
	}
	if (closedir(dir) != 0 && error == 0) {
		printf("PANTHERA_PKGSRC_TREE_CLOSEDIR_ERRNO:%s:%d\n", path, errno);
		error = 1;
	}
	if (error != 0)
		return error;
	if (state->remove_tree && rmdir(path) != 0) {
		printf("PANTHERA_PKGSRC_TREE_RMDIR_ERRNO:%s:%d\n", path, errno);
		return 1;
	}
	return 0;
}

int
main(int argc, char **argv)
{
	struct account_state state;
	const char *root = "/tmp/pkgsrc-devel-dma-r1/pkgsrc/devel";
	double start;
	double elapsed;

	memset(&state, 0, sizeof(state));
	state.progress_step = 5000;

	if (argc > 1)
		root = argv[1];
	if (argc > 2)
		state.remove_tree = strcmp(argv[2], "remove") == 0;
	if (argc > 3)
		state.progress_step = strtoull(argv[3], NULL, 10);

	printf("PANTHERA_PKGSRC_TREE_ROOT:%s\n", root);
	printf("PANTHERA_PKGSRC_TREE_REMOVE:%d\n", state.remove_tree);
	fflush(stdout);

	start = now_seconds();
	if (walk_tree(root, &state) != 0)
		return 1;
	elapsed = now_seconds() - start;

	printf("PANTHERA_PKGSRC_TREE_SECONDS:%.6f\n", elapsed);
	printf("PANTHERA_PKGSRC_TREE_ENTRIES:%llu\n", state.entries);
	printf("PANTHERA_PKGSRC_TREE_FILES:%llu\n", state.files);
	printf("PANTHERA_PKGSRC_TREE_DIRS:%llu\n", state.dirs);
	printf("PANTHERA_PKGSRC_TREE_SYMLINKS:%llu\n", state.symlinks);
	printf("PANTHERA_PKGSRC_TREE_OTHER:%llu\n", state.other);
	printf("PANTHERA_PKGSRC_TREE_BYTES:%llu\n", state.bytes);
	printf("PANTHERA_PKGSRC_TREE_OK\n");
	return 0;
}
