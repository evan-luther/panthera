/*
 * Minimal notify test with SIGILL handler and breadcrumbs.
 */
#include <notify.h>
#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>

static void msg(const char *s) {
    write(STDERR_FILENO, s, strlen(s));
}

static void sigill_handler(int sig) {
    (void)sig;
    msg("!!! SIGILL caught !!!\n");
    _exit(99);
}

int main(void) {
    signal(SIGILL, sigill_handler);
    signal(SIGBUS, sigill_handler);
    signal(SIGSEGV, sigill_handler);

    msg("A: before register_check\n");

    int token = -1;
    uint32_t status = notify_register_check("com.test.minimal", &token);

    char buf[128];
    snprintf(buf, sizeof(buf), "B: register status=%u token=%d\n", status, token);
    msg(buf);

    msg("C: before post\n");
    status = notify_post("com.test.minimal");
    snprintf(buf, sizeof(buf), "D: post status=%u\n", status);
    msg(buf);

    msg("E: before check\n");
    int changed = 0;
    status = notify_check(token, &changed);
    snprintf(buf, sizeof(buf), "F: check status=%u changed=%d\n", status, changed);
    msg(buf);

    msg("G: before cancel\n");
    status = notify_cancel(token);
    snprintf(buf, sizeof(buf), "H: cancel status=%u\n", status);
    msg(buf);

    msg("Z: all done\n");
    return 0;
}
