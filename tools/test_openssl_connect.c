/*
 * test_openssl_connect.c -- TLS test using OpenSSL with blocking and non-blocking
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <poll.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>

static int do_tls_test(int nonblocking) {
    int fd, rc, flags;
    struct sockaddr_in addr;

    printf("\n--- TLS test (nonblocking=%d) ---\n", nonblocking);

    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) { printf("SSL_CTX_new failed\n"); ERR_print_errors_fp(stdout); return 1; }
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { printf("socket: %d\n", errno); SSL_CTX_free(ctx); return 1; }

    memset(&addr, 0, sizeof(addr));
    addr.sin_len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_port = (443 >> 8) | ((443 & 0xff) << 8);
    addr.sin_addr.s_addr = (142) | (251 << 8) | (41 << 16) | (174 << 24);

    /* Blocking connect first */
    rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    printf("connect=%d errno=%d\n", rc, errno);
    if (rc < 0) { close(fd); SSL_CTX_free(ctx); return 1; }

    if (nonblocking) {
        flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | 0x0004 /* O_NONBLOCK */);
        printf("set non-blocking\n");
    }

    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, fd);
    printf("SSL_set_fd done\n");

    /* Do TLS handshake */
    int attempts = 0;
    while (1) {
        rc = SSL_connect(ssl);
        attempts++;
        if (rc == 1) {
            printf("SSL_connect OK after %d attempts\n", attempts);
            break;
        }
        int err = SSL_get_error(ssl, rc);
        printf("SSL_connect=%d err=%d (attempt %d)\n", rc, err, attempts);
        if (err == SSL_ERROR_WANT_READ) {
            struct pollfd pfd = { .fd = fd, .events = POLLIN };
            rc = poll(&pfd, 1, 10000);
            printf("poll(read)=%d revents=0x%x\n", rc, pfd.revents);
            if (rc <= 0) { printf("poll timeout/error\n"); break; }
        } else if (err == SSL_ERROR_WANT_WRITE) {
            struct pollfd pfd = { .fd = fd, .events = POLLOUT };
            rc = poll(&pfd, 1, 10000);
            printf("poll(write)=%d revents=0x%x\n", rc, pfd.revents);
            if (rc <= 0) { printf("poll timeout/error\n"); break; }
        } else {
            printf("SSL error: %d\n", err);
            ERR_print_errors_fp(stdout);
            break;
        }
        if (attempts > 20) { printf("too many attempts\n"); break; }
    }

    if (SSL_is_init_finished(ssl)) {
        printf("TLS version: %s\n", SSL_get_version(ssl));
        printf("Cipher: %s\n", SSL_get_cipher(ssl));

        const char *req = "GET / HTTP/1.0\r\nHost: google.com\r\n\r\n";
        rc = SSL_write(ssl, req, strlen(req));
        printf("SSL_write=%d\n", rc);

        char buf[4096];
        rc = SSL_read(ssl, buf, sizeof(buf) - 1);
        printf("SSL_read=%d\n", rc);
        if (rc > 0) {
            buf[rc] = 0;
            if (rc > 80) buf[80] = 0;
            printf("data: %s\n", buf);
        }
    }

    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(fd);
    SSL_CTX_free(ctx);
    printf("--- test done ---\n");
    return 0;
}

int main(void) {
    printf("test_openssl_connect: start\n");
    OPENSSL_init_ssl(0, NULL);

    /* Test 1: Blocking */
    do_tls_test(0);

    /* Test 2: Non-blocking */
    do_tls_test(1);

    printf("test_openssl_connect: all done\n");
    return 0;
}
