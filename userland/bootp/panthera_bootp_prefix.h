#ifndef PANTHERA_BOOTP_PREFIX_H
#define PANTHERA_BOOTP_PREFIX_H

#include "../configd/shims/panthera_sc_prefix.h"

/*
 * IPConfiguration is userland code.  The shared configd prefix defines this
 * for older SystemConfiguration header probes, but leaving it visible here
 * pulls kernel-private networking declarations into bootp sources.
 */
#ifdef KERNEL_PRIVATE
#undef KERNEL_PRIVATE
#endif

#include <errno.h>
#include <sys/errno.h>
#include <CoreFoundation/CoreFoundation.h>

#ifndef XPC_EXPORT
#define XPC_EXPORT extern
#endif
#ifndef XPC_WARN_RESULT
#define XPC_WARN_RESULT
#endif
#ifndef XPC_NONNULL1
#define XPC_NONNULL1
#endif
#ifndef XPC_NONNULL2
#define XPC_NONNULL2
#endif
#ifndef XPC_NONNULL3
#define XPC_NONNULL3
#endif
#ifndef XPC_NONNULL4
#define XPC_NONNULL4
#endif
#ifndef XPC_NONNULL5
#define XPC_NONNULL5
#endif

#ifndef errno
extern int *__error(void);
#define errno (*__error())
#endif

/*
 * Panthera's current sysroot exposes XNU fortified string macros through
 * string.h.  Several Apple bootp files include strings.h after CoreFoundation
 * has already pulled in string.h; keep the legacy bcopy declaration parseable.
 */
#ifdef bcopy
#undef bcopy
#endif

extern const CFStringRef kSCPropNetPvDIdentifier;
extern const CFStringRef kSCPropNetPvDHTTPSupported;
extern const CFStringRef kSCPropNetPvDSequenceNumber;
extern const CFStringRef kSCPropNetPvDDelay;
extern const CFStringRef kSCPropNetPvDAdditionalInformation;
extern const CFStringRef kSCPropNetPvDLegacy;

#ifndef ND_OPT_PVD
#define ND_OPT_PVD 21
struct nd_opt_pvd {
	uint8_t nd_opt_pvd_type;
	uint8_t nd_opt_pvd_len;
	uint8_t nd_opt_flags_delay[2];
	uint16_t nd_opt_pvd_seq;
	uint8_t nd_opt_pvd_id[];
} __attribute__((packed));
#define ND_OPT_PVD_MIN_LENGTH __builtin_offsetof(struct nd_opt_pvd, nd_opt_pvd_id)
#define ND_OPT_PVD_FLAGS_HTTP 0x80
#define ND_OPT_PVD_FLAGS_LEGACY 0x40
#define ND_OPT_PVD_FLAGS_RA 0x20
#define ND_OPT_PVD_DELAY_MASK 0x0f
#endif

#include <fcntl.h>
#include <string.h>
#include <stdarg.h>

static inline int panthera_bootp_safe_open(const char *path, int flags, ...) {
	if (path != NULL && strcmp(path, "/dev/console") == 0) {
		return -1;
	}
	mode_t mode = 0;
	if (flags & O_CREAT) {
		va_list ap;
		va_start(ap, flags);
		mode = (mode_t)va_arg(ap, int);
		va_end(ap);
	}
	return (open)(path, flags, mode);
}
#ifndef open
#define open(p, ...) panthera_bootp_safe_open(p, __VA_ARGS__)
#endif

#endif /* PANTHERA_BOOTP_PREFIX_H */
