/* mach-o/dyld_priv.h — Panthera shim for CoreFoundation build */
#ifndef _MACH_O_DYLD_PRIV_H
#define _MACH_O_DYLD_PRIV_H

#include <mach-o/dyld.h>

#ifdef __cplusplus
extern "C" {
#endif

const char *dyld_image_path_containing_address(const void *addr);

#ifdef __cplusplus
}
#endif

#endif /* _MACH_O_DYLD_PRIV_H */
