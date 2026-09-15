#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <poll.h>

/* UNIX2003 variant trampolines */
extern int close(int);
int close_u(int fd) __asm__("_close$UNIX2003");
int close_u(int fd) { return close(fd); }

extern int connect(int, const struct sockaddr *, socklen_t);
int connect_u(int a, const struct sockaddr *b, socklen_t c) __asm__("_connect$UNIX2003");
int connect_u(int a, const struct sockaddr *b, socklen_t c) { return connect(a,b,c); }

extern int poll(struct pollfd *, unsigned int, int);
int poll_u(struct pollfd *a, unsigned int b, int c) __asm__("_poll$UNIX2003");
int poll_u(struct pollfd *a, unsigned int b, int c) { return poll(a,b,c); }

extern ssize_t read(int, void *, size_t);
ssize_t read_u(int a, void *b, size_t c) __asm__("_read$UNIX2003");
ssize_t read_u(int a, void *b, size_t c) { return read(a,b,c); }

extern ssize_t write(int, const void *, size_t);
ssize_t write_u(int a, const void *b, size_t c) __asm__("_write$UNIX2003");
ssize_t write_u(int a, const void *b, size_t c) { return write(a,b,c); }

int main(void) {
    int fd, rc;
    struct sockaddr_in addr;
    char buf[4096];
    struct pollfd pfd;
    const char *req = "GET / HTTP/1.0\r\nHost: google.com\r\n\r\n";

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { printf("socket failed\n"); return 1; }

    memset(&addr, 0, sizeof(addr));
    addr.sin_len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_port = (80 >> 8) | ((80 & 0xff) << 8);
    addr.sin_addr.s_addr = (142) | (251 << 8) | (41 << 16) | (174 << 24);

    printf("test_tcp_recv: connecting to 142.251.41.174:80...\n");
    rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    printf("connect rc=%d\n", rc);
    if (rc < 0) { close(fd); return 1; }

    printf("sending HTTP/1.0 request\n");
    rc = (int)write(fd, req, strlen(req));
    printf("write rc=%d\n", rc);

    printf("polling for response (5s timeout)...\n");
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    rc = poll(&pfd, 1, 5000);
    printf("poll rc=%d revents=0x%x\n", rc, pfd.revents);

    if (rc > 0 && (pfd.revents & POLLIN)) {
        rc = (int)read(fd, buf, sizeof(buf) - 1);
        printf("read rc=%d\n", rc);
        if (rc > 0) {
            buf[rc] = '\0';
            if (rc > 300) buf[300] = '\0';
            printf("data:\n%s\n", buf);
        }
    } else {
        printf("NO DATA (poll timed out or error)\n");
    }

    close(fd);
    printf("test_tcp_recv: DONE\n");
    return 0;
}
