#ifndef PANTHERA_PTHREAD_MACHDEP_H
#define PANTHERA_PTHREAD_MACHDEP_H

#include <os/tsd.h>

static __inline__ int
_pthread_has_direct_tsd(void)
{
	return 1;
}

static __inline__ void *
_pthread_getspecific_direct(unsigned long slot)
{
	return _os_tsd_get_direct(slot);
}

static __inline__ int
_pthread_setspecific_direct(unsigned long slot, void *val)
{
	return _os_tsd_set_direct(slot, val);
}

#endif
