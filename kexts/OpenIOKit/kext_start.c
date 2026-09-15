/*
 * kext_start.c — Default kext module start/stop for Panthera OpenIOKit kexts.
 *
 * Apple's kext build toolchain generates these automatically. Since we build
 * outside Xcode, we provide them explicitly. The __realmain / __antimain
 * pointers are what _start/_stop jump to.
 */
#include <mach/kern_return.h>

typedef struct kmod_info kmod_info_t;

kern_return_t _kmod_start(kmod_info_t *ki, void *data);
kern_return_t _kmod_stop(kmod_info_t *ki, void *data);

kern_return_t
_kmod_start(__attribute__((unused)) kmod_info_t *ki,
            __attribute__((unused)) void *data)
{
	return KERN_SUCCESS;
}

kern_return_t
_kmod_stop(__attribute__((unused)) kmod_info_t *ki,
           __attribute__((unused)) void *data)
{
	return KERN_SUCCESS;
}

/* The linker resolves __realmain and __antimain to point here */
__attribute__((visibility("default")))
kern_return_t (*__realmain)(kmod_info_t *, void *) = _kmod_start;

__attribute__((visibility("default")))
kern_return_t (*__antimain)(kmod_info_t *, void *) = _kmod_stop;
