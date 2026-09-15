/* Panthera shim: os/crashlog_private.h */
#ifndef _OS_CRASHLOG_PRIVATE_H
#define _OS_CRASHLOG_PRIVATE_H

#include <stdio.h>
#include <stdlib.h>

#define OS_BUG_INTERNAL(c, lib, msg) do { \
    fprintf(stderr, "%s BUG: %s (code=%d)\n", lib, msg, (int)(c)); \
    abort(); \
} while(0)

#define OS_BUG_CLIENT(c, lib, msg) do { \
    fprintf(stderr, "%s CLIENT BUG: %s (code=%d)\n", lib, msg, (int)(c)); \
} while(0)

#endif /* _OS_CRASHLOG_PRIVATE_H */
