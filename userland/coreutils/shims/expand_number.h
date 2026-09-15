/* expand_number() shim for Panthera — parses human-readable sizes (1K, 2M, etc.) */
#ifndef _PANTHERA_EXPAND_NUMBER_H
#define _PANTHERA_EXPAND_NUMBER_H

#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <ctype.h>

static inline int
expand_number(const char *buf, int64_t *num)
{
    char *endptr;
    int64_t val;
    int shift = 0;

    errno = 0;
    val = strtoll(buf, &endptr, 0);
    if (buf == endptr || errno != 0)
        return -1;

    switch (tolower((unsigned char)*endptr)) {
    case 'e': shift = 60; break;
    case 'p': shift = 50; break;
    case 't': shift = 40; break;
    case 'g': shift = 30; break;
    case 'm': shift = 20; break;
    case 'k': shift = 10; break;
    case 'b': case '\0': shift = 0; break;
    default: errno = EINVAL; return -1;
    }

    if (shift && *endptr) endptr++;
    /* allow trailing 'b' or 'B' */
    if (*endptr == 'b' || *endptr == 'B') endptr++;
    if (*endptr != '\0') { errno = EINVAL; return -1; }

    *num = val << shift;
    return 0;
}

#endif
