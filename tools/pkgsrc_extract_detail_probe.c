#include <archive.h>
#include <archive_entry.h>
#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

struct metric {
	unsigned long long count;
	unsigned long long bytes;
	double seconds;
};

struct totals {
	struct metric header;
	struct metric skip;
	struct metric parent_mkdir;
	struct metric mkdir;
	struct metric file_open;
	struct metric file_read;
	struct metric file_write;
	struct metric file_close;
	struct metric chmod_file;
	struct metric chmod_dir;
	struct metric utimes_file;
	struct metric utimes_dir;
	struct metric symlink;
	struct metric unsupported;
	struct metric cleanup_walk;
	struct metric cleanup_unlink;
	struct metric cleanup_rmdir;
	unsigned long long entries;
	unsigned long long files;
	unsigned long long dirs;
	unsigned long long symlinks;
	unsigned long long skipped;
	unsigned long long errors;
	unsigned long long cleanup_entries;
	unsigned long long cleanup_errors;
};

struct path_cache {
	char **slots;
	size_t capacity;
	size_t count;
};

struct probe_config {
	const char *tarball;
	const char *root;
	const char *member;
	const char *compression;
	int apply_times;
	int apply_modes;
	int parent_cache;
	int cleanup;
};

struct extract_state {
	const struct probe_config *cfg;
	struct totals *totals;
	struct path_cache cache;
};

static double
now_seconds(void)
{
	struct timeval tv;

	if (gettimeofday(&tv, NULL) != 0)
		return 0.0;
	return (double)tv.tv_sec + ((double)tv.tv_usec / 1000000.0);
}

static void
metric_add(struct metric *metric, double seconds,
    unsigned long long bytes)
{
	metric->count++;
	metric->seconds += seconds;
	metric->bytes += bytes;
}

static void
metric_print(const char *name, const struct metric *metric)
{
	printf("PANTHERA_PKGSRC_DETAIL_%s_COUNT:%llu\n", name, metric->count);
	printf("PANTHERA_PKGSRC_DETAIL_%s_SECONDS:%.6f\n", name,
	    metric->seconds);
	if (metric->bytes != 0)
		printf("PANTHERA_PKGSRC_DETAIL_%s_BYTES:%llu\n", name,
		    metric->bytes);
}

static unsigned long
hash_string(const char *str)
{
	unsigned long hash = 1469598103934665603UL;

	while (*str != '\0') {
		hash ^= (unsigned char)*str++;
		hash *= 1099511628211UL;
	}
	return hash;
}

static int
cache_init(struct path_cache *cache, size_t capacity)
{
	cache->slots = calloc(capacity, sizeof(cache->slots[0]));
	if (cache->slots == NULL)
		return ENOMEM;
	cache->capacity = capacity;
	cache->count = 0;
	return 0;
}

static void
cache_free(struct path_cache *cache)
{
	size_t i;

	if (cache->slots == NULL)
		return;
	for (i = 0; i < cache->capacity; i++)
		free(cache->slots[i]);
	free(cache->slots);
	memset(cache, 0, sizeof(*cache));
}

static char *
copy_string(const char *str)
{
	size_t len = strlen(str) + 1;
	char *copy = malloc(len);

	if (copy != NULL)
		memcpy(copy, str, len);
	return copy;
}

static int
cache_contains(const struct path_cache *cache, const char *path)
{
	size_t i;
	size_t pos;

	if (cache->slots == NULL || cache->capacity == 0)
		return 0;
	pos = hash_string(path) % cache->capacity;
	for (i = 0; i < cache->capacity; i++) {
		size_t idx = (pos + i) % cache->capacity;

		if (cache->slots[idx] == NULL)
			return 0;
		if (strcmp(cache->slots[idx], path) == 0)
			return 1;
	}
	return 0;
}

static int
cache_insert(struct path_cache *cache, const char *path)
{
	size_t i;
	size_t pos;

	if (cache->slots == NULL || cache->capacity == 0 ||
	    cache->count * 2 >= cache->capacity)
		return 0;
	pos = hash_string(path) % cache->capacity;
	for (i = 0; i < cache->capacity; i++) {
		size_t idx = (pos + i) % cache->capacity;

		if (cache->slots[idx] == NULL) {
			cache->slots[idx] = copy_string(path);
			if (cache->slots[idx] == NULL)
				return ENOMEM;
			cache->count++;
			return 0;
		}
		if (strcmp(cache->slots[idx], path) == 0)
			return 0;
	}
	return 0;
}

