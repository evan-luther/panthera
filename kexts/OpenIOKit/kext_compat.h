/*
 * kext_compat.h — Compatibility shims for building old kexts against macOS 26 SDK
 *
 * Force-included via -include before all other headers to ensure
 * these definitions take effect before <Availability.h> is included.
 */
#ifndef OPENIOKIT_KEXT_COMPAT_H
#define OPENIOKIT_KEXT_COMPAT_H

/* Suppress availability attributes — they produce parse errors in enum
 * contexts when the deployment target or SDK version doesn't match the
 * old kext source expectations.
 *
 * The SDK's os/availability.h checks #ifndef before defining these.
 * By defining them here (force-included first), we prevent the complex
 * __attribute__((availability)) expansion that breaks in enum contexts. */
#define API_DEPRECATED_WITH_REPLACEMENT(...)
#define API_DEPRECATED_WITH_REPLACEMENT_BEGIN(...)
#define API_DEPRECATED_WITH_REPLACEMENT_END
#define API_DEPRECATED(...)
#define API_DEPRECATED_BEGIN(...)
#define API_DEPRECATED_END
#define API_UNAVAILABLE(...)
#define API_UNAVAILABLE_BEGIN(...)
#define API_UNAVAILABLE_END
#define API_AVAILABLE(...)
#define API_AVAILABLE_BEGIN(...)
#define API_AVAILABLE_END

/* BoundsSafety / ptrcheck (macOS 26 SDK) */
#undef __ptrcheck_unavailable_r
#define __ptrcheck_unavailable_r(msg)

#undef __unsafe_forge_null_terminated
#define __unsafe_forge_null_terminated(T, V) ((T)(V))

#undef __unsafe_null_terminated_from_indexable
#define __unsafe_null_terminated_from_indexable(P, E) (P)

#undef __unsafe_terminated_by_from_indexable
#define __unsafe_terminated_by_from_indexable(V, P, E) (P)

#undef __unsafe_forge_terminated_by
#define __unsafe_forge_terminated_by(T, P, V) ((T)(P))

#undef __unsafe_forge_bidi_indexable
#define __unsafe_forge_bidi_indexable(T, P, S) ((T)(P))

#undef __unsafe_forge_single
#define __unsafe_forge_single(T, P) ((T)(P))

#undef __null_terminated
#define __null_terminated

#undef __terminated_by
#define __terminated_by(x)

#undef __ended_by
#define __ended_by(x)

#undef __counted_by
#define __counted_by(x)

#undef __sized_by
#define __sized_by(x)

#undef __single
#define __single

#undef __unsafe_indexable
#define __unsafe_indexable

#undef __header_indexable
#define __header_indexable

#undef __bidi_indexable
#define __bidi_indexable

/*
 * Some older OSS kexts call the early-registration helper directly.
 * Panthera exports the runtime-safe fallback as sysctl_register_oid_early_kext.
 */
#define sysctl_register_oid_early sysctl_register_oid_early_kext

#ifndef DBG_IOG_LOG_SYNCH
#define DBG_IOG_LOG_SYNCH 34
#endif

#ifndef DBG_IOG_BUILTIN_PANEL_POWER
#define DBG_IOG_BUILTIN_PANEL_POWER 51
#endif

#ifndef DBG_IOG_TIMELOCK
#define DBG_IOG_TIMELOCK 52
#endif

#ifndef DBG_IOG_ASYNC_WORK
#define DBG_IOG_ASYNC_WORK 53
#endif

#ifndef DBG_IOG_CAPTURED_RETRAIN
#define DBG_IOG_CAPTURED_RETRAIN 54
#endif

#ifndef DBG_IOG_CONNECT_WORK_ASYNC
#define DBG_IOG_CONNECT_WORK_ASYNC 55
#endif

#ifndef DBG_IOG_MSG_CONNECT_CHANGE
#define DBG_IOG_MSG_CONNECT_CHANGE 56
#endif

#ifndef DBG_IOG_CONNECT_CHANGE_INTERRUPT_V2
#define DBG_IOG_CONNECT_CHANGE_INTERRUPT_V2 57
#endif

#ifndef DBG_IOG_CURSORLOCK
#define DBG_IOG_CURSORLOCK 58
#endif

#ifndef DBG_IOG_FB_EXT_CLOSE
#define DBG_IOG_FB_EXT_CLOSE 59
#endif

