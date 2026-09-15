#ifndef PANTHERA_ZFS_COMPAT_STDIO_H
#define PANTHERA_ZFS_COMPAT_STDIO_H

typedef struct __panthera_zfs_file FILE;
extern FILE *stderr;

int fprintf(FILE *, const char *, ...);

#endif /* PANTHERA_ZFS_COMPAT_STDIO_H */
