/* Panthera shim: Cambria/Traps.h
 * Rosetta trap functions — not used on native x86_64.
 */
#ifndef _CAMBRIA_TRAPS_H
#define _CAMBRIA_TRAPS_H

#include <mach/kern_return.h>
#include <mach/mach_types.h>
#include <stdint.h>

static inline kern_return_t
objc_thread_get_rip(thread_act_t thread, uint64_t *rip)
{
    (void)thread;
    (void)rip;
    return KERN_FAILURE;
}

#endif /* _CAMBRIA_TRAPS_H */
