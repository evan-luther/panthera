/*
 * test_poll_tcp.c -- test poll(POLLIN) on a TCP socket
 *
 * Connects to google.com:80 via QEMU SLIRP DNS (10.0.2.3),
 * sends GET request, then uses poll() to wait for data.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>

int main(void) {
    int fd, rc;
    struct sockaddr_in addr;
    char buf[4096];

    printf("test_poll_tcp: creating socket\n");
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { printf("socket failed: %d\n", errno); return 1; }

    memset(&addr, 0, sizeof(addr));
    addr.sin_len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_port = (80 >> 8) | ((80 & 0xff) << 8); /* htons(80) */
    /* 142.251.41.174 = google.com */
    addr.sin_addr.s_addr = (142) | (251 << 8) | (41 << 16) | (174 << 24);

    printf("test_poll_tcp: connect...\n");
    rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    printf("test_poll_tcp: connect=%d errno=%d\n", rc, errno);
    if (rc < 0) { close(fd); return 1; }

    const char *req = "GET / HTTP/1.0\r\nHost: google.com\r\n\r\n";
    rc = (int)write(fd, req, strlen(req));
    printf("test_poll_tcp: wrote=%d\n", rc);

    /* Now test poll(POLLIN) */
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    printf("test_poll_tcp: calling poll(POLLIN, 10000)...\n");
    rc = poll(&pfd, 1, 10000);
    printf("test_poll_tcp: poll=%d revents=0x%x\n", rc, pfd.revents);

    if (rc > 0 && (pfd.revents & POLLIN)) {
        rc = (int)read(fd, buf, sizeof(buf) - 1);
        printf("test_poll_tcp: read=%d\n", rc);
        if (rc > 0) {
            buf[rc] = 0;
            if (rc > 100) buf[100] = 0;
            printf("test_poll_tcp: data=%s\n", buf);
        }
        printf("test_poll_tcp: PASS\n");
    } else {
        printf("test_poll_tcp: FAIL (poll timeout or no POLLIN)\n");

        /* Try blocking read to prove data is there */
        printf("test_poll_tcp: trying blocking read anyway...\n");
        rc = (int)read(fd, buf, sizeof(buf) - 1);
        printf("test_poll_tcp: blocking_read=%d\n", rc);
        if (rc > 0) {
            buf[rc] = 0;
            if (rc > 100) buf[100] = 0;
            printf("test_poll_tcp: data_was_there=%s\n", buf);
        }
    }

    close(fd);
    return 0;
}
