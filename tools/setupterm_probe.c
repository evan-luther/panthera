/*
 * Minimal probe: tests setupterm with NO prior ncurses calls.
 * Overrides _nc_err_abort to capture the return address before exiting.
 */
#include <curses.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <term.h>
#include <unistd.h>

static void
w(const char *s)
{
    write(STDOUT_FILENO, s, strlen(s));
}

static void
wi2(const char *label, long a, long b)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "%s%ld errno=%ld\n", label, a, b);
    w(buf);
}

static void
ws2(const char *label, const char *a, const char *b)
{
    char buf[512];
    snprintf(buf, sizeof(buf), "%s%s%s\n", label, a ? a : "(null)", b ? b : "");
    w(buf);
}



int
main(void)
{
    int errret;
    int rc;
    const char *term;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    w("=== setupterm_probe ===\n");

    term = getenv("TERM");
    ws2("TERM=", term, "");
    ws2("HOME=", getenv("HOME"), "");
    ws2("TERMINFO=", getenv("TERMINFO"), "");

    /* Set TERMINFO and unset HOME to minimize code paths */
    setenv("TERMINFO", "/usr/share/terminfo", 1);
    unsetenv("HOME");
    ws2("TERMINFO (after set)=", getenv("TERMINFO"), "");
    ws2("HOME (after unset)=", getenv("HOME"), "");

    /* Test setupterm with ZERO prior ncurses calls */
    errret = 777;
    errno = 0;
    w("before setupterm\n");
    rc = setupterm((char *)(term ? term : "vt100"), STDOUT_FILENO, &errret);
    wi2("setupterm rc=", rc, errret);
    wi2("setupterm errno=", errno, 0);

    if (cur_term != NULL) {
        ws2("termname()=", termname(), "");

        if (clear_screen != NULL) {
            w("clear_screen capability found, emitting:\n");
            putp(clear_screen);
            w("clear done\n");
        } else {
            w("clear_screen capability is NULL\n");
        }
    } else {
        w("cur_term is NULL - setupterm failed\n");
    }

    w("=== setupterm_probe done ===\n");
    return rc;
}
