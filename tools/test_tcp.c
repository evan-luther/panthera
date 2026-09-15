#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

int main(void) {
    int fd;
    struct sockaddr_in addr;

    printf("test_tcp: creating TCP socket\n");
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        printf("FAIL: socket() failed\n");
        return 1;
    }
    printf("test_tcp: socket created fd=%d\n", fd);

    memset(&addr, 0, sizeof(addr));
    addr.sin_len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_port = (80 >> 8) | ((80 & 0xff) << 8); /* htons(80) */
    addr.sin_addr.s_addr = (10) | (0 << 8) | (2 << 16) | (2 << 24); /* 10.0.2.2 */

    printf("test_tcp: connecting to 10.0.2.2:80...\n");
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("test_tcp: connect failed (expected)\n");
        close(fd);
        return 1;
    }
    printf("test_tcp: connected!\n");
    close(fd);
    return 0;
}
