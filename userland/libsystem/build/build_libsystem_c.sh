#!/bin/bash
# Build libsystem_c.dylib for Panthera Darwin
# The C library - biggest sub-library in libSystem
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
PANTHERA="${PANTHERA:-$PANTHERA_ROOT}"
SRCDIR="${SRCDIR:-$PANTHERA/src/Libc-1583.40.7}"
BUILDDIR="${BUILDDIR:-$PANTHERA/userland/libsystem/build}"
OBJDIR="${OBJDIR:-$BUILDDIR/obj/libsystem_c}"
OUTDIR="${OUTDIR:-$BUILDDIR/sysroot/usr/lib/system}"
SYSROOT="${SYSROOT:-$BUILDDIR/sysroot}"
SHIMSRC="${SHIMSRC:-$BUILDDIR/shims/libsystem_c}"
BRIDGE_SRC="$SHIMSRC/panthera_libc_bridges.c"
BRIDGE_ASM="$SHIMSRC/panthera_libc_aliases.s"
MATH_SRC="$SHIMSRC/math"
BUILD_STUB_BINDER="${BUILD_STUB_BINDER:-$BUILDDIR/build_stub_binder.sh}"
DYLD_STUB="${BUILDDIR}/obj/dyld_stub_binder.o"
SDK_PATH="$(xcrun -sdk macosx -show-sdk-path)"

TARGET="x86_64-apple-darwin23.0"

# Prerequisite checks
required_inputs=(
    "$SRCDIR"
    "$BRIDGE_SRC"
    "$BUILDDIR/compat_include/Availability.h"
    "$BRIDGE_ASM"
    "$MATH_SRC/pow.c"
    "$MATH_SRC/pow_log_data.c"
    "$MATH_SRC/exp_data.c"
    "$MATH_SRC/math_err.c"
    "$MATH_SRC/math_config.h"
    "$PANTHERA/src/xnu-10002.41.9"
    "$PANTHERA/src/libplatform-306.0.1"
    "$PANTHERA/src/libpthread-519"
)

for path in "${required_inputs[@]}"; do
    if [ ! -e "$path" ]; then
        echo "missing required input: $path" >&2
        exit 1
    fi
done

if [ -f "$BUILD_STUB_BINDER" ]; then
    bash "$BUILD_STUB_BINDER"
fi

if [ ! -f "$DYLD_STUB" ]; then
    echo "missing required input: $DYLD_STUB" >&2
    exit 1
fi

mkdir -p "$OBJDIR" "$OUTDIR"
# ============================================================
# Step 0: Create shim headers that Libc needs but we don't have
# ============================================================
SHIMDIR="$OBJDIR/shims"
mkdir -p "$SHIMDIR" "$SHIMDIR/os" "$SHIMDIR/mach-o" 2>/dev/null
mkdir -p "$SHIMDIR/mach" "$SHIMDIR/libproc" "$SHIMDIR/malloc"

# Older runs accidentally created this shim path as a directory.
if [ -d "$SHIMDIR/CrashReporterClient.h" ]; then
    rmdir "$SHIMDIR/CrashReporterClient.h" 2>/dev/null || true
fi
# Stale sysroot xpc directory shadows SDK <xpc/base.h>
if [ -d "$SYSROOT/usr/include/xpc" ]; then
    rm -rf "$SYSROOT/usr/include/xpc"
fi

# Avoid stale-object contamination from earlier experiments.
find "$OBJDIR" -name '*.o' -delete 2>/dev/null || true

