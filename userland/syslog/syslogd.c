/*
 * Panthera minimal syslogd
 *
 * Listens on /var/run/syslog (Unix domain socket) and /var/run/log,
 * reads BSD syslog protocol messages, appends to /var/log/system.log
 * with timestamps. Runs in the foreground for launchd.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>

#define SYSLOG_PATH    "/var/run/syslog"
#define LOG_PATH_ALT   "/var/run/log"
#define OUTPUT_LOG     "/var/log/system.log"
#define MAX_MSG        8192

static volatile sig_atomic_t running = 1;
static FILE *logfp = NULL;

static void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

static const char *facility_name(int fac) {
    static const char *names[] = {
        "kern", "user", "mail", "daemon", "auth", "syslog",
        "lpr", "news", "uucp", "cron", "authpriv", "ftp",
        "ntp", "audit", "alert", "clock",
        "local0", "local1", "local2", "local3",
        "local4", "local5", "local6", "local7"
    };
    if (fac >= 0 && fac < 24) return names[fac];
    return "unknown";
}

static const char *severity_name(int sev) {
    static const char *names[] = {
        "emerg", "alert", "crit", "err",
        "warning", "notice", "info", "debug"
    };
    if (sev >= 0 && sev < 8) return names[sev];
    return "unknown";
}

static int create_socket(const char *path) {
    struct sockaddr_un addr;
    int fd;

    unlink(path);

    fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) {
        fprintf(stderr, "syslogd: socket(%s): %s\n", path, strerror(errno));
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "syslogd: bind(%s): %s\n", path, strerror(errno));
        close(fd);
        return -1;
    }

    chmod(path, 0666);
    return fd;
}

static void process_message(const char *buf, ssize_t len) {
    int pri = 13; /* default: user.notice */
    const char *msg = buf;
    char timestamp[64];
    time_t now;
    struct tm tm;

    if (len <= 0) return;

    /* Parse priority: <N> at start of message */
    if (buf[0] == '<') {
        const char *p = buf + 1;
        pri = 0;
        while (*p >= '0' && *p <= '9' && p < buf + 5) {
            pri = pri * 10 + (*p - '0');
            p++;
        }
        if (*p == '>') {
            msg = p + 1;
        } else {
            pri = 13;
            msg = buf;
        }
    }

    int facility = (pri >> 3) & 0x1f;
    int severity = pri & 0x07;

    now = time(NULL);
    localtime_r(&now, &tm);
    strftime(timestamp, sizeof(timestamp), "%b %e %H:%M:%S", &tm);

    if (logfp) {
        fprintf(logfp, "%s panthera %s.%s: %s\n",
                timestamp,
                facility_name(facility),
                severity_name(severity),
                msg);
        fflush(logfp);
    }

    /* Also print to stderr (captured by launchd) */
    fprintf(stderr, "%s %s.%s: %s\n",
            timestamp,
            facility_name(facility),
            severity_name(severity),
            msg);
}

int main(int argc, char **argv) {
    int sock1, sock2 = -1;
    char buf[MAX_MSG + 1];
    ssize_t n;

    (void)argc;
    (void)argv;

    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);

    logfp = fopen(OUTPUT_LOG, "a");
    if (!logfp) {
        fprintf(stderr, "syslogd: cannot open %s: %s\n",
                OUTPUT_LOG, strerror(errno));
        /* Continue anyway — we still log to stderr */
    }

    sock1 = create_socket(SYSLOG_PATH);
    if (sock1 < 0) {
        fprintf(stderr, "syslogd: fatal: cannot create primary socket\n");
        return 1;
    }

    /* Also listen on /var/run/log (some programs use this) */
    sock2 = create_socket(LOG_PATH_ALT);

    fprintf(stderr, "syslogd: listening on %s", SYSLOG_PATH);
    if (sock2 >= 0) fprintf(stderr, " and %s", LOG_PATH_ALT);
    fprintf(stderr, "\n");

    if (logfp) {
        time_t now = time(NULL);
        struct tm tm;
        char timestamp[64];
        localtime_r(&now, &tm);
        strftime(timestamp, sizeof(timestamp), "%b %e %H:%M:%S", &tm);
        fprintf(logfp, "%s panthera syslogd[%d]: syslogd started\n",
                timestamp, getpid());
        fflush(logfp);
    }

    while (running) {
        fd_set rfds;
        int maxfd;
        struct timeval tv;

        FD_ZERO(&rfds);
        FD_SET(sock1, &rfds);
        maxfd = sock1;
        if (sock2 >= 0) {
            FD_SET(sock2, &rfds);
            if (sock2 > maxfd) maxfd = sock2;
        }

        tv.tv_sec = 5;
        tv.tv_usec = 0;

        int ret = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "syslogd: select: %s\n", strerror(errno));
            break;
        }
        if (ret == 0) continue; /* timeout — check running flag */

        if (FD_ISSET(sock1, &rfds)) {
            n = recv(sock1, buf, MAX_MSG, 0);
            if (n > 0) {
                buf[n] = '\0';
                process_message(buf, n);
            }
        }

        if (sock2 >= 0 && FD_ISSET(sock2, &rfds)) {
            n = recv(sock2, buf, MAX_MSG, 0);
            if (n > 0) {
                buf[n] = '\0';
                process_message(buf, n);
            }
        }
    }

    fprintf(stderr, "syslogd: shutting down\n");
    if (logfp) {
        fprintf(logfp, "syslogd: shutting down\n");
        fclose(logfp);
    }

    close(sock1);
    if (sock2 >= 0) close(sock2);
    unlink(SYSLOG_PATH);
    unlink(LOG_PATH_ALT);

    return 0;
}
