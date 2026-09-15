/* Panthera shim: objc-shared-cache.h
 * Defines the objc_opt structures used by the ObjC runtime to read
 * pre-optimized class/selector/protocol data from the dyld shared cache.
 */
#ifndef _OBJC_SHARED_CACHE_H
#define _OBJC_SHARED_CACHE_H

#include <stdint.h>

namespace objc_opt {

static const uint32_t VERSION = 16;

enum : uint32_t {
    NoMissingWeakSuperclasses = (1 << 0),
};

struct objc_opt_t {
    uint32_t version;
    uint32_t flags;
    int32_t  selopt_offset;
    int32_t  headeropt_ro_offset;
    int32_t  unused_clsopt_offset;
    int32_t  unused_protocolopt_offset;
    int32_t  headeropt_rw_offset;
    int32_t  unused_protocolopt2_offset;
    int32_t  largeSharedCachesClassOffset;
    int32_t  largeSharedCachesProtocolOffset;
    uint64_t relativeMethodSelectorBaseAddressOffset;
};

} // namespace objc_opt

#endif /* _OBJC_SHARED_CACHE_H */
