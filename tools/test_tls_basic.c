/*
 * test_tls_basic.c -- test getentropy and basic TLS prerequisites
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>

/* Test getentropy syscall */
extern int getentropy(void *buf, unsigned long len);

int main(void) {
    unsigned char rnd[32];
    int rc, i;

    printf("=== test_tls_basic ===\n");

    /* Test 1: getentropy */
    memset(rnd, 0, sizeof(rnd));
    rc = getentropy(rnd, sizeof(rnd));
    printf("getentropy: rc=%d errno=%d\n", rc, errno);
    printf("random: ");
    for (i = 0; i < 16; i++)
        printf("%02x", rnd[i]);
    printf("\n");

    /* Check if it's all zeros (failure) */
    int allzero = 1;
    for (i = 0; i < 32; i++) {
        if (rnd[i] != 0) { allzero = 0; break; }
    }
    printf("getentropy %s\n", allzero ? "FAIL (all zeros)" : "OK");

    /* Test 2: TCP connect to google:443 */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { printf("socket: %d\n", errno); return 1; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_port = (443 >> 8) | ((443 & 0xff) << 8);  /* htons(443) */
    addr.sin_addr.s_addr = (142) | (251 << 8) | (41 << 16) | (174 << 24);

    printf("connect to 142.251.41.174:443...\n");
    rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    printf("connect=%d errno=%d\n", rc, errno);
    if (rc < 0) { close(fd); return 1; }

    /* Test 3: Write minimal TLS ClientHello and read response */
    /* Minimal TLS 1.0 ClientHello */
    unsigned char hello[] = {
        0x16, 0x03, 0x01, 0x00, 0x45, /* Record: Handshake, TLS 1.0, length 69 */
        0x01, 0x00, 0x00, 0x41,       /* ClientHello, length 65 */
        0x03, 0x03,                    /* TLS 1.2 */
        /* 32 bytes random */
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
        0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
        0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
        0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,
        0x00,                          /* session ID length: 0 */
        0x00, 0x04,                    /* cipher suites length: 4 */
        0x00, 0x2f,                    /* TLS_RSA_WITH_AES_128_CBC_SHA */
        0x00, 0xff,                    /* TLS_EMPTY_RENEGOTIATION_INFO_SCSV */
        0x01, 0x00,                    /* compression methods: 1 method, null */
        0x00, 0x14,                    /* extensions length: 20 */
        /* SNI extension */
        0x00, 0x00, 0x00, 0x10,       /* type=SNI, length=16 */
        0x00, 0x0e, 0x00,             /* list length=14, type=hostname */
        0x00, 0x0b,                    /* hostname length=11 */
        'g','o','o','g','l','e','.','c','o','m', 0x00
    };

    rc = (int)write(fd, hello, sizeof(hello));
    printf("write ClientHello=%d\n", rc);

    /* Read ServerHello */
    unsigned char buf[4096];
    rc = (int)read(fd, buf, sizeof(buf));
    printf("read=%d\n", rc);
    if (rc > 0) {
        printf("response type=0x%02x version=0x%02x%02x length=%d\n",
            buf[0], buf[1], buf[2], (buf[3]<<8)|buf[4]);
    }

    close(fd);
    printf("=== test_tls_basic DONE ===\n");
    return 0;
}
