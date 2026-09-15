#include <sys/attr.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/vnode.h>

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef ATTR_BIT_MAP_COUNT
#define ATTR_BIT_MAP_COUNT 5
#endif

#ifndef FSOPT_PACK_INVAL_ATTRS
#define FSOPT_PACK_INVAL_ATTRS 0x00000008
#endif

#define ATTR_BUF_SIZE (32 * 1024)

struct walk_state {
	unsigned long long entries;
	unsigned long long files;
	unsigned long long dirs;
	unsigned long long symlinks;
	unsigned long long other;
	unsigned long long progress_step;
	unsigned long long max_rounds_per_dir;
	int full_attrs;
};

struct attr_record {
	uint32_t length;
	attribute_set_t returned;
	attrreference_t name;
	dev_t dev;
	fsobj_type_t objtype;
	uint64_t fileid;
	nlink_t linkcount;
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
progress(struct walk_state *state, const char *path)
{
	if (state->progress_step == 0)
		return;
	if ((state->entries % state->progress_step) != 0)
		return;
	printf("PANTHERA_GLB_WALK_PROGRESS:%llu:%s\n", state->entries, path);
	fflush(stdout);
}

static int
parse_name(const char *record, size_t remaining, const char **name_out,
    size_t *namelen_out)
{
	const struct attr_record *rec;
	const char *name;

	if (remaining < sizeof(*rec))
		return EINVAL;
	rec = (const struct attr_record *)record;
	if (rec->length == 0 || rec->length > remaining)
		return EINVAL;
	if (rec->name.attr_length == 0)
		return EINVAL;
	name = ((const char *)&rec->name) + rec->name.attr_dataoffset;
	if (name < record || name + rec->name.attr_length > record + rec->length)
		return EINVAL;
	*name_out = name;
	*namelen_out = rec->name.attr_length - 1;
	return 0;
}

static int
walk_dir(const char *path, struct walk_state *state)
{
	struct attrlist al;
	char child[PATH_MAX];
	char *buf;
	int fd;
	unsigned long long rounds = 0;
	int error = 0;

	fd = open(path, O_RDONLY | O_NONBLOCK | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0) {
		printf("PANTHERA_GLB_WALK_OPEN_ERRNO:%s:%d\n", path, errno);
		return 1;
	}

	buf = malloc(ATTR_BUF_SIZE);
	if (buf == NULL) {
		printf("PANTHERA_GLB_WALK_MALLOC_ERRNO:%d\n", errno);
		close(fd);
		return 1;
	}

	memset(&al, 0, sizeof(al));
	al.bitmapcount = ATTR_BIT_MAP_COUNT;
	if (state->full_attrs) {
		al.commonattr = ATTR_CMN_RETURNED_ATTRS | ATTR_CMN_NAME |
		    ATTR_CMN_DEVID | ATTR_CMN_OBJTYPE | ATTR_CMN_CRTIME |
		    ATTR_CMN_MODTIME | ATTR_CMN_CHGTIME | ATTR_CMN_ACCTIME |
		    ATTR_CMN_OWNERID | ATTR_CMN_GRPID |
		    ATTR_CMN_ACCESSMASK | ATTR_CMN_FLAGS | ATTR_CMN_FILEID;
		al.fileattr = ATTR_FILE_LINKCOUNT | ATTR_FILE_ALLOCSIZE |
		    ATTR_FILE_IOBLOCKSIZE | ATTR_FILE_DEVTYPE |
		    ATTR_FILE_DATALENGTH;
	} else {
		al.commonattr = ATTR_CMN_RETURNED_ATTRS | ATTR_CMN_NAME |
		    ATTR_CMN_DEVID | ATTR_CMN_OBJTYPE | ATTR_CMN_FILEID;
		al.fileattr = ATTR_FILE_LINKCOUNT;
	}

	for (;;) {
		const char *cursor;
		size_t remaining;
		int count;
		int parsed;

		memset(buf, 0, ATTR_BUF_SIZE);
		count = getattrlistbulk(fd, &al, buf, ATTR_BUF_SIZE,
		    FSOPT_PACK_INVAL_ATTRS);
		if (count < 0) {
			printf("PANTHERA_GLB_WALK_GETATTR_ERRNO:%s:%d\n",
			    path, errno);
			error = 1;
			break;
		}
		rounds++;
		if (rounds > state->max_rounds_per_dir) {
			printf("PANTHERA_GLB_WALK_MAX_ROUNDS:%s:%llu\n",
			    path, rounds);
			error = 1;
			break;
		}
		if (count == 0)
			break;

		cursor = buf;
		remaining = ATTR_BUF_SIZE;
		for (parsed = 0; parsed < count; parsed++) {
			const struct attr_record *rec;
			const char *name;
			size_t namelen;

			rec = (const struct attr_record *)cursor;
			if (parse_name(cursor, remaining, &name, &namelen) != 0) {
				printf("PANTHERA_GLB_WALK_BAD_RECORD:%s:%d:%zu\n",
				    path, parsed, remaining);
				error = 1;
				goto out;
			}
			if (namelen == 1 && name[0] == '.') {
				cursor += rec->length;
				remaining -= rec->length;
				continue;
			}
			if (namelen == 2 && name[0] == '.' && name[1] == '.') {
				cursor += rec->length;
				remaining -= rec->length;
				continue;
			}
			if (join_path(child, sizeof(child), path, name)) {
				printf("PANTHERA_GLB_WALK_PATH_TOO_LONG:%s/%s\n",
				    path, name);
				error = 1;
				goto out;
			}

			state->entries++;
			switch (rec->objtype) {
			case VDIR:
				state->dirs++;
				progress(state, child);
				if (walk_dir(child, state) != 0) {
					error = 1;
					goto out;
				}
				break;
			case VREG:
				state->files++;
				progress(state, child);
				break;
			case VLNK:
				state->symlinks++;
				progress(state, child);
				break;
			default:
				state->other++;
				progress(state, child);
				break;
			}

			cursor += rec->length;
			remaining -= rec->length;
		}
	}

out:
	free(buf);
	close(fd);
	return error;
}

int
main(int argc, char **argv)
{
	struct walk_state state;
	const char *root = "/tmp/pkgsrc-devel-dma-r1/pkgsrc/devel";
	double start;
	double elapsed;

	memset(&state, 0, sizeof(state));
	state.progress_step = 5000;
	state.max_rounds_per_dir = 100000;

	if (argc > 1)
		root = argv[1];
	if (argc > 2)
		state.progress_step = strtoull(argv[2], NULL, 10);
	if (argc > 3)
		state.max_rounds_per_dir = strtoull(argv[3], NULL, 10);
	if (argc > 4)
		state.full_attrs = strcmp(argv[4], "full") == 0;

	printf("PANTHERA_GLB_WALK_ROOT:%s\n", root);
	printf("PANTHERA_GLB_WALK_FULL:%d\n", state.full_attrs);
	fflush(stdout);
	start = now_seconds();
	if (walk_dir(root, &state) != 0)
		return 1;
	elapsed = now_seconds() - start;

	printf("PANTHERA_GLB_WALK_SECONDS:%.6f\n", elapsed);
	printf("PANTHERA_GLB_WALK_ENTRIES:%llu\n", state.entries);
	printf("PANTHERA_GLB_WALK_FILES:%llu\n", state.files);
	printf("PANTHERA_GLB_WALK_DIRS:%llu\n", state.dirs);
	printf("PANTHERA_GLB_WALK_SYMLINKS:%llu\n", state.symlinks);
	printf("PANTHERA_GLB_WALK_OTHER:%llu\n", state.other);
	printf("PANTHERA_GLB_WALK_OK\n");
	return 0;
}
