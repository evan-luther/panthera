/* test_select.c — verify select() works on TCP sockets */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <errno.h>
#include <poll.h>

int main(void) {
    printf("test_select: START\n");

    /* Create TCP loopback pair */
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(19999);
    sa.sin_addr.s_addr = htonl(0x7f000001);
    int one = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    bind(lfd, (struct sockaddr *)&sa, sizeof(sa));
    listen(lfd, 1);

    int cfd = socket(AF_INET, SOCK_STREAM, 0);
    connect(cfd, (struct sockaddr *)&sa, sizeof(sa));
    int afd = accept(lfd, NULL, NULL);
    printf("test_select: TCP pair cfd=%d afd=%d\n", cfd, afd);

    write(afd, "SELECTTEST", 10);
    usleep(200000);

    /* Test: select for read using standard FD_SET */
    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(cfd, &rset);
    struct timeval tv = {5, 0};
    struct timeval t0, t1;
    gettimeofday(&t0, NULL);
    int rc = select(cfd + 1, &rset, NULL, NULL, &tv);
    gettimeofday(&t1, NULL);
    long elapsed_ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_usec - t0.tv_usec) / 1000;

    int isset = FD_ISSET(cfd, &rset);
    printf("test_select: select rc=%d isset=%d elapsed=%ldms\n", rc, isset, elapsed_ms);

    if (rc == 1 && isset) {
        char buf[64];
        int nr = read(cfd, buf, sizeof(buf) - 1);
        if (nr > 0) { buf[nr] = '\0'; printf("test_select: read '%s'\n", buf); }
        printf("test_select: PASS\n");
    } else {
        printf("test_select: FAIL (expected rc=1 isset=1)\n");
    }

    close(afd); close(cfd); close(lfd);
    return 0;
}