# TargetConditionals.h shim
cat > "$SHIMDIR/TargetConditionals.h" << 'SHIM'
#ifndef __TARGETCONDITIONALS_H__
#define __TARGETCONDITIONALS_H__
#define TARGET_OS_MAC 1
#define TARGET_OS_OSX 1
#define TARGET_OS_IPHONE 0
#define TARGET_OS_IOS 0
#define TARGET_OS_WATCH 0
#define TARGET_OS_TV 0
#define TARGET_OS_SIMULATOR 0
#define TARGET_OS_EMBEDDED 0
#define TARGET_OS_DRIVERKIT 0
#define TARGET_OS_BRIDGE 0
#define TARGET_CPU_X86_64 1
#define TARGET_CPU_ARM 0
#define TARGET_CPU_ARM64 0
#define TARGET_CPU_X86 0
#define TARGET_RT_64_BIT 1
#define TARGET_RT_LITTLE_ENDIAN 1
#define TARGET_RT_BIG_ENDIAN 0
#endif
SHIM

# os/assumes.h shim
cat > "$SHIMDIR/os/assumes.h" << 'SHIM'
#ifndef _OS_ASSUMES_H_
#define _OS_ASSUMES_H_
#include <assert.h>
#include <stdlib.h>
#define os_assumes(_x) ({ __typeof__(_x) _r = (_x); if (!_r) { } _r; })
#define os_assert(_x) assert(_x)
#define os_assumes_zero(_x) ({ __typeof__(_x) _r = (_x); if (_r) { } _r; })
#define os_assert_zero(_x) ({ __typeof__(_x) _r = (_x); assert(_r == 0); _r; })
#define os_crash(msg) abort()
#define os_log_error(log, fmt, ...) ((void)0)
#define OS_CRASH_ENABLE_EXPERIMENTAL_LIBTRACE 0
#endif
SHIM

# mach-o/dyld_priv.h shim
cat > "$SHIMDIR/mach-o/dyld_priv.h" << 'SHIM'
#ifndef _MACH_O_DYLD_PRIV_H_
#define _MACH_O_DYLD_PRIV_H_
#include <stdint.h>
#include <stdbool.h>
typedef uint32_t dyld_platform_t;
static inline bool dyld_program_sdk_at_least(dyld_platform_t __unused p, uint32_t __unused v) { return true; }
#define dyld_fall_2018_os_versions 0x000C0200
#define dyld_fall_2020_os_versions 0x000E0000
#define dyld_platform_version_macOS_10_14 0x000E0000
#define dyld_platform_version_macOS_10_16 0x000E0000
#define dyld_platform_version_macOS_11_0 0x000B0000
#define dyld_platform_version_macOS_12_0 0x000C0000
#define dyld_platform_version_macOS_13_0 0x000D0000
#define PLATFORM_MACOS 1
#endif
SHIM

# CrashReporterClient.h shim
cat > "$SHIMDIR/CrashReporterClient.h" << 'SHIM'
#ifndef _CRASHREPORTERCLIENT_H_
#define _CRASHREPORTERCLIENT_H_
#define CRSetCrashLogMessage(msg) ((void)0)
#define CRSetCrashLogMessage2(msg) ((void)0)
#define _CRASHREPORTERCLIENT_META_INFO
#define CRASH_REPORTER_CLIENT_HIDDEN
#define CRGetCrashLogMessage() ((const char *)0)
struct crashreporter_annotations_t {
    unsigned long long version;
    char *message;
    char *message2;
};
#endif
SHIM

# os/log.h shim (if not present)
cat > "$SHIMDIR/os/log.h" << 'SHIM'
#ifndef _OS_LOG_H_
#define _OS_LOG_H_
#include <stdint.h>
typedef struct os_log_s *os_log_t;
typedef uint8_t os_log_type_t;
enum {
    OS_LOG_TYPE_DEFAULT = 0x00,
    OS_LOG_TYPE_INFO = 0x01,
    OS_LOG_TYPE_DEBUG = 0x02,
    OS_LOG_TYPE_ERROR = 0x10,
    OS_LOG_TYPE_FAULT = 0x11,
};
#define OS_LOG_DEFAULT ((os_log_t)0)
#define os_log(log, fmt, ...) ((void)0)
#define os_log_debug(log, fmt, ...) ((void)0)
#define os_log_error(log, fmt, ...) ((void)0)
#define os_log_info(log, fmt, ...) ((void)0)
#define os_log_fault(log, fmt, ...) ((void)0)
static inline os_log_t os_log_create(const char *s __attribute__((unused)), const char *c __attribute__((unused))) { return (os_log_t)0; }
#endif
SHIM

