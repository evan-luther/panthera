/* Block_private.h — Panthera shim for CoreFoundation build */
#ifndef _BLOCK_PRIVATE_SHIM_H
#define _BLOCK_PRIVATE_SHIM_H

#include <Block.h>

/* Private Block runtime APIs used by CF for QoS propagation */
typedef void (*_Block_invoke_funcptr)(void *, ...);

/* Block descriptor layout (private) */
struct Block_descriptor_1 {
    unsigned long int reserved;
    unsigned long int size;
};

#endif /* _BLOCK_PRIVATE_SHIM_H */
