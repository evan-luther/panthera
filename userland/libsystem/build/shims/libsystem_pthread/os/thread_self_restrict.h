/*
 * Minimal thread self-restrict shim for Panthera pthread bring-up.
 * Panthera does not implement RWX thread self-restriction yet.
 */

#ifndef _PANTHERA_OS_THREAD_SELF_RESTRICT_H
#define _PANTHERA_OS_THREAD_SELF_RESTRICT_H

#include <stdbool.h>

static inline bool
os_thread_self_restrict_rwx_is_supported(void)
{
	return false;
}

static inline void
os_thread_self_restrict_rwx_to_rx(void)
{
}

static inline void
os_thread_self_restrict_rwx_to_rw(void)
{
}

#endif /* _PANTHERA_OS_THREAD_SELF_RESTRICT_H */