# os/variant_private.h shim
cat > "$SHIMDIR/os/variant_private.h" << 'SHIM'
#ifndef _OS_VARIANT_PRIVATE_H_
#define _OS_VARIANT_PRIVATE_H_
#include <stdbool.h>
static inline bool os_variant_has_internal_content(const char *s __attribute__((unused))) { return false; }
static inline bool os_variant_has_internal_diagnostics(const char *s __attribute__((unused))) { return false; }
static inline bool os_variant_allows_internal_security_policies(const char *s __attribute__((unused))) { return false; }
#endif
SHIM

# os/lock_private.h shim
cat > "$SHIMDIR/os/lock_private.h" << 'SHIM'
#ifndef _OS_LOCK_PRIVATE_H_
#define _OS_LOCK_PRIVATE_H_
#include <os/lock.h>
typedef os_unfair_lock os_unfair_lock_s;
#endif
SHIM

# malloc/malloc_private.h shim
cat > "$SHIMDIR/malloc/malloc_private.h" << 'SHIM'
#ifndef _MALLOC_PRIVATE_H_
#define _MALLOC_PRIVATE_H_
#endif
SHIM

# libproc.h shim
cat > "$SHIMDIR/libproc.h" << 'SHIM'
#ifndef _LIBPROC_H_
#define _LIBPROC_H_
#endif
SHIM

# os/once_private.h shim
cat > "$SHIMDIR/os/once_private.h" << 'SHIM'
#ifndef _OS_ONCE_PRIVATE_H_
#define _OS_ONCE_PRIVATE_H_
typedef long os_once_t;
extern void _os_once(os_once_t *predicate, void *context, void (*function)(void *));
static inline void os_once(os_once_t *predicate, void *context, void (*function)(void *)) {
    _os_once(predicate, context, function);
}
#define OS_ONCE_INLINE static inline
#endif
SHIM

# ============================================================
# Step 1: Gather all .c files
# ============================================================
echo "=== Gathering source files ==="

SOURCES=()

# Core directories
for dir in gen stdio stdlib string stdtime darwin fbsdcompat secure locale; do
    while IFS= read -r f; do
        SOURCES+=("$f")
    done < <(find "$SRCDIR/$dir" -name "*.c" 2>/dev/null \
        | grep -v '/arm/' | grep -v '/arm64/' | grep -v '/i386/' \
        | grep -v '/test' | grep -v '/tests/' \
        | sort)
done

