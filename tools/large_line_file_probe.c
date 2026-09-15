#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	const char *path = "/var/tmp/large-line-file.txt";
	int lines = 300000;
	int fd;
	int i;

	if (argc > 1)
		path = argv[1];
	if (argc > 2)
		lines = atoi(argv[2]);
	if (lines <= 0) {
		fprintf(stderr, "usage: %s [path] [lines]\n", argv[0]);
		return 2;
	}

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd < 0) {
		printf("PANTHERA_LARGE_LINE_OPEN_ERRNO:%d\n", errno);
		return 1;
	}
	for (i = 0; i < lines; i++) {
		const char line[] = "x\n";

		if (write(fd, line, sizeof(line) - 1) != (ssize_t)(sizeof(line) - 1)) {
			printf("PANTHERA_LARGE_LINE_WRITE_ERRNO:%d:%d\n", i, errno);
			close(fd);
			return 1;
		}
	}
	if (close(fd) != 0) {
		printf("PANTHERA_LARGE_LINE_CLOSE_ERRNO:%d\n", errno);
		return 1;
	}
	printf("PANTHERA_LARGE_LINE_FILE:%s\n", path);
	printf("PANTHERA_LARGE_LINE_COUNT:%d\n", lines);
	printf("PANTHERA_LARGE_LINE_OK\n");
	return 0;
}
