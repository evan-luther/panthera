#ifndef _LIBUTIL_H_
#define _LIBUTIL_H_
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define HN_DECIMAL 0x01
#define HN_NOSPACE 0x02
#define HN_B       0x04
#define HN_DIVISOR_1000 0x08
#define HN_GETSCALE 0x10
#define HN_AUTOSCALE 0x20
#define HN_IEC_PREFIXES 0x40
static inline int humanize_number(char *buf, size_t len, int64_t bytes, const char *suffix, int scale, int flags) {
    (void)scale;
    const char *units = "BKMGTPE";
    double val = (double)bytes;
    int i = 0;
    double divisor = (flags & HN_DIVISOR_1000) ? 1000.0 : 1024.0;
    while (val >= divisor && units[i+1]) { val /= divisor; i++; }
    snprintf(buf, len, "%.0f%c%s", val, units[i], suffix ? suffix : "");
    return (int)strlen(buf);
}
#endif