# Phase 1 additions: pull in the missing Apple-source implementations that are
# still being exported out of libpanthera_extra.
EXTRA_SOURCES=(
    "$SRCDIR/compat-43/sigcompat.c"
    "$SRCDIR/sys/posix_spawn.c"
    "$SRCDIR/sys/getgroups.c"
    "$SRCDIR/util/login_tty.c"
    "$SRCDIR/util/pty.c"
    "$SRCDIR/regex/FreeBSD/regerror.c"
    "$SRCDIR/regex/TRE/lib/regcomp.c"
    "$SRCDIR/regex/TRE/lib/regexec.c"
    "$SRCDIR/regex/TRE/lib/tre-ast.c"
    "$SRCDIR/regex/TRE/lib/tre-compile.c"
    "$SRCDIR/regex/TRE/lib/tre-match-backtrack.c"
    "$SRCDIR/regex/TRE/lib/tre-match-parallel.c"
    "$SRCDIR/regex/TRE/lib/tre-mem.c"
    "$SRCDIR/regex/TRE/lib/tre-parse.c"
    "$SRCDIR/regex/TRE/lib/tre-stack.c"
    "$SRCDIR/uuid/uuidsrc/clear.c"
    "$SRCDIR/uuid/uuidsrc/compare.c"
    "$SRCDIR/uuid/uuidsrc/copy.c"
    "$SRCDIR/uuid/uuidsrc/gen_uuid.c"
    "$SRCDIR/uuid/uuidsrc/isnull.c"
    "$SRCDIR/uuid/uuidsrc/pack.c"
    "$SRCDIR/uuid/uuidsrc/parse.c"
    "$SRCDIR/uuid/uuidsrc/unpack.c"
    "$SRCDIR/uuid/uuidsrc/unparse.c"
    "$SRCDIR/gdtoa/FreeBSD/_hdtoa.c"
    "$SRCDIR/gdtoa/FreeBSD/_ldtoa.c"
    "$SRCDIR/gdtoa/FreeBSD/gdtoa-dmisc.c"
    "$SRCDIR/gdtoa/FreeBSD/gdtoa-dtoa.c"
    "$SRCDIR/gdtoa/FreeBSD/gdtoa-gdtoa.c"
    "$SRCDIR/gdtoa/FreeBSD/gdtoa-gmisc.c"
    "$SRCDIR/gdtoa/FreeBSD/gdtoa-misc.c"
    "$SRCDIR/gdtoa/FreeBSD/gdtoa-smisc.c"
    "$SRCDIR/gdtoa/FreeBSD/gdtoa-sum.c"
    "$SRCDIR/gdtoa/FreeBSD/gdtoa-ulp.c"
    "$SRCDIR/gdtoa/FreeBSD/glue.c"
)

for src in "${EXTRA_SOURCES[@]}"; do
    if [ -f "$src" ]; then
        SOURCES+=("$src")
    fi
done

echo "Found ${#SOURCES[@]} source files"

# ============================================================
# Step 2: Compile each file
# ============================================================
echo "=== Compiling ==="

