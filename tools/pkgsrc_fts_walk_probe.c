#include <errno.h>
#include <fts.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>

static double
now_seconds(void)
{
	struct timeval tv;

	if (gettimeofday(&tv, NULL) != 0)
		return 0.0;
	return (double)tv.tv_sec + ((double)tv.tv_usec / 1000000.0);
}

int
main(int argc, char **argv)
{
	const char *root = "/tmp/pkgsrc-devel-dma-r1/pkgsrc/devel";
	int options = FTS_PHYSICAL;
	unsigned long long progress_step = 5000;
	unsigned long long entries = 0;
	unsigned long long files = 0;
	unsigned long long dirs = 0;
	unsigned long long symlinks = 0;
	unsigned long long other = 0;
	char *paths[2];
	FTS *tree;
	FTSENT *entry;
	double start;
	double elapsed;

	if (argc > 1)
		root = argv[1];
	if (argc > 2 && strcmp(argv[2], "nostat") == 0)
		options |= FTS_NOSTAT;
	if (argc > 3 && strcmp(argv[3], "nochdir") == 0)
		options |= FTS_NOCHDIR;
	if (argc > 4)
		progress_step = strtoull(argv[4], NULL, 10);

	paths[0] = (char *)root;
	paths[1] = NULL;

	printf("PANTHERA_FTS_WALK_ROOT:%s\n", root);
	printf("PANTHERA_FTS_WALK_OPTIONS:0x%x\n", options);
	fflush(stdout);

	tree = fts_open(paths, options, NULL);
	if (tree == NULL) {
		printf("PANTHERA_FTS_WALK_OPEN_ERRNO:%d\n", errno);
		return 1;
	}

	start = now_seconds();
	errno = 0;
	while ((entry = fts_read(tree)) != NULL) {
		entries++;
		switch (entry->fts_info) {
		case FTS_F:
			files++;
			break;
		case FTS_D:
		case FTS_DP:
		case FTS_DC:
		case FTS_DNR:
		case FTS_DOT:
			dirs++;
			break;
		case FTS_SL:
		case FTS_SLNONE:
			symlinks++;
			break;
		default:
			other++;
			break;
		}
		if (progress_step != 0 && (entries % progress_step) == 0) {
			printf("PANTHERA_FTS_WALK_PROGRESS:%llu:%s\n",
			    entries, entry->fts_path);
			fflush(stdout);
		}
		errno = 0;
	}
	if (errno != 0) {
		printf("PANTHERA_FTS_WALK_READ_ERRNO:%d\n", errno);
		fts_close(tree);
		return 1;
	}
	if (fts_close(tree) != 0) {
		printf("PANTHERA_FTS_WALK_CLOSE_ERRNO:%d\n", errno);
		return 1;
	}
	elapsed = now_seconds() - start;

	printf("PANTHERA_FTS_WALK_SECONDS:%.6f\n", elapsed);
	printf("PANTHERA_FTS_WALK_ENTRIES:%llu\n", entries);
	printf("PANTHERA_FTS_WALK_FILES:%llu\n", files);
	printf("PANTHERA_FTS_WALK_DIRS:%llu\n", dirs);
	printf("PANTHERA_FTS_WALK_SYMLINKS:%llu\n", symlinks);
	printf("PANTHERA_FTS_WALK_OTHER:%llu\n", other);
	printf("PANTHERA_FTS_WALK_OK\n");
	return 0;
}
