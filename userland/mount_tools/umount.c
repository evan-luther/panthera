#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>

static void
usage(const char *prog)
{
	fprintf(stderr, "usage: %s [-f] path\n", prog);
}

int
main(int argc, char **argv)
{
	int flags = 0;
	const char *path = NULL;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-f") == 0) {
			flags |= MNT_FORCE;
		} else if (argv[i][0] == '-') {
			usage(argv[0]);
			return (2);
		} else if (path == NULL) {
			path = argv[i];
		} else {
			usage(argv[0]);
			return (2);
		}
	}

	if (path == NULL) {
		usage(argv[0]);
		return (2);
	}

	if (unmount(path, flags) != 0) {
		fprintf(stderr, "%s: %s: ", argv[0], path);
		perror(NULL);
		return (1);
	}

	return (0);
}