CFLAGS=(
    -target "$TARGET"
    -mmacosx-version-min=14.0
    -c -fPIC -O2

    # Include paths - Libc internal
    -include "$BUILDDIR/compat_include/Availability.h"
    -include "$SHIMSRC/panthera_libc_compat.h"
    -I"$SHIMSRC"
    -I"$SHIMDIR"
    -I"$SRCDIR"
    -I"$SRCDIR/include"
    -I"$SRCDIR/gen"
    -I"$SRCDIR/gen/FreeBSD"
    -I"$SRCDIR/stdio"
    -I"$SRCDIR/stdio/FreeBSD"
    -I"$SRCDIR/stdlib"
    -I"$SRCDIR/stdlib/FreeBSD"
    -I"$SRCDIR/locale"
    -I"$SRCDIR/locale/FreeBSD"
    -I"$SRCDIR/regex"
    -I"$SRCDIR/regex/FreeBSD"
    -I"$SRCDIR/regex/TRE"
    -I"$SRCDIR/regex/TRE/lib"
    -I"$SRCDIR/fbsdcompat"
    -I"$SRCDIR/nls"
    -I"$SRCDIR/nls/FreeBSD"
    -I"$SRCDIR/darwin"
    -I"$SRCDIR/string"
    -I"$SRCDIR/string/FreeBSD"
    -I"$SRCDIR/stdtime"
    -I"$SRCDIR/stdtime/FreeBSD"
    -I"$SRCDIR/secure"
    -I"$SRCDIR/util"
    -I"$SRCDIR/uuid"
    -I"$SRCDIR/uuid/uuidsrc"
    -I"$SRCDIR/os"
    -I"$SRCDIR/collections"
    -I"$SRCDIR/emulated"
    -I"$SRCDIR/gdtoa"
    -I"$SRCDIR/gdtoa/FreeBSD"
    -I"$SRCDIR/compat-43"
    -I"$SRCDIR/sys"

    # Include paths - sysroot and dependencies
    -I"$SYSROOT/usr/include"
    -I"$PANTHERA/src/xnu-10002.41.9/libsyscall"
    -I"$PANTHERA/src/xnu-10002.41.9/libsyscall/wrappers/spawn"
    -I"$PANTHERA/src/xnu-10002.41.9/osfmk"
    -I"$PANTHERA/src/xnu-10002.41.9/EXTERNAL_HEADERS"
    -I"$PANTHERA/src/xnu-10002.41.9/bsd"
    -I"$PANTHERA/src/xnu-10002.41.9/libkern"
    -I"$PANTHERA/src/libplatform-306.0.1/include"
    -I"$PANTHERA/src/libplatform-306.0.1/private"
    -I"$PANTHERA/src/libpthread-519/include"
    -I"$PANTHERA/src/libpthread-519/private"
    -I"$PANTHERA/src/libclosure-90"
    -I"$PANTHERA/src/Libsystem-1336"

    # SDK headers for anything we still can't find
    -I"$SDK_PATH/usr/include"

    # Defines
    -DPRIVATE
    -D__DARWIN_UNIX03=1
    -D__DARWIN_64_BIT_INO_T=1
    -D__DARWIN_NON_CANCELABLE=0
    -DLIBC_ALIAS_PSELECT
    '-D__SPI_AVAILABLE(...)='
    '-DAPI_AVAILABLE(...)='
    '-DAPI_DEPRECATED(...)='
    '-DAPI_UNAVAILABLE(...)='
    '-DAPI_DEPRECATED_WITH_REPLACEMENT(...)='
    '-D__API_AVAILABLE(...)='
    '-D__API_DEPRECATED(...)='
    '-D__API_UNAVAILABLE(...)='
    '-D__API_DEPRECATED_WITH_REPLACEMENT(...)='
    '-D__OSX_AVAILABLE(...)='
    '-D__OSX_AVAILABLE_STARTING(...)='
    '-D__OSX_AVAILABLE_BUT_DEPRECATED(...)='
    '-D__IOS_AVAILABLE(...)='
    '-D__TVOS_AVAILABLE(...)='
    '-D__WATCHOS_AVAILABLE(...)='
    '-D__BRIDGEOS_AVAILABLE(...)='
    '-D__DRIVERKIT_AVAILABLE(...)='
    '-D__OS_AVAILABILITY(...)='
    '-D__OS_AVAILABILITY_MSG(...)='
    '-D__swift_unavailable(...)='
    -DBUILDING_LIBC=1
    -DPLATFORM_MacOSX=1
    -D_LIBC_NO_FEATURE_VERIFICATION=1

    # Warning suppression
    -Wno-error
    -Wno-implicit-function-declaration
    -Wno-int-conversion
    -Wno-incompatible-pointer-types
    -Wno-pointer-type-mismatch
    -Wno-deprecated-declarations
    -Wno-macro-redefined
    -Wno-nullability-completeness
    -Wno-availability
    -Wno-format
    -Wno-gnu
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-missing-declarations
    -Wno-implicit-int
    -Wno-sign-compare
    -Wno-return-type
    -w
)

SUCCESS=0
FAIL=0
FAILED_FILES=()