static int
has_dotdot_component(const char *path)
{
	const char *p = path;

	while (*p != '\0') {
		const char *next = strchr(p, '/');
		size_t len = next != NULL ? (size_t)(next - p) : strlen(p);

		if (len == 2 && p[0] == '.' && p[1] == '.')
			return 1;
		if (next == NULL)
			break;
		p = next + 1;
	}
	return 0;
}

static int
path_matches_member(const char *path, const char *member)
{
	size_t len;

	if (member == NULL || member[0] == '\0')
		return 1;
	len = strlen(member);
	while (len > 0 && member[len - 1] == '/')
		len--;
	if (strncmp(path, member, len) != 0)
		return 0;
	return path[len] == '\0' || path[len] == '/';
}

static int
join_path(char *out, size_t out_size, const char *root, const char *path)
{
	if (path[0] == '/' || has_dotdot_component(path))
		return EINVAL;
	if (snprintf(out, out_size, "%s/%s", root, path) >= (int)out_size)
		return ENAMETOOLONG;
	return 0;
}

static int
ensure_parent_dirs(const char *path, struct extract_state *state)
{
	struct totals *totals = state->totals;
	char tmp[PATH_MAX];
	char *p;

	if (strlen(path) >= sizeof(tmp))
		return ENAMETOOLONG;
	memcpy(tmp, path, strlen(path) + 1);
	for (p = tmp + 1; *p != '\0'; p++) {
		double start;

		if (*p != '/')
			continue;
		*p = '\0';
		if (state->cfg->parent_cache && cache_contains(&state->cache, tmp)) {
			*p = '/';
			continue;
		}
		start = now_seconds();
		if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
			*p = '/';
			return errno;
		}
		metric_add(&totals->parent_mkdir, now_seconds() - start, 0);
		if (state->cfg->parent_cache) {
			int rc = cache_insert(&state->cache, tmp);

			if (rc != 0) {
				*p = '/';
				return rc;
			}
		}
		*p = '/';
	}
	return 0;
}

static int
apply_entry_mode(const char *path, mode_t mode, int is_dir,
    const struct probe_config *cfg, struct totals *totals)
{
	double start;

	if (!cfg->apply_modes)
		return 0;
	start = now_seconds();
	if (chmod(path, mode & 07777) != 0)
		return errno;
	metric_add(is_dir ? &totals->chmod_dir : &totals->chmod_file,
	    now_seconds() - start, 0);
	return 0;
}

static int
apply_entry_time(const char *path, struct archive_entry *entry, int is_dir,
    const struct probe_config *cfg, struct totals *totals)
{
	struct timeval times[2];
	double start;

	if (!cfg->apply_times || !archive_entry_mtime_is_set(entry))
		return 0;
	times[0].tv_sec = archive_entry_mtime(entry);
	times[0].tv_usec = archive_entry_mtime_nsec(entry) / 1000;
	times[1] = times[0];
	start = now_seconds();
	if (utimes(path, times) != 0)
		return errno;
	metric_add(is_dir ? &totals->utimes_dir : &totals->utimes_file,
	    now_seconds() - start, 0);
	return 0;
}

static int
extract_file(struct archive *archive, struct archive_entry *entry,
    const char *path, struct extract_state *state)
{
	const struct probe_config *cfg = state->cfg;
	struct totals *totals = state->totals;
	const void *buffer;
	size_t size;
	la_int64_t offset;
	mode_t mode = archive_entry_mode(entry);
	int fd;
	int rc;
	double start;

	rc = ensure_parent_dirs(path, state);
	if (rc != 0)
		return rc;
	start = now_seconds();
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode & 07777);
	if (fd < 0)
		return errno;
	metric_add(&totals->file_open, now_seconds() - start, 0);
	for (;;) {
		start = now_seconds();
		rc = archive_read_data_block(archive, &buffer, &size, &offset);
		metric_add(&totals->file_read, now_seconds() - start,
		    rc == ARCHIVE_OK ? (unsigned long long)size : 0);
		if (rc == ARCHIVE_EOF)
			break;
		if (rc != ARCHIVE_OK) {
			close(fd);
			return EIO;
		}
		if (lseek(fd, offset, SEEK_SET) < 0) {
			close(fd);
			return errno;
		}
		start = now_seconds();
		if (write(fd, buffer, size) != (ssize_t)size) {
			close(fd);
			return errno != 0 ? errno : EIO;
		}
		metric_add(&totals->file_write, now_seconds() - start, size);
	}
	start = now_seconds();
	if (close(fd) != 0)
		return errno;
	metric_add(&totals->file_close, now_seconds() - start, 0);
	rc = apply_entry_mode(path, mode, 0, cfg, totals);
	if (rc != 0)
		return rc;
	return apply_entry_time(path, entry, 0, cfg, totals);
}

