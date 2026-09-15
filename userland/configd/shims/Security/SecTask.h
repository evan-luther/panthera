#ifndef PANTHERA_SECURITY_SECTASK_H
#define PANTHERA_SECURITY_SECTASK_H

#include <CoreFoundation/CoreFoundation.h>

typedef const struct __SecTask *SecTaskRef;

static inline SecTaskRef
SecTaskCreateFromSelf(CFAllocatorRef allocator)
{
	(void)allocator;
	return NULL;
}

static inline CFTypeRef
SecTaskCopyValueForEntitlement(SecTaskRef task, CFStringRef entitlement, CFErrorRef *error)
{
	(void)task;
	(void)entitlement;
	if (error != NULL) {
		*error = NULL;
	}
	return NULL;
}

#endif /* PANTHERA_SECURITY_SECTASK_H */
