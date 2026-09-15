/* Stub libsystem_trace — os_log writes to stderr until real logging exists. */

#include <stdarg.h>
#include <stdio.h>

void *_os_log_default = (void *)1;

void *os_log_create(const char *subsystem, const char *category) {
    (void)subsystem;
    (void)category;
    return _os_log_default;
}

int os_log_type_enabled(void *log, int type) {
    (void)log;
    (void)type;
    return 0;
}

void _os_log_impl(void *dso, void *log, int type, const char *fmt, ...) {
    va_list ap;

    (void)dso;
    (void)log;
    (void)type;

    if (fmt != NULL) {
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
    }
    fputc('\n', stderr);
}

void _os_log_debug_impl(void *a, void *b, int c, const char *d, ...) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
}

void _os_log_error_impl(void *a, void *b, int c, const char *d, ...) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
}

void _os_log_fault_impl(void *a, void *b, int c, const char *d, ...) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
}
