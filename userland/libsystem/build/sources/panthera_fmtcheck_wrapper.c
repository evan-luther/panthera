#ifndef __FBSDID
#define __FBSDID(x)
#endif

/*
 * The lightweight libpanthera_extra build path does not provide FreeBSD's
 * __weak_reference helper. Pull in the Apple/FreeBSD fmtcheck implementation
 * and expose the public alias explicitly.
 */
#ifndef __weak_reference
#define __weak_reference(sym, alias)
#endif

#include "../../../../src/Libc-1583.40.7/gen/FreeBSD/fmtcheck.c"

const char *
fmtcheck(const char *f1, const char *f2)
{
    return __fmtcheck(f1, f2);
}
