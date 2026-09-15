/*
 * test_nbio_select.c -- test select() on TCP socket
 *
 * Uses raw bit manipulation to avoid any FD_SET macro issues.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <poll.h>

/* Raw fd_set bit manipulation to avoid macro issues */
static void raw_fdset(fd_set *s, int fd) {
    unsigned char *p = (unsigned char *)s;
    p[fd / 8] |= (1 << (fd % 8));
}

static int raw_fdisset(fd_set *s, int fd) {
    unsigned char *p = (unsigned char *)s;
    return (p[fd / 8] >> (fd % 8)) & 1;
}

int main(void) {
    int fd, rc, flags, so_err;
    unsigned int so_len;
    struct sockaddr_in addr;
    char buf[4096];

    printf("=== test_nbio_select ===\n");

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { printf("socket: %d\n", errno); return 1; }
    printf("socket fd=%d\n", fd);

    /* Print fd_set details */
    printf("sizeof(fd_set)=%d\n", (int)sizeof(fd_set));

    /* Set non-blocking */
    flags = fcntl(fd, F_GETFL, 0);
    rc = fcntl(fd, F_SETFL, flags | 0x0004 /* O_NONBLOCK */);
    printf("F_SETFL(NONBLOCK)=%d\n", rc);

    memset(&addr, 0, sizeof(addr));
    addr.sin_len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_port = (80 >> 8) | ((80 & 0xff) << 8);
    addr.sin_addr.s_addr = (142) | (251 << 8) | (41 << 16) | (174 << 24);

    printf("connect (non-blocking)...\n");
    rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    printf("connect=%d errno=%d\n", rc, errno);

    if (rc < 0 && errno == 36 /* EINPROGRESS */) {
        /* Test 1: select(write) using RAW bit set */
        fd_set wfds;
        struct timeval tv;
        memset(&wfds, 0, sizeof(wfds));
        raw_fdset(&wfds, fd);
        /* Verify the bits are set */
        printf("fd_set raw check: fd=%d byte[%d]=0x%02x isset=%d first_word=0x%08x\n",
            fd, fd/8, ((unsigned char *)&wfds)[fd/8], raw_fdisset(&wfds, fd),
            *(unsigned int *)&wfds);
        tv.tv_sec = 10;
        tv.tv_usec = 0;

        printf("select(write, 10s) fd=%d...\n", fd);
        rc = select(fd + 1, NULL, &wfds, NULL, &tv);
        printf("select(write)=%d isset=%d\n", rc, raw_fdisset(&wfds, fd));

        /* Check SO_ERROR */
        so_err = -1;
        so_len = sizeof(so_err);
        rc = getsockopt(fd, 0xffff, 0x1007, &so_err, &so_len);
        printf("getsockopt(SO_ERROR)=%d so_err=%d\n", rc, so_err);
    } else if (rc == 0) {
        printf("connected immediately\n");
    } else {
        printf("connect failed\n");
        close(fd);
        return 1;
    }

    /* Send HTTP request */
    const char *req = "GET / HTTP/1.0\r\nHost: google.com\r\n\r\n";
    rc = (int)write(fd, req, strlen(req));
    printf("write=%d\n", rc);

    /* Test 2: select(read) using RAW bit set */
    {
        fd_set rfds;
        struct timeval tv;
        memset(&rfds, 0, sizeof(rfds));
        raw_fdset(&rfds, fd);
        printf("select(read) raw check: first_word=0x%08x addr=%p\n",
            *(unsigned int *)&rfds, (void *)&rfds);
        tv.tv_sec = 10;
        tv.tv_usec = 0;

        printf("select(read, 10s) fd=%d...\n", fd);
        rc = select(fd + 1, &rfds, NULL, NULL, &tv);
        printf("select(read)=%d isset=%d\n", rc, raw_fdisset(&rfds, fd));
    }

    /* Test 3: poll for comparison */
    {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        printf("poll(POLLIN, 5s)...\n");
        rc = poll(&pfd, 1, 5000);
        printf("poll=%d revents=0x%x\n", rc, pfd.revents);
    }

    /* Try read */
    rc = (int)read(fd, buf, sizeof(buf) - 1);
    printf("read=%d\n", rc);
    if (rc > 0) {
        buf[rc] = 0;
        if (rc > 60) buf[60] = 0;
        printf("data: %s\n", buf);
    }

    close(fd);
    printf("=== test_nbio_select DONE ===\n");
    return 0;
}