static int
extract_dir(struct archive_entry *entry, const char *path,
    struct extract_state *state)
{
	const struct probe_config *cfg = state->cfg;
	struct totals *totals = state->totals;
	mode_t mode = archive_entry_mode(entry);
	double start;
	int rc;

	rc = ensure_parent_dirs(path, state);
	if (rc != 0)
		return rc;
	start = now_seconds();
	if (mkdir(path, mode & 07777) != 0 && errno != EEXIST)
		return errno;
	metric_add(&totals->mkdir, now_seconds() - start, 0);
	rc = apply_entry_mode(path, mode, 1, cfg, totals);
	if (rc != 0)
		return rc;
	return apply_entry_time(path, entry, 1, cfg, totals);
}

static int
extract_symlink(struct archive_entry *entry, const char *path,
    struct extract_state *state)
{
	struct totals *totals = state->totals;
	const char *target = archive_entry_symlink(entry);
	double start;
	int rc;

	if (target == NULL)
		target = "";
	rc = ensure_parent_dirs(path, state);
	if (rc != 0)
		return rc;
	start = now_seconds();
	if (symlink(target, path) != 0)
		return errno;
	metric_add(&totals->symlink, now_seconds() - start, 0);
	return 0;
}

static int
extract_archive(struct extract_state *state)
{
	const struct probe_config *cfg = state->cfg;
	struct totals *totals = state->totals;
	struct archive *archive;
	struct archive_entry *entry;
	int rc;

	archive = archive_read_new();
	if (archive == NULL)
		return ENOMEM;
	if (strcmp(cfg->compression, "none") == 0)
		archive_read_support_filter_none(archive);
	else
		archive_read_support_filter_gzip(archive);
	archive_read_support_format_tar(archive);
	rc = archive_read_open_filename(archive, cfg->tarball, 1024 * 128);
	if (rc != ARCHIVE_OK) {
		fprintf(stderr, "archive open failed: %s\n",
		    archive_error_string(archive));
		archive_read_free(archive);
		return EIO;
	}
	for (;;) {
		const char *pathname;
		char path[PATH_MAX];
		double start;
		mode_t type;
		int error;

		start = now_seconds();
		rc = archive_read_next_header(archive, &entry);
		metric_add(&totals->header, now_seconds() - start, 0);
		if (rc == ARCHIVE_EOF)
			break;
		if (rc != ARCHIVE_OK) {
			fprintf(stderr, "archive header failed: %s\n",
			    archive_error_string(archive));
			totals->errors++;
			break;
		}
		pathname = archive_entry_pathname(entry);
		if (pathname == NULL || !path_matches_member(pathname, cfg->member)) {
			start = now_seconds();
			archive_read_data_skip(archive);
			metric_add(&totals->skip, now_seconds() - start, 0);
			totals->skipped++;
			continue;
		}
		error = join_path(path, sizeof(path), cfg->root, pathname);
		if (error != 0) {
			fprintf(stderr, "unsafe/path-too-long entry: %s\n", pathname);
			totals->errors++;
			continue;
		}
		totals->entries++;
		type = archive_entry_filetype(entry);
		if (type == AE_IFDIR) {
			totals->dirs++;
			error = extract_dir(entry, path, state);
		} else if (type == AE_IFREG) {
			totals->files++;
			error = extract_file(archive, entry, path, state);
		} else if (type == AE_IFLNK) {
			totals->symlinks++;
			error = extract_symlink(entry, path, state);
		} else {
			metric_add(&totals->unsupported, 0.0, 0);
			error = archive_read_data_skip(archive) == ARCHIVE_OK ? 0 : EIO;
		}
		if (error != 0) {
			fprintf(stderr, "extract failed for %s: errno=%d\n",
			    pathname, error);
			totals->errors++;
			break;
		}
	}
	archive_read_close(archive);
	archive_read_free(archive);
	return totals->errors == 0 ? 0 : 1;
}

