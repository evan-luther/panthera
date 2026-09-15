#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t got_usr1;
static volatile sig_atomic_t got_chld;

static void
putstr(const char *s)
{
    size_t len = 0;
    while (s[len]) {
        len++;
    }
    (void)write(2, s, len);
}

static void
putnum(long value)
{
    char buf[32];
    size_t i = sizeof(buf);
    unsigned long mag;

    if (value == 0) {
        (void)write(2, "0", 1);
        return;
    }

    if (value < 0) {
        (void)write(2, "-", 1);
        mag = (unsigned long)(-value);
    } else {
        mag = (unsigned long)value;
    }

    while (mag != 0 && i != 0) {
        buf[--i] = (char)('0' + (mag % 10));
        mag /= 10;
    }
    (void)write(2, buf + i, sizeof(buf) - i);
}

static void
handler(int sig)
{
    if (sig == SIGUSR1) {
        got_usr1 = 1;
    } else if (sig == SIGCHLD) {
        got_chld = 1;
    }
}

int
main(void)
{
    struct sigaction act;
    struct sigaction ign;
    pid_t pid;
    int status = 0;
    int i;

    putstr("probe: main enter\n");
    memset(&act, 0, sizeof(act));
    memset(&ign, 0, sizeof(ign));
    putstr("probe: after memset\n");
    act.sa_handler = handler;
    ign.sa_handler = SIG_IGN;
    putstr("probe: after sa_handler\n");
    sigemptyset(&act.sa_mask);
    sigemptyset(&ign.sa_mask);
    putstr("probe: after sigemptyset\n");

    putstr("probe: before sigaction usr1\n");
    putstr("sigaction(SIGUSR1,SIG_IGN)=");
    putnum(sigaction(SIGUSR1, &ign, NULL));
    putstr("\n");
    putstr("probe: after sigaction usr1\n");
    putstr("sigaction(SIGCHLD)=");
    putnum(sigaction(SIGCHLD, &act, NULL));
    putstr("\n");
    putstr("probe: after sigaction chld\n");

    putstr("kill(SIGUSR1)=");
    putnum(kill(getpid(), SIGUSR1));
    putstr("\n");
    putstr("after kill: got_usr1=");
    putnum(got_usr1);
    putstr(" got_chld=");
    putnum(got_chld);
    putstr("\n");

    pid = fork();
    putstr("fork=");
    putnum((long)pid);
    putstr("\n");
    if (pid == 0) {
        _exit(7);
    }
    if (pid < 0) {
        return 1;
    }

    for (i = 0; i < 50 && !got_chld; ++i) {
        usleep(10000);
    }
    putstr("after child: got_usr1=");
    putnum(got_usr1);
    putstr(" got_chld=");
    putnum(got_chld);
    putstr("\n");

    putstr("waitpid=");
    putnum((long)waitpid(pid, &status, 0));
    putstr(" status=");
    putnum(status);
    putstr("\n");
    return 0;
}
