#include <sys/attr.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/time.h>
#include <sys/types.h>

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

static double
elapsed_seconds(struct timeval start, struct timeval end)
{
	return (double)(end.tv_sec - start.tv_sec) +
	    (double)(end.tv_usec - start.tv_usec) / 1000000.0;
}

static uint32_t
record_length(const char *record, size_t remaining)
{
	uint32_t len;

	if (remaining < sizeof(len)) {
		return 0;
	}
	memcpy(&len, record, sizeof(len));
	return len;
}

static void
usage(const char *argv0)
{
	fprintf(stderr,
	    "usage: %s <directory> [full|minimal] [buffer-size] [max-rounds]\n",
	    argv0);
}

int
main(int argc, char **argv)
{
	const char *path;
	const char *mode = "full";
	size_t bufsize = 32768;
	int max_rounds = 100000;
	struct attrlist al;
	char *buf;
	int fd;
	int total = 0;
	int rounds = 0;
	struct timeval start;
	struct timeval end;

	if (argc < 2 || argc > 5) {
		usage(argv[0]);
		return 2;
	}

	path = argv[1];
	if (argc >= 3) {
		mode = argv[2];
	}
	if (argc >= 4) {
		bufsize = (size_t)strtoull(argv[3], NULL, 10);
	}
	if (argc >= 5) {
		max_rounds = atoi(argv[4]);
	}
	if (bufsize < 4096 || max_rounds <= 0) {
		usage(argv[0]);
		return 2;
	}

	memset(&al, 0, sizeof(al));
	al.bitmapcount = ATTR_BIT_MAP_COUNT;
	if (strcmp(mode, "minimal") == 0) {
		al.commonattr = ATTR_CMN_RETURNED_ATTRS | ATTR_CMN_NAME |
		    ATTR_CMN_DEVID | ATTR_CMN_OBJTYPE | ATTR_CMN_FILEID;
		al.fileattr = ATTR_FILE_LINKCOUNT;
	} else if (strcmp(mode, "full") == 0) {
		al.commonattr = ATTR_CMN_RETURNED_ATTRS | ATTR_CMN_NAME |
		    ATTR_CMN_DEVID | ATTR_CMN_OBJTYPE | ATTR_CMN_CRTIME |
		    ATTR_CMN_MODTIME | ATTR_CMN_CHGTIME | ATTR_CMN_ACCTIME |
		    ATTR_CMN_OWNERID | ATTR_CMN_GRPID |
		    ATTR_CMN_ACCESSMASK | ATTR_CMN_FLAGS |
		    ATTR_CMN_FILEID;
		al.fileattr = ATTR_FILE_LINKCOUNT | ATTR_FILE_ALLOCSIZE |
		    ATTR_FILE_IOBLOCKSIZE | ATTR_FILE_DEVTYPE |
		    ATTR_FILE_DATALENGTH;
	} else {
		usage(argv[0]);
		return 2;
	}

	fd = open(path, O_RDONLY | O_NONBLOCK | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	buf = malloc(bufsize);
	if (buf == NULL) {
		perror("malloc");
		close(fd);
		return 1;
	}

	printf("PANTHERA_GETATTRLISTBULK_PATH:%s\n", path);
	printf("PANTHERA_GETATTRLISTBULK_MODE:%s\n", mode);
	printf("PANTHERA_GETATTRLISTBULK_BUFSIZE:%zu\n", bufsize);
	gettimeofday(&start, NULL);

	for (;;) {
		int count;
		const char *cursor;
		size_t remaining;
		int parsed = 0;

		memset(buf, 0, bufsize);
		count = getattrlistbulk(fd, &al, buf, bufsize,
		    FSOPT_PACK_INVAL_ATTRS);
		if (count < 0) {
			perror("getattrlistbulk");
			free(buf);
			close(fd);
			return 1;
		}

		rounds++;
		printf("PANTHERA_GETATTRLISTBULK_ROUND:%d:%d\n", rounds, count);
		if (count == 0) {
			break;
		}

		cursor = buf;
		remaining = bufsize;
		while (parsed < count) {
			uint32_t len = record_length(cursor, remaining);
			if (len == 0 || len > remaining) {
				printf("PANTHERA_GETATTRLISTBULK_BAD_RECORD:%d:%u:%zu\n",
				    parsed, len, remaining);
				free(buf);
				close(fd);
				return 1;
			}
			cursor += len;
			remaining -= len;
			parsed++;
		}

		total += count;
		if (rounds >= max_rounds) {
			printf("PANTHERA_GETATTRLISTBULK_MAX_ROUNDS:%d\n",
			    max_rounds);
			free(buf);
			close(fd);
			return 1;
		}
	}

	gettimeofday(&end, NULL);
	printf("PANTHERA_GETATTRLISTBULK_TOTAL:%d\n", total);
	printf("PANTHERA_GETATTRLISTBULK_SECONDS:%.6f\n",
	    elapsed_seconds(start, end));
	printf("PANTHERA_GETATTRLISTBULK_OK\n");

	free(buf);
	close(fd);
	return 0;
}
