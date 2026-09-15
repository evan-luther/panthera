#ifndef PANTHERA_ZFS_USERLAND_COMPAT_H
#define PANTHERA_ZFS_USERLAND_COMPAT_H

#include <stdint.h>

typedef unsigned int uint_t;
typedef unsigned char uchar_t;

#ifndef zfs_fallthrough
#define zfs_fallthrough __attribute__((fallthrough))
#endif

#ifndef TEXT_DOMAIN
#define TEXT_DOMAIN "zfs"
#endif

#ifndef ZFS_META_ALIAS
#define ZFS_META_ALIAS "zfs-2.3.1-panthera"
#endif

#ifndef ZFSEXECDIR
#define ZFSEXECDIR "/usr/libexec/zfs"
#endif

#ifndef KERNEL_MODPREFIX
#define KERNEL_MODPREFIX "/System/Library/Extensions"
#endif

#endif
