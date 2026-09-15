/*
 * Minimal userland IOMapTypes surface for Panthera Phase 1.
 */

#ifndef __IOKIT_IOMAPTYPES_H
#define __IOKIT_IOMAPTYPES_H

enum {
    kIODefaultMemoryType = 0
};

enum {
    kIODefaultCache            = 0,
    kIOInhibitCache            = 1,
    kIOWriteThruCache          = 2,
    kIOCopybackCache           = 3,
    kIOWriteCombineCache       = 4,
    kIOCopybackInnerCache      = 5,
    kIOPostedWrite             = 6,
    kIORealTimeCache           = 7,
    kIOPostedReordered         = 8,
    kIOPostedCombinedReordered = 9,
};

enum {
    kIOMapAnywhere                = 0x00000001,
    kIOMapCacheMask               = 0x00000f00,
    kIOMapCacheShift              = 8,
    kIOMapDefaultCache            = kIODefaultCache << kIOMapCacheShift,
    kIOMapInhibitCache            = kIOInhibitCache << kIOMapCacheShift,
    kIOMapWriteThruCache          = kIOWriteThruCache << kIOMapCacheShift,
    kIOMapCopybackCache           = kIOCopybackCache << kIOMapCacheShift,
    kIOMapWriteCombineCache       = kIOWriteCombineCache << kIOMapCacheShift,
    kIOMapCopybackInnerCache      = kIOCopybackInnerCache << kIOMapCacheShift,
    kIOMapPostedWrite             = kIOPostedWrite << kIOMapCacheShift,
    kIOMapRealTimeCache           = kIORealTimeCache << kIOMapCacheShift,
    kIOMapPostedReordered         = kIOPostedReordered << kIOMapCacheShift,
    kIOMapPostedCombinedReordered = kIOPostedCombinedReordered << kIOMapCacheShift,
    kIOMapUserOptionsMask         = 0x00000fff,
    kIOMapReadOnly                = 0x00001000,
};

#endif
