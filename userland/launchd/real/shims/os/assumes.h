#ifndef _OS_ASSUMES_H
#define _OS_ASSUMES_H

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define os_assert(e) assert(e)
#define os_assumes(e) (e)
#define os_assert_zero(e) assert((e) == 0)
#define os_assumes_zero(e) (e)

/* posix_assumes_zero / posix_assert_zero — Apple convention:
 * "assert zero errors", i.e. assert the POSIX call did NOT return -1.
 * NOT "assert the value is zero". */
#define posix_assumes_zero(e) (e)
#define posix_assert_zero(e) ({ __typeof__(e) _paz_v = (e); if (_paz_v == -1) abort(); _paz_v; })

#define os_assert_mach(kr, e) assert((e) == 0)
#define os_crash(msg) do { fprintf(stderr, "CRASH: %s\n", msg); abort(); } while(0)

#define os_assumes_ctx(f, ctx, e) (e)
#define os_assumes_zero_ctx(f, ctx, e) (e)
#define posix_assumes_zero_ctx(f, ctx, e) (e)

#define os_redirect_assumes(f)

#define osx_assumes(e) (e)
#define osx_assumes_zero(e) (e)
#define osx_assert(e) assert(e)
#define osx_assert_zero(e) assert((e) == 0)

typedef unsigned int os_redirect_t;

#endif /* _OS_ASSUMES_H */