static int
cleanup_tree(const char *root, struct totals *totals, double *wall_seconds)
{
	char *paths[2];
	FTS *tree;
	FTSENT *entry;
	double start;
	int rc = 0;

	paths[0] = (char *)root;
	paths[1] = NULL;
	tree = fts_open(paths, FTS_PHYSICAL | FTS_NOCHDIR, NULL);
	if (tree == NULL) {
		totals->cleanup_errors++;
		return errno;
	}
	start = now_seconds();
	errno = 0;
	for (;;) {
		double read_start = now_seconds();
		entry = fts_read(tree);
		metric_add(&totals->cleanup_walk, now_seconds() - read_start, 0);
		if (entry == NULL)
			break;
		totals->cleanup_entries++;
		switch (entry->fts_info) {
		case FTS_D:
			break;
		case FTS_DP: {
			double op_start = now_seconds();

			if (rmdir(entry->fts_accpath) != 0) {
				rc = errno;
				totals->cleanup_errors++;
			}
			metric_add(&totals->cleanup_rmdir, now_seconds() - op_start,
			    0);
			break;
		}
		case FTS_F:
		case FTS_SL:
		case FTS_SLNONE:
		case FTS_DEFAULT: {
			double op_start = now_seconds();

			if (unlink(entry->fts_accpath) != 0) {
				rc = errno;
				totals->cleanup_errors++;
			}
			metric_add(&totals->cleanup_unlink, now_seconds() - op_start,
			    0);
			break;
		}
		case FTS_ERR:
		case FTS_DNR:
		case FTS_NS:
			rc = entry->fts_errno != 0 ? entry->fts_errno : EIO;
			totals->cleanup_errors++;
			break;
		default:
			break;
		}
		if (rc != 0)
			break;
		errno = 0;
	}
	if (rc == 0 && errno != 0) {
		rc = errno;
		totals->cleanup_errors++;
	}
	if (fts_close(tree) != 0 && rc == 0) {
		rc = errno;
		totals->cleanup_errors++;
	}
	*wall_seconds = now_seconds() - start;
	return rc;
}

static void
print_totals(const struct totals *totals, double wall)
{
	printf("PANTHERA_PKGSRC_DETAIL_WALL_SECONDS:%.6f\n", wall);
	printf("PANTHERA_PKGSRC_DETAIL_ENTRIES:%llu\n", totals->entries);
	printf("PANTHERA_PKGSRC_DETAIL_FILES:%llu\n", totals->files);
	printf("PANTHERA_PKGSRC_DETAIL_DIRS:%llu\n", totals->dirs);
	printf("PANTHERA_PKGSRC_DETAIL_SYMLINKS:%llu\n", totals->symlinks);
	printf("PANTHERA_PKGSRC_DETAIL_SKIPPED:%llu\n", totals->skipped);
	printf("PANTHERA_PKGSRC_DETAIL_ERRORS:%llu\n", totals->errors);
	metric_print("HEADER", &totals->header);
	metric_print("SKIP", &totals->skip);
	metric_print("PARENT_MKDIR", &totals->parent_mkdir);
	metric_print("MKDIR", &totals->mkdir);
	metric_print("FILE_OPEN", &totals->file_open);
	metric_print("FILE_READ", &totals->file_read);
	metric_print("FILE_WRITE", &totals->file_write);
	metric_print("FILE_CLOSE", &totals->file_close);
	metric_print("CHMOD_FILE", &totals->chmod_file);
	metric_print("CHMOD_DIR", &totals->chmod_dir);
	metric_print("UTIMES_FILE", &totals->utimes_file);
	metric_print("UTIMES_DIR", &totals->utimes_dir);
	metric_print("SYMLINK", &totals->symlink);
	metric_print("UNSUPPORTED", &totals->unsupported);
}

