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
    if (fd < 0) { printf("socket failed\n"); return 1; }

    memset(&addr, 0, sizeof(addr));
    addr.sin_len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_port = (80 >> 8) | ((80 & 0xff) << 8); /* port 80, not 443 */
    addr.sin_addr.s_addr = (142) | (251 << 8) | (41 << 16) | (174 << 24);

    printf("connecting to :80...\n");
    rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    printf("connect=%d\n", rc);
    if (rc < 0) { close(fd); return 1; }

    /* DON'T set non-blocking. Use blocking I/O. */
    const char *req = "GET / HTTP/1.0\r\nHost: google.com\r\n\r\n";
    rc = (int)write(fd, req, strlen(req));
    printf("wrote=%d\n", rc);

    /* Blocking read */
    printf("blocking read...\n");
    rc = (int)read(fd, buf, sizeof(buf)-1);
    printf("read=%d\n", rc);
    if (rc > 0) {
        buf[rc] = 0;
        if (rc > 200) buf[200] = 0;
        printf("%s\n", buf);
    }

    close(fd);
    printf("done\n");
    return 0;
}
