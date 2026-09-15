#ifndef _PANTHERA_LIBSYSTEM_C_TIME_SHIM_H_
#define _PANTHERA_LIBSYSTEM_C_TIME_SHIM_H_

/*
 * Apple headers expose the UNIX03 timezone variable when __DARWIN_UNIX03=1,
 * but the legacy FreeBSD timezone() function source still builds in this tree.
 * Hide the variable declaration while including the real header, then restore
 * the function prototype for the rebuild.
 */
#define timezone __panthera_timezone_hidden
#include_next <time.h>
#undef timezone

#endif
