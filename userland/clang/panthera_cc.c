#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    static const char *clang_path = "/usr/bin/clang";
    static const char *defaults[] = {
        "-target", "x86_64-apple-darwin23.0",
        "-mmacosx-version-min=14.0",
        "-isysroot", "/",
        "-resource-dir", "/usr/lib/clang/17",
        "-Wno-incompatible-sysroot",
        "-fuse-ld=/usr/bin/ld",
    };
    const int default_count = (int)(sizeof(defaults) / sizeof(defaults[0]));
    char **next_argv = calloc((size_t)argc + (size_t)default_count + 1, sizeof(char *));
    int out = 0;

    if (next_argv == NULL) {
        fprintf(stderr, "cc: allocation failed: errno %d\n", errno);
        return 1;
    }

    next_argv[out++] = (char *)clang_path;
    for (int i = 0; i < default_count; ++i)
        next_argv[out++] = (char *)defaults[i];
    for (int i = 1; i < argc; ++i)
        next_argv[out++] = argv[i];
    next_argv[out] = NULL;

    execv(clang_path, next_argv);
    fprintf(stderr, "cc: exec %s failed: errno %d\n", clang_path, errno);
    free(next_argv);
    return 127;
}
