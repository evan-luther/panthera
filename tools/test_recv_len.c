/*
 * test_recv_len.c -- verify recv() respects the len parameter
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>

int main(void) {
    int fd, rc;
    struct sockaddr_in addr;
    char buf[4096];

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { printf("socket: %d\n", errno); return 1; }

    memset(&addr, 0, sizeof(addr));
    addr.sin_len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_port = (80 >> 8) | ((80 & 0xff) << 8);
    addr.sin_addr.s_addr = (142) | (251 << 8) | (41 << 16) | (174 << 24);

    rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc < 0) { printf("connect: %d\n", errno); close(fd); return 1; }

    const char *req = "GET / HTTP/1.0\r\nHost: google.com\r\n\r\n";
    write(fd, req, strlen(req));
    printf("sent request\n");

    /* Wait for response */
    sleep(2);

    /* Test recv with small len */
    memset(buf, 0, sizeof(buf));
    printf("calling recv(fd, buf, 5, 0)...\n");
    rc = (int)recv(fd, buf, 5, 0);
    printf("recv(5) returned %d, errno=%d\n", rc, errno);
    printf("buf[0..9]: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
        (unsigned char)buf[0], (unsigned char)buf[1], (unsigned char)buf[2],
        (unsigned char)buf[3], (unsigned char)buf[4], (unsigned char)buf[5],
        (unsigned char)buf[6], (unsigned char)buf[7], (unsigned char)buf[8],
        (unsigned char)buf[9]);

    /* Test read with small len */
    memset(buf, 0, sizeof(buf));
    printf("calling read(fd, buf, 5)...\n");
    rc = (int)read(fd, buf, 5);
    printf("read(5) returned %d, errno=%d\n", rc, errno);
    printf("buf[0..9]: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
        (unsigned char)buf[0], (unsigned char)buf[1], (unsigned char)buf[2],
        (unsigned char)buf[3], (unsigned char)buf[4], (unsigned char)buf[5],
        (unsigned char)buf[6], (unsigned char)buf[7], (unsigned char)buf[8],
        (unsigned char)buf[9]);

    close(fd);
    printf("test_recv_len done\n");
    return 0;
}
