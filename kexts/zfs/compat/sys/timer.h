/*
 * Panthera/OpenZFS SPL timer compatibility.
 *
 * XNU's real delay() takes microseconds. OpenZFS SPL's Solaris-facing delay()
 * takes ticks and is implemented by osx_delay(clock_t) in spl-osx.c. Include
 * XNU clock declarations before redirecting the SPL-facing name so the kernel
 * ABI remains untouched while SPL call sites get Solaris tick semantics.
 */
#ifndef PANTHERA_ZFS_COMPAT_SYS_TIMER_H
#define PANTHERA_ZFS_COMPAT_SYS_TIMER_H

#include <kern/clock.h>

#define delay osx_delay
#include_next <sys/timer.h>

#endif /* PANTHERA_ZFS_COMPAT_SYS_TIMER_H */
