#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

static long long tv_delta_us(const struct timeval *start, const struct timeval *end) {
    long long sec = (long long)end->tv_sec - (long long)start->tv_sec;
    long long usec = (long long)end->tv_usec - (long long)start->tv_usec;
    return sec * 1000000LL + usec;
}

static void print_ms(const char *label, long long usec) {
    long long ms = usec / 1000LL;
    long long frac = (usec % 1000LL + 1000LL) % 1000LL;
    printf("%s %lld.%03lld\n", label, ms / 1000LL, frac);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s command [args...]\n", argv[0]);
        return 2;
    }

    struct timeval start, end;
    struct rusage before, after;
    if (gettimeofday(&start, NULL) != 0 || getrusage(RUSAGE_CHILDREN, &before) != 0) {
        perror("timing setup");
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }
    if (pid == 0) {
        execv(argv[1], &argv[1]);
        fprintf(stderr, "execv(%s) failed: %s\n", argv[1], strerror(errno));
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return 1;
    }
    if (gettimeofday(&end, NULL) != 0 || getrusage(RUSAGE_CHILDREN, &after) != 0) {
        perror("timing finish");
        return 1;
    }

    long long real = tv_delta_us(&start, &end);
    long long user = tv_delta_us(&before.ru_utime, &after.ru_utime);
    long long sys  = tv_delta_us(&before.ru_stime, &after.ru_stime);

    print_ms("real", real);
    print_ms("user", user);
    print_ms("sys", sys);

    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return 1;
}
