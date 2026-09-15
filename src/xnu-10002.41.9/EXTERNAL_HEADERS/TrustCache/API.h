/*
 * TrustCache/API.h — Stub for PureDarwin/Panthera build
 *
 * TrustCache is a proprietary Apple framework for code signing verification.
 * These are minimal type stubs to satisfy the XNU build. On PureDarwin,
 * trust cache functionality is effectively a no-op (no AMFI enforcement).
 */

#ifndef _TRUSTCACHE_API_H_
#define _TRUSTCACHE_API_H_

#include <stdint.h>

/* Constants */
#define kUUIDSize 16
#define kTCEntryHashSize 20

/* Return codes */
typedef enum {
	kTCReturnSuccess = 0,
	kTCReturnDuplicate = 1,
	kTCReturnError = 2,
	kTCReturnNotFound = 3,
	kTCReturnInvalidModule = 4,
	kTCReturnInvalidArguments = 5,
} TCReturn_error_t;

typedef struct {
	TCReturn_error_t error;
	uint32_t component;
	uint32_t uniqueError;
} TCReturn_t;

/* Trust cache type identifiers */
typedef uint32_t TCType_t;
typedef uint32_t TCQueryType_t;

/* Query type constants */
enum {
	kTCQueryTypeTotal = 0,
};

/* Trust cache type constants */
enum {
	kTCTypeInvalid        = 0,
	kTCTypeStatic         = 1,
	kTCTypeEngineering    = 2,
	kTCTypeLegacy         = 3,
	kTCTypeDTRS           = 4,
	kTCTypeLTRS           = 5,
	kTCTypeCryptex1BootOS = 6,
	kTCTypeCryptex1BootApp = 7,
	kTCTypeTotal          = 8,
};

/* Trust cache type config — array indexed by TCType constants */
typedef struct {
	const char *entitlementValue;
	uint32_t _opaque[3];
} TCTypeConfig_t;

#ifdef __cplusplus
#define TC_NULLPTR nullptr
#else
#define TC_NULLPTR ((const char *)0)
#endif

/* Global config array — stub with all-NULL entries */
static const TCTypeConfig_t TCTypeConfig[kTCTypeTotal + 1] = {
	{ TC_NULLPTR, { 0, 0, 0 } }
};

/* Opaque runtime types */
typedef struct _TrustCache {
	uint64_t _opaque[8];
} TrustCache_t;

typedef struct _TrustCacheRuntime {
	uint64_t _opaque[14];
	bool allowEngineeringTC;
	bool _pad;
} TrustCacheRuntime_t;

typedef struct _TrustCacheMutableRuntime {
	uint64_t _opaque[16];
} TrustCacheMutableRuntime_t;

typedef struct _TrustCacheQueryToken {
	uint64_t _opaque[4];
} TrustCacheQueryToken_t;

typedef uint64_t TCCapabilities_t;

#define kTCCapabilityNone ((TCCapabilities_t)0)

static inline void
trustCacheInitializeRuntime(
	TrustCacheRuntime_t *runtime,
	TrustCacheMutableRuntime_t *mutable_runtime,
	bool allow_second_static_cache,
	bool allow_engineering_caches,
	bool allow_legacy_caches,
	uint32_t img4_runtime)
{
	(void)mutable_runtime;
	(void)allow_second_static_cache;
	(void)allow_legacy_caches;
	(void)img4_runtime;

	if (runtime) {
		runtime->allowEngineeringTC = allow_engineering_caches;
		runtime->_pad = false;
	}
}

#undef TC_NULLPTR

#endif /* _TRUSTCACHE_API_H_ */
