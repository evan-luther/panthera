/* Test ls output, user identity, and /etc/passwd for issue diagnosis */
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pwd.h>
#include <sys/wait.h>

static void w(const char *s) { write(1, s, strlen(s)); }

int main(void) {
    w("=== issue probe ===\n");

    /* Test 1: run ls / */
    w("-- ls / --\n");
    fflush(stdout);
    pid_t p = fork();
    if (p == 0) {
        char *argv[] = { "ls", "/", NULL };
        char *envp[] = { "PATH=/bin:/sbin:/usr/bin:/usr/sbin", NULL };
        execve("/bin/ls", argv, envp);
        _exit(127);
    }
    if (p > 0) {
        int st; waitpid(p, &st, 0);
        char buf[32]; snprintf(buf, sizeof(buf), "ls exit=%d\n", WEXITSTATUS(st));
        w(buf);
    }

    /* Test 2: run ls -la / */
    w("-- ls -la / --\n");
    fflush(stdout);
    p = fork();
    if (p == 0) {
        char *argv[] = { "ls", "-la", "/", NULL };
        char *envp[] = { "PATH=/bin:/sbin:/usr/bin:/usr/sbin", NULL };
        execve("/bin/ls", argv, envp);
        _exit(127);
    }
    if (p > 0) {
        int st; waitpid(p, &st, 0);
        char buf[32]; snprintf(buf, sizeof(buf), "ls -la exit=%d\n", WEXITSTATUS(st));
        w(buf);
    }

    /* Test 3: user identity */
    w("-- identity --\n");
    char buf[64];
    snprintf(buf, sizeof(buf), "uid=%d gid=%d\n", getuid(), getgid());
    w(buf);

    struct passwd *pw = getpwuid(getuid());
    if (pw) {
        w("getpwuid: "); w(pw->pw_name); w("\n");
    } else {
        w("getpwuid: NULL\n");
    }

    /* Test 4: /etc/passwd readable */
    w("-- /etc/passwd --\n");
    FILE *fp = fopen("/etc/passwd", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            w("  "); w(line);
        }
        fclose(fp);
    } else {
        w("  cannot open\n");
    }

    w("=== done ===\n");
    return 0;
}