#ifndef DBG_IOG_MUX_ACTIVITY_CHANGE
#define DBG_IOG_MUX_ACTIVITY_CHANGE 60
#endif

#ifndef DBG_IOG_SET_ATTR_FOR_CONN_EXT
#define DBG_IOG_SET_ATTR_FOR_CONN_EXT 61
#endif

#ifndef DBG_IOG_EXT_END_CONNECT_CHANGE
#define DBG_IOG_EXT_END_CONNECT_CHANGE 62
#endif

#ifndef DBG_IOG_EXT_PROCESS_CONNECT_CHANGE
#define DBG_IOG_EXT_PROCESS_CONNECT_CHANGE 63
#endif

#ifndef DBG_IOG_SYSTEM_WORK
#define DBG_IOG_SYSTEM_WORK 64
#endif

#ifndef DBG_IOG_SOURCE_DO_SETUP
#define DBG_IOG_SOURCE_DO_SETUP 35
#endif

#ifndef DBG_IOG_SOURCE_DO_SET_DISPLAY_MODE
#define DBG_IOG_SOURCE_DO_SET_DISPLAY_MODE 36
#endif

#ifndef DBG_IOG_SOURCE_EXT_GET_CURRENT_DISPLAY_MODE
#define DBG_IOG_SOURCE_EXT_GET_CURRENT_DISPLAY_MODE 37
#endif

#ifndef DBG_IOG_SOURCE_INIT_FB
#define DBG_IOG_SOURCE_INIT_FB 38
#endif

#ifndef DBG_IOG_SOURCE_OVERSCAN
#define DBG_IOG_SOURCE_OVERSCAN 39
#endif

#ifndef DBG_IOG_SOURCE_SERVER_ACK_TIMEOUT
#define DBG_IOG_SOURCE_SERVER_ACK_TIMEOUT 40
#endif

#ifndef DBG_IOG_SOURCE_SET_ATTR_FOR_CONN_EXT
#define DBG_IOG_SOURCE_SET_ATTR_FOR_CONN_EXT 41
#endif

#ifndef DBG_IOG_SOURCE_SET_TRANSFORM
#define DBG_IOG_SOURCE_SET_TRANSFORM 42
#endif

#ifndef DBG_IOG_SOURCE_SYSWORK_RESETCLAMSHELL_V2
#define DBG_IOG_SOURCE_SYSWORK_RESETCLAMSHELL_V2 43
#endif

#ifndef DBG_IOG_SOURCE_VENDOR
#define DBG_IOG_SOURCE_VENDOR 44
#endif

#ifndef IODW_BUILTIN_PANEL_POWER_VERSION
#define IODW_BUILTIN_PANEL_POWER_VERSION 1U
typedef struct {
    unsigned int version;
    unsigned long long mct;
    unsigned char state;
    unsigned char reserved[7];
} IODWBuiltinPanelPower_t;
#endif

#ifndef XXH64
static __inline__ unsigned long long
panthera_kext_xxh64_compat(const unsigned char *data, unsigned long len)
{
    unsigned long long hash = 1469598103934665603ULL;
    if (!data) {
        return hash;
    }
    for (unsigned long i = 0; i < len; i++) {
        hash ^= (unsigned long long)data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}
#define XXH64(data, len) panthera_kext_xxh64_compat((const unsigned char *)(data), (unsigned long)(len))
#endif

#ifndef ABLTRACE_RAW
#define ABLTRACE_RAW(...) do {} while (0)
#define ABL_GT_SET_DISPLAY(...) do {} while (0)
#define ABL_GT_SET_BRIGHTNESS_PROBE(...) do {} while (0)
#define ABL_GT_SET_BRIGHTNESS(...) do {} while (0)
#define ABL_GT_COMMITTED(...) do {} while (0)
#define ABL_GT_SET_DISPLAY_POWER(...) do {} while (0)
#define ABL_GT_DO_UPDATE(...) do {} while (0)
#define ABL_SOURCE_IOD_ADDPARAMETERHANDLER 1
#define ABL_SOURCE_IOD_SETPARAMETER 2
#define ABL_SOURCE_IOD_DOINTEGERSET 3
#define ABL_SOURCE_IOD_DOUPDATE 4
#define ABL_SET_BRIGHTNESS 5
#define ABL_SET_BRIGHTNESS_PROBE 6
#define ABL_SET_DISPLAY_POWER 7
#define ABL_COMMIT 8
#endif

#endif /* OPENIOKIT_KEXT_COMPAT_H */