static void
print_cleanup_totals(const struct totals *totals, double wall)
{
	printf("PANTHERA_PKGSRC_DETAIL_CLEANUP_WALL_SECONDS:%.6f\n", wall);
	printf("PANTHERA_PKGSRC_DETAIL_CLEANUP_ENTRIES:%llu\n",
	    totals->cleanup_entries);
	printf("PANTHERA_PKGSRC_DETAIL_CLEANUP_ERRORS:%llu\n",
	    totals->cleanup_errors);
	metric_print("CLEANUP_WALK", &totals->cleanup_walk);
	metric_print("CLEANUP_UNLINK", &totals->cleanup_unlink);
	metric_print("CLEANUP_RMDIR", &totals->cleanup_rmdir);
}

static void
usage(const char *prog)
{
	fprintf(stderr,
	    "Usage: %s --tarball PATH --root DIR [options]\n"
	    "Options:\n"
	    "  --compression gz|none\n"
	    "  --member PATH\n"
	    "  --no-times\n"
	    "  --no-modes\n"
	    "  --no-parent-cache\n"
	    "  --cleanup\n",
	    prog);
}

int
main(int argc, char **argv)
{
	struct probe_config cfg;
	struct totals totals;
	double cleanup_wall = 0.0;
	double start;
	int i;
	int rc;

	memset(&cfg, 0, sizeof(cfg));
	memset(&totals, 0, sizeof(totals));
	cfg.compression = "gz";
	cfg.apply_times = 1;
	cfg.apply_modes = 1;
	cfg.parent_cache = 1;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--tarball") == 0 && i + 1 < argc) {
			cfg.tarball = argv[++i];
		} else if (strcmp(argv[i], "--root") == 0 && i + 1 < argc) {
			cfg.root = argv[++i];
		} else if (strcmp(argv[i], "--compression") == 0 && i + 1 < argc) {
			cfg.compression = argv[++i];
		} else if (strcmp(argv[i], "--member") == 0 && i + 1 < argc) {
			cfg.member = argv[++i];
		} else if (strcmp(argv[i], "--no-times") == 0) {
			cfg.apply_times = 0;
		} else if (strcmp(argv[i], "--no-modes") == 0) {
			cfg.apply_modes = 0;
		} else if (strcmp(argv[i], "--no-parent-cache") == 0) {
			cfg.parent_cache = 0;
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
	printf("PANTHERA_PKGSRC_DETAIL_TARBALL:%s\n", cfg.tarball);
	printf("PANTHERA_PKGSRC_DETAIL_ROOT:%s\n", cfg.root);
	printf("PANTHERA_PKGSRC_DETAIL_COMPRESSION:%s\n", cfg.compression);
	printf("PANTHERA_PKGSRC_DETAIL_MEMBER:%s\n",
	    cfg.member != NULL ? cfg.member : "");
	printf("PANTHERA_PKGSRC_DETAIL_APPLY_TIMES:%d\n", cfg.apply_times);
	printf("PANTHERA_PKGSRC_DETAIL_APPLY_MODES:%d\n", cfg.apply_modes);
	printf("PANTHERA_PKGSRC_DETAIL_PARENT_CACHE:%d\n", cfg.parent_cache);
	fflush(stdout);
	(void)mkdir(cfg.root, 0777);
	start = now_seconds();
	{
		struct extract_state state;

		memset(&state, 0, sizeof(state));
		state.cfg = &cfg;
		state.totals = &totals;
		if (cfg.parent_cache && cache_init(&state.cache, 65536) != 0)
			return 1;
		if (cfg.parent_cache)
			(void)cache_insert(&state.cache, cfg.root);
	rc = extract_archive(&state);
	cache_free(&state.cache);
	}
	print_totals(&totals, now_seconds() - start);
	if (rc == 0 && cfg.cleanup) {
		int cleanup_rc = cleanup_tree(cfg.root, &totals, &cleanup_wall);

		print_cleanup_totals(&totals, cleanup_wall);
		if (cleanup_rc != 0)
			rc = cleanup_rc;
	}
	if (rc == 0)
		printf("PANTHERA_PKGSRC_DETAIL_PROBE_OK\n");
	return rc;
}
