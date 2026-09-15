#define NCURSES_INTERNALS 1
#include <curses.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <term.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <nc_tparm.h>

extern const char *_nc_get_source(void);
extern int _nc_read_entry(const char *const, char *const, TERMTYPE *const);
extern int _nc_read_file_entry(const char *const, TERMTYPE *const);

static void
w(const char *s)
{
    write(STDOUT_FILENO, s, strlen(s));
}

static void
wp(const char *label, const void *p)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%s%p\n", label, p);
    w(buf);
}

static void
wi2(const char *label, long a, long b)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "%s%ld errno=%ld\n", label, a, b);
    w(buf);
}

static void
wi1(const char *label, long a)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "%s%ld\n", label, a);
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
    const char *term = getenv("TERM");
    char tcbuf[2048];
    void *p1;
    void *p2;
    char *copy;
    int errret;
    int rc;
    char filename[1024];
    TERMTYPE ttype;
    int fd;
    char raw[4096];
    ssize_t nread;
    struct stat st;
    FILE *fp;
    size_t nfile;
    SCREEN *screen;
    WINDOW *win;
    struct winsize ws;
    char *tty;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    w("=== ncurses probe ===\n");
    ws2("TERM=", term, "");
    wi1("isatty(stdin)=", isatty(STDIN_FILENO));
    wi1("isatty(stdout)=", isatty(STDOUT_FILENO));
    tty = ttyname(STDIN_FILENO);
    ws2("ttyname(stdin)=", tty, "");
    memset(&ws, 0, sizeof(ws));
    errno = 0;
    rc = ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
    wi2("ioctl(TIOCGWINSZ) rc=", rc, errno);
    wi1("winsize rows=", ws.ws_row);
    wi1("winsize cols=", ws.ws_col);

    wi1("sizeof(TERMINAL)=", (long)sizeof(TERMINAL));
    wi1("sizeof(TERMTYPE)=", (long)sizeof(TERMTYPE));

    errno = 0;
    p1 = malloc(16);
    wp("malloc(16)=", p1);
    wi2("malloc errno=", errno, 0);

    errno = 0;
    p2 = calloc(4, 8);
    wp("calloc(4,8)=", p2);
    wi2("calloc errno=", errno, 0);

    errno = 0;
    p1 = realloc(p1, 64);
    wp("realloc(64)=", p1);
    wi2("realloc errno=", errno, 0);

    errno = 0;
    copy = strdup("probe");
    wp("strdup=", copy);
    wi2("strdup errno=", errno, 0);

    if (p1 != NULL) {
        memset(p1, 0x41, 64);
    }
    if (p2 != NULL) {
        memset(p2, 0x42, 32);
    }

    /* Test larger allocations matching ncurses internal sizes */
    {
        static const size_t sizes[] = {
            256, 512, 1024, 2048, 3312, 4096, 8192, 16384, 32768
        };
        void *tp;
        char lbl[64];
        int i;
        for (i = 0; i < (int)(sizeof(sizes)/sizeof(sizes[0])); i++) {
            errno = 0;
            tp = calloc(1, sizes[i]);
            snprintf(lbl, sizeof(lbl), "calloc(1,%zu)=", sizes[i]);
            wp(lbl, tp);
            wi2("  errno=", errno, 0);
            if (tp != NULL) {
                memset(tp, 0xCC, sizes[i]);
                free(tp);
            }
        }
    }

    memset(&ttype, 0, sizeof(ttype));
    errno = 0;
    fd = open("/usr/share/terminfo/76/vt100", O_RDONLY, 0);
    wi2("open vt100 fd=", fd, errno);
    if (fd >= 0) {
        errno = 0;
        nread = read(fd, raw, sizeof(raw));
        wi2("read vt100 bytes=", nread, errno);
        if (nread > 0) {
            memset(&ttype, 0, sizeof(ttype));
            errno = 0;
            rc = _nc_read_termtype(&ttype, raw, (int)nread);
            wi2("_nc_read_termtype rc=", rc, errno);
        }
        close(fd);
    }

    errno = 0;
    rc = access("/usr/share/terminfo/76/vt100", R_OK);
    wi2("access vt100 rc=", rc, errno);

    memset(&st, 0, sizeof(st));
    errno = 0;
    rc = stat("/usr/share/terminfo/76/vt100", &st);
    wi2("stat vt100 rc=", rc, errno);
    wi2("stat vt100 size=", rc == 0 ? (long) st.st_size : -1L, 0);

    errno = 0;
    fp = fopen("/usr/share/terminfo/76/vt100", "rb");
    wp("fopen vt100=", fp);
    wi2("fopen vt100 errno=", errno, 0);
    if (fp != NULL) {
        memset(raw, 0, sizeof(raw));
        errno = 0;
        nfile = fread(raw, 1, sizeof(raw), fp);
        wi2("fread vt100 bytes=", (long) nfile, errno);
        wi2("fread vt100 ferror=", ferror(fp), feof(fp));
        fclose(fp);
    }

    memset(&ttype, 0, sizeof(ttype));
    memset(filename, 0, sizeof(filename));
    errno = 0;
    rc = _nc_read_file_entry("/usr/share/terminfo/76/vt100", &ttype);
    wi2("_nc_read_file_entry rc=", rc, errno);

    /* Re-zero ttype to avoid dirty state from _nc_read_file_entry */
    memset(&ttype, 0, sizeof(ttype));
    memset(filename, 0, sizeof(filename));

    /* Print HOME before testing _nc_read_entry */
    {
        char *hv = getenv("HOME");
        ws2("HOME=", hv, "");
        if (hv != NULL) {
            wi1("strlen(HOME)=", (long)strlen(hv));
        }
    }

    /* Test 1: _nc_read_entry with TERMINFO set AND HOME unset */
    setenv("TERMINFO", "/usr/share/terminfo", 1);
    unsetenv("HOME");
    w("before _nc_read_entry (TERMINFO set, HOME unset)\n");
    errno = 0;
    rc = _nc_read_entry(term ? term : "vt100", filename, &ttype);
    wi2("_nc_read_entry rc=", rc, errno);
    ws2("_nc_read_entry file=", filename, "");
    ws2("_nc_get_source()=", _nc_get_source(), "");

    errret = 777;
    errno = 0;
    w("before setupterm\n");
    rc = setupterm((char *)(term ? term : "vt100"), STDOUT_FILENO, &errret);
    wi2("setupterm rc=", rc, errret);
    wi2("setupterm errno=", errno, 0);
    if (cur_term != NULL) {
        ws2("termname()=", termname(), "");
    } else {
        w("cur_term name=(null)\n");
    }

    w("before TIPARM literal\n");
    errno = 0;
    copy = TIPARM_2("\033[%i%p1%d;%p2%dH", 23, 23);
    ws2("TIPARM literal=", copy, "");
    wi2("TIPARM literal errno=", errno, 0);
    if (cur_term != NULL && cursor_address != NULL) {
        w("before TIPARM cursor_address\n");
        errno = 0;
        copy = TIPARM_2(cursor_address, 23, 23);
        ws2("TIPARM cursor_address=", copy, "");
        wi2("TIPARM cursor_address errno=", errno, 0);
    }

    memset(tcbuf, 0, sizeof(tcbuf));
    errno = 0;
    w("before tgetent\n");
    rc = tgetent(tcbuf, term ? term : "vt100");
    wi2("tgetent rc=", rc, errno);
    if (rc == 1) {
        char *cl = tgetstr("cl", NULL);
        ws2("tgetstr(cl)=", cl, "");
    }

    if (cur_term != NULL) {
        del_curterm(cur_term);
    }

    /* Test setupterm with explicit TERMINFO to bypass db iterator */
    w("before setupterm with TERMINFO=/usr/share/terminfo\n");
    setenv("TERMINFO", "/usr/share/terminfo", 1);
    errret = 777;
    errno = 0;
    rc = setupterm((char *)(term ? term : "vt100"), STDOUT_FILENO, &errret);
    wi2("setupterm+TERMINFO rc=", rc, errret);
    wi2("setupterm+TERMINFO errno=", errno, 0);
    if (cur_term != NULL) {
        ws2("termname+TERMINFO()=", termname(), "");
        del_curterm(cur_term);
    }

    w("before newterm\n");
    errno = 0;
    screen = newterm((char *)(term ? term : "vt100"), stdout, stdin);
    wp("newterm()=", screen);
    wi2("newterm errno=", errno, 0);
    if (screen != NULL) {
        wi1("LINES=", LINES);
        wi1("COLS=", COLS);
        w("before newwin\n");
        errno = 0;
        win = newwin(1, 1, 0, 0);
        wp("newwin()=", win);
        wi2("newwin errno=", errno, 0);
        if (win != NULL) {
            delwin(win);
        }
        w("before endwin\n");
        wi2("endwin rc=", endwin(), errno);
        delscreen(screen);
    }

    free(copy);
    free(p2);
    free(p1);

    w("=== done ===\n");
    return 0;
}
