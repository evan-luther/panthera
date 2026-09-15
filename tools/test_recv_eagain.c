/*
 * test_recv_eagain.c -- test carry flag after failed syscall
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>

/* Direct syscall with carry flag check */
static long my_recv(int s, void *buf, unsigned long len, int flags) {
    long ret;
    long carry;
    register long r10 __asm__("r10") = flags;
    register long r8 __asm__("r8") = 0;
    register long r9 __asm__("r9") = 0;
    __asm__ volatile(
        "syscall\n\t"
        "jnc 1f\n\t"
        "movq $1, %[cf]\n\t"
        "jmp 2f\n"
        "1: movq $0, %[cf]\n"
        "2:"
        : "=a"(ret), [cf]"=r"(carry)
        : "a"(0x200001d), "D"((long)s), "S"((long)buf), "d"((long)len),
          "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory", "cc");
    printf("  raw syscall: ret=%ld carry=%ld\n", ret, carry);
    if (carry) {
        errno = (int)ret;
        return -1;
    }
    return ret;
}

int main(void) {
    int fd, rc, flags;
    struct sockaddr_in addr;
    char buf[64];

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { printf("socket: %d\n", errno); return 1; }

    memset(&addr, 0, sizeof(addr));
    addr.sin_len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_port = (80 >> 8) | ((80 & 0xff) << 8);
    addr.sin_addr.s_addr = (142) | (251 << 8) | (41 << 16) | (174 << 24);

    rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc < 0) { printf("connect failed: %d\n", errno); close(fd); return 1; }

    /* Set non-blocking */
    flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | 0x0004);

    /* Test 1: my_recv (with carry flag check) */
    printf("test my_recv (direct syscall with carry check):\n");
    errno = 0;
    rc = (int)my_recv(fd, buf, 5, 0);
    printf("  my_recv = %d, errno = %d\n", rc, errno);
    if (rc == -1 && errno == 35) printf("  PASS\n");
    else printf("  FAIL\n");

    /* Test 2: library recv */
    printf("test recv (library):\n");
    errno = 0;
    rc = (int)recv(fd, buf, 5, 0);
    printf("  recv = %d, errno = %d\n", rc, errno);
    if (rc == -1 && errno == 35) printf("  PASS\n");
    else if (rc == 35) printf("  FAIL: raw EAGAIN returned\n");
    else printf("  result: rc=%d errno=%d\n", rc, errno);

    close(fd);
    return 0;
}
