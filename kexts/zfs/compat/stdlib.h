#ifndef PANTHERA_ZFS_COMPAT_STDLIB_H
#define PANTHERA_ZFS_COMPAT_STDLIB_H

#include <stddef.h>

void *malloc(size_t);
void *calloc(size_t, size_t);
void free(void *);
void qsort(void *, size_t, size_t, int (*)(const void *, const void *));

#endif /* PANTHERA_ZFS_COMPAT_STDLIB_H */
