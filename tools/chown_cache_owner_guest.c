#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef SF_RESTRICTED
#define SF_RESTRICTED 0x00080000
#endif

static int print_meta(const char *label, const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "%s stat(%s) failed: errno=%d\n", label, path, errno);
        return 1;
    }

    printf("%s uid=%d gid=%d flags=0x%08x\n",
           label, st.st_uid, st.st_gid, (unsigned int)st.st_flags);
    return 0;
}

int main(int argc, char **argv) {
    const char *path = "/System/Library/dyld/dyld_shared_cache_x86_64";
    if (argc > 1)
        path = argv[1];

    (void)print_meta("before", path);

    if (chown(path, 0, 0) != 0) {
        fprintf(stderr, "chown(%s) failed: errno=%d\n", path, errno);
        return 1;
    }

    if (chflags(path, SF_RESTRICTED) != 0) {
        fprintf(stderr, "chflags(%s, SF_RESTRICTED) failed: errno=%d\n",
                path, errno);
        return 1;
    }

    printf("fixed(%s) -> uid=0 gid=0 flags|=SF_RESTRICTED\n", path);
    (void)print_meta("after ", path);
    return 0;
}