for src in "${SOURCES[@]}"; do
    basename=$(basename "$src" .c)
    # Use directory-qualified name to avoid collisions (e.g., gen/time.c vs stdtime/time.c)
    relpath="${src#$SRCDIR/}"
    objname=$(echo "$relpath" | tr '/' '_' | sed 's/\.c$/.o/')
    obj="$OBJDIR/$objname"
    extra_cflags=()

	    case "$relpath" in
	        gen/FreeBSD/arc4random.c)
	            # Panthera does not yet provide corecrypto's ccrng runtime.
	            # Build the getentropy-backed fallback instead of leaving
	            # _ccrng/_ccrng_uniform as dynamic arc4random dependencies.
	            extra_cflags+=(-DVARIANT_STATIC=1)
	            ;;
	        stdlib/FreeBSD/psort_r.c)
	            extra_cflags+=(-DI_AM_PSORT_R)
	            ;;
        stdlib/FreeBSD/psort_b.c)
            extra_cflags+=(-DI_AM_PSORT_B)
            ;;
        # Phase 4: these upstream sources are already part of the broad source
        # sweep, but their key compatibility entry points are marked
        # __private_extern__ in Apple's tree. Export them from libsystem_c so
        # Panthera can retire the overlapping shim ownership.
        locale/FreeBSD/collate.c|locale/FreeBSD/setlocale.c|stdio/FreeBSD/vfscanf.c)
            extra_cflags+=(-D__private_extern__=)
            ;;
        gen/FreeBSD/err.c)
            # Panthera's stdio string-stream path is not yet reliable enough
            # for err(3)'s visibility-escaping formatter. Keep err/warn output
            # on the normal vfprintf path while preserving the public API.
            extra_cflags+=(-DPANTHERA_ERR_USE_VFPRINTF=1)
            ;;
        gen/nanosleep.c)
            extra_cflags+=(-DVARIANT_CANCELABLE=1 -DLIBC_ALIAS_NANOSLEEP)
            ;;
        gen/FreeBSD/sleep.c)
            extra_cflags+=(-DVARIANT_CANCELABLE=1 -DLIBC_ALIAS_SLEEP -D__sleep=sleep -D_nanosleep=nanosleep)
            ;;
        gen/FreeBSD/usleep.c)
            extra_cflags+=(-DVARIANT_CANCELABLE=1 -D_nanosleep=nanosleep)
            ;;
        regex/TRE/lib/*)
            extra_cflags+=(-DHAVE_CONFIG_H=1)
            ;;
    esac

    if clang "${CFLAGS[@]}" "${extra_cflags[@]}" "$src" -o "$obj"; then
        SUCCESS=$((SUCCESS + 1))
    else
        echo "FAILED: ${relpath}" >&2
        exit 1
    fi
done

# Preserve the noncancelable sleep ABI with matching upstream variants.
clang "${CFLAGS[@]}" -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=1 \
    -DBUILDING_VARIANT=1 -DLIBC_ALIAS_NANOSLEEP \
    "$SRCDIR/gen/nanosleep.c" -o "$OBJDIR/gen_nanosleep_nocancel.o"
clang "${CFLAGS[@]}" -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=1 \
    -DLIBC_ALIAS_SLEEP -D__sleep=sleep -D_nanosleep=nanosleep \
    "$SRCDIR/gen/FreeBSD/sleep.c" -o "$OBJDIR/gen_FreeBSD_sleep_nocancel.o"

# Scalar upstream pow: no recursive bridge or dependency on host libm.
for name in pow pow_log_data exp_data math_err; do
    clang -target "$TARGET" -mmacosx-version-min=14.0 -isysroot "$SDK_PATH" \
        -c -fPIC -O2 -frounding-math -ffp-contract=off \
        -DUSE_GLIBC_ABI=0 -DHAVE_FAST_FMA=0 -DHAVE_FAST_ROUND=0 \
        -DWANT_ROUNDING=1 -DWANT_ERRNO=1 \
        "$MATH_SRC/$name.c" -o "$OBJDIR/aor_$name.o"
done

BRIDGE_OBJ="$OBJDIR/panthera_libc_bridges.o"
if clang "${CFLAGS[@]}" "$BRIDGE_SRC" -o "$BRIDGE_OBJ"; then
    echo "  Bridge:   $(basename "$BRIDGE_SRC")"
else
    echo "  Bridge failed: $(basename "$BRIDGE_SRC")"
    exit 1
fi

BRIDGE_ASM_OBJ="$OBJDIR/panthera_libc_aliases.o"
if clang -target "$TARGET" -mmacosx-version-min=14.0 -c "$BRIDGE_ASM" -o "$BRIDGE_ASM_OBJ"; then
    echo "  Bridge:   $(basename "$BRIDGE_ASM")"
else
    echo "  Bridge failed: $(basename "$BRIDGE_ASM")"
    exit 1
fi

echo "  Compiled: $SUCCESS / $((SUCCESS + FAIL))"
echo "  Failed:   $FAIL"

if [ $FAIL -gt 0 ]; then
    echo ""
    echo "=== Failed files ==="
    for f in "${FAILED_FILES[@]}"; do
        echo "  $f"
    done
fi

# ============================================================
# Step 3: Link into dylib
# ============================================================
echo ""
echo "=== Linking libsystem_c.dylib ==="

OBJS=()
while IFS= read -r obj; do
    if [ "$obj" = "$OBJDIR/gen_FreeBSD_timezone.o" ]; then
        continue
    fi
    OBJS+=("$obj")
done < <(find "$OBJDIR" -name "*.o" ! -name "shims" | sort)

DYLD_STUB="${BUILDDIR}/obj/dyld_stub_binder.o"
if [ ! -f "$DYLD_STUB" ]; then
    if [ -f "$BUILD_STUB_BINDER" ]; then
        bash "$BUILD_STUB_BINDER"
    fi
fi
if [ ! -f "$DYLD_STUB" ]; then
    echo "missing required input: $DYLD_STUB" >&2
    exit 1
fi

LINKLOG="$OBJDIR/link_libsystem_c.log"
rm -f "$OUTDIR/libsystem_c.dylib" "$LINKLOG"

if ! xcrun ld -arch x86_64 -dylib \
    -install_name /usr/lib/system/libsystem_c.dylib \
    -o "$OUTDIR/libsystem_c.dylib" \
    "${OBJS[@]}" \
    "$DYLD_STUB" \
    -undefined dynamic_lookup \
    -not_for_dyld_shared_cache \
    -compatibility_version 1.0.0 \
    -current_version 1583.40.7 \
    -platform_version macos 14.0.0 14.0.0 \
    >"$LINKLOG" 2>&1; then
    tail -n 120 "$LINKLOG"
    echo "LINK FAILED"
    exit 1
fi

if [ -s "$LINKLOG" ]; then
    tail -n 20 "$LINKLOG"
fi

if [ -f "$OUTDIR/libsystem_c.dylib" ]; then
    echo ""
    echo "=== SUCCESS ==="
    echo "Output: $OUTDIR/libsystem_c.dylib"
    file "$OUTDIR/libsystem_c.dylib"
    ls -lh "$OUTDIR/libsystem_c.dylib"

    echo ""
    echo "=== Exported symbols ==="
    SYMCOUNT=$(nm -gU "$OUTDIR/libsystem_c.dylib" 2>/dev/null | wc -l | tr -d ' ')
    echo "Total exported symbols: $SYMCOUNT"

    echo ""
    echo "=== Key symbols sample ==="
    nm -gU "$OUTDIR/libsystem_c.dylib" 2>/dev/null | grep -E ' (T|S) _' | grep -iE '(printf|sprintf|fprintf|snprintf|vprintf|scanf|fopen|fclose|fread|fwrite|fgets|fputs|fflush|fseek|ftell|perror|strtol|strtoul|atoi|atol|atof|abs|exit|abort|atexit|getenv|setenv|qsort|bsearch|strerror|strdup|strndup|strcasecmp|strsep|strtok|memset|memcpy|memmove|memcmp|bcopy|sleep|usleep|nanosleep|alarm|pause|signal|raise|isatty|getcwd|opendir|readdir|closedir|time|localtime|strftime|setlocale|tolower|toupper|glob|fnmatch)$' | head -60
else
    echo "LINK FAILED"
fi
