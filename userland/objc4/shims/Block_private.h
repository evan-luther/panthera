/* Panthera shim: Block_private.h
 * Provides Block private internals needed by objc4.
 */
#ifndef _BLOCK_PRIVATE_H
#define _BLOCK_PRIVATE_H

#include <Block.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Block descriptor flags */
enum {
    BLOCK_DEALLOCATING =      (0x0001),
    BLOCK_REFCOUNT_MASK =     (0xfffe),
    BLOCK_INLINE_LAYOUT_STRING = (1 << 21),
    BLOCK_SMALL_DESCRIPTOR =  (1 << 22),
    BLOCK_IS_NOESCAPE =       (1 << 23),
    BLOCK_NEEDS_FREE =        (1 << 24),
    BLOCK_HAS_COPY_DISPOSE =  (1 << 25),
    BLOCK_HAS_CTOR =          (1 << 26),
    BLOCK_IS_GC =             (1 << 27),
    BLOCK_IS_GLOBAL =         (1 << 28),
    BLOCK_USE_STRET =         (1 << 29),
    BLOCK_HAS_SIGNATURE  =    (1 << 30),
    BLOCK_HAS_EXTENDED_LAYOUT = (1u << 31),
};

/* Block layout */
struct Block_layout {
    void *isa;
    volatile int32_t flags;
    int32_t reserved;
    void (*invoke)(void *, ...);
    struct Block_descriptor_1 *descriptor;
};

struct Block_descriptor_1 {
    uintptr_t reserved;
    uintptr_t size;
};

struct Block_descriptor_2 {
    void (*copy)(void *dst, const void *src);
    void (*dispose)(const void *);
};

struct Block_descriptor_3 {
    const char *signature;
    const char *layout;
};

static inline bool _Block_has_signature(const void *aBlock) {
    const struct Block_layout *layout = (const struct Block_layout *)aBlock;
    return (layout->flags & BLOCK_HAS_SIGNATURE) != 0;
}

static inline bool _Block_use_stret(const void *aBlock) {
    const struct Block_layout *layout = (const struct Block_layout *)aBlock;
    return (layout->flags & BLOCK_USE_STRET) != 0;
}

#ifdef __cplusplus
}
#endif

#endif /* _BLOCK_PRIVATE_H */
