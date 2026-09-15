/* Panthera shim: Cambria/Cambria.h
 * Rosetta translation layer — always returns "not translated" on native x86_64.
 */
#ifndef _CAMBRIA_CAMBRIA_H
#define _CAMBRIA_CAMBRIA_H

#include <stdbool.h>

static inline bool oah_is_current_process_translated(void) {
    return false;
}

#endif /* _CAMBRIA_CAMBRIA_H */
