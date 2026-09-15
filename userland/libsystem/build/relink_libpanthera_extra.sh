#!/bin/bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILDDIR="${PANTHERA_ROOT}/userland/libsystem/build"
OBJDIR="${BUILDDIR}/obj"
SOURCESDIR="${BUILDDIR}/sources"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"
OUT="${SYSROOT}/usr/lib/system/libpanthera_extra.dylib"
SWEEP_LIST="${PANTHERA_ROOT}/userland/libsystem/build/libpanthera_extra_strip_symbols.txt"
SDK_PATH="$(xcrun -sdk macosx -show-sdk-path)"
LIBC_SRC="${PANTHERA_ROOT}/src/Libc-1583.40.7"
LIBPTHREAD_SRC="${PANTHERA_ROOT}/src/libpthread-519"
XNU_SRC="${PANTHERA_ROOT}/src/xnu-10002.41.9"

CC="xcrun -sdk macosx clang"
CFLAGS="-target x86_64-apple-darwin23.0 -mmacosx-version-min=14.0 -isysroot ${SDK_PATH} -fPIC -O2 -I${LIBPTHREAD_SRC}/private -I${SYSROOT}/usr/include -I${SDK_PATH}/usr/include -I${PANTHERA_ROOT}/userland/libsystem/build/compat_include -I${LIBC_SRC}/locale -I${LIBC_SRC}/locale/FreeBSD -I${LIBC_SRC}/stdtime/FreeBSD -I${XNU_SRC}/libsyscall -DPRIVATE -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_NON_CANCELABLE=1 -D__DARWIN_VERS_1050=1 -D_LIBC_NO_FEATURE_VERIFICATION=1 -D_FORTIFY_SOURCE=0 -w"
CORE_SRC="${SOURCESDIR}/panthera_extra_core.c"
STUBS_SRC="${SOURCESDIR}/panthera_extra_stubs.c"
BRIDGE_SRC="${SOURCESDIR}/panthera_extra_bridge.c"
ALIASES_SRC="${SOURCESDIR}/panthera_extra_aliases.s"
FMTCHECK_SRC="${SOURCESDIR}/panthera_fmtcheck_wrapper.c"
CACHE_BRIDGE_SRC="${SOURCESDIR}/panthera_cache_bridge_stubs.c"
CACHE_BRIDGE_ASM="${SOURCESDIR}/panthera_cache_bridge_aliases.s"
LIBUNWIND_ARCHIVE="${PANTHERA_ROOT}/userland/libunwind/lib/libunwind.a"
LIBUNWIND_SAVE_OBJ="${PANTHERA_ROOT}/userland/libunwind/obj/UnwindRegistersSave.o"
LIBUNWIND_RESTORE_OBJ="${PANTHERA_ROOT}/userland/libunwind/obj/UnwindRegistersRestore.o"

CORE_OBJ="${OBJDIR}/panthera_extra_core.o"
STUBS_OBJ="${OBJDIR}/panthera_extra_stubs.o"
BRIDGE_OBJ="${OBJDIR}/panthera_extra_bridge.o"
ALIASES_OBJ="${OBJDIR}/panthera_extra_aliases.o"
FMTCHECK_OBJ="${OBJDIR}/fmtcheck.o"
TIER1_OBJ="${OBJDIR}/panthera_tier1_syms.o"
CACHE_BRIDGE_OBJ="${OBJDIR}/panthera_cache_bridge_stubs.o"
CACHE_BRIDGE_ASM_OBJ="${OBJDIR}/panthera_cache_bridge_aliases.o"

UNEXPORTED="$(mktemp /tmp/panthera_libpanthera_extra_unexported.XXXXXX)"
CURRENT_EXPORTS="$(mktemp /tmp/panthera_libpanthera_extra_current.XXXXXX)"
NEW_EXPORTS="$(mktemp /tmp/panthera_libpanthera_extra_new.XXXXXX)"
trap 'rm -f "${UNEXPORTED}" "${CURRENT_EXPORTS}" "${NEW_EXPORTS}"' EXIT

required=(
    "${CORE_SRC}"
    "${SOURCESDIR}/panthera_resolve_impl.c"
    "${SOURCESDIR}/panthera_mach_globals.c"
    "${SOURCESDIR}/panthera_malloc_override.c"
    "${STUBS_SRC}"
    "${SOURCESDIR}/panthera_missing.c"
    "${SOURCESDIR}/panthera_pthread_simple.c"
    "${BRIDGE_SRC}"
    "${SOURCESDIR}/panthera_resolve_wave2.c"
    "${SOURCESDIR}/panthera_resolve_wave3.c"
    "${SOURCESDIR}/clang_runtime_impl.c"
    "${ALIASES_SRC}"
    "${FMTCHECK_SRC}"
    "${LIBC_SRC}/gen/FreeBSD/fmtcheck.c"
    "${LIBPTHREAD_SRC}/private/pthread/tsd_private.h"
    "${XNU_SRC}/libsyscall/os/tsd.h"
    "${CACHE_BRIDGE_SRC}"
    "${CACHE_BRIDGE_ASM}"
    "${SWEEP_LIST}"
    "${LIBUNWIND_ARCHIVE}"
    "${LIBUNWIND_SAVE_OBJ}"
    "${LIBUNWIND_RESTORE_OBJ}"
)

for path in "${required[@]}"; do
    if [[ ! -f "${path}" ]]; then
        echo "missing required input: ${path}" >&2
        exit 1
    fi
done
mkdir -p "${OBJDIR}" "$(dirname "${OUT}")"


if [[ -f "${OUT}" ]]; then
    nm -gU "${OUT}" | awk '{print $NF}' | sed '/^$/d' | sort -u > "${CURRENT_EXPORTS}"
else
    : > "${CURRENT_EXPORTS}"
fi

${CC} ${CFLAGS} -c "${CORE_SRC}" -o "${CORE_OBJ}"
${CC} ${CFLAGS} -c "${STUBS_SRC}" -o "${STUBS_OBJ}"
${CC} ${CFLAGS} -c "${BRIDGE_SRC}" -o "${BRIDGE_OBJ}"
${CC} ${CFLAGS} -c "${ALIASES_SRC}" -o "${ALIASES_OBJ}"
${CC} ${CFLAGS} -c "${FMTCHECK_SRC}" -o "${FMTCHECK_OBJ}"
${CC} ${CFLAGS} -c "${CACHE_BRIDGE_SRC}" -o "${CACHE_BRIDGE_OBJ}"
${CC} ${CFLAGS} -c "${CACHE_BRIDGE_ASM}" -o "${CACHE_BRIDGE_ASM_OBJ}"

TIER1_SRC="${SOURCESDIR}/panthera_tier1_syms.c"
if [[ -f "${TIER1_SRC}" ]]; then
    ${CC} ${CFLAGS} -c "${TIER1_SRC}" -o "${TIER1_OBJ}"
fi

cat > "${UNEXPORTED}" <<'EOF'
_dyld_stub_binder
dyld_stub_binder
EOF

cat "${SWEEP_LIST}" >> "${UNEXPORTED}"

xcrun ld -arch x86_64 -dylib \
    -install_name /usr/lib/system/libpanthera_extra.dylib \
    -o "${OUT}" \
    "${CORE_OBJ}" \
    "${STUBS_OBJ}" \
    "${BRIDGE_OBJ}" \
    "${ALIASES_OBJ}" \
    "${FMTCHECK_OBJ}" \
    "${CACHE_BRIDGE_OBJ}" \
    "${CACHE_BRIDGE_ASM_OBJ}" \
    $(test -f "${TIER1_OBJ}" && echo "${TIER1_OBJ}") \
    -force_load "${LIBUNWIND_ARCHIVE}" \
    "${LIBUNWIND_SAVE_OBJ}" \
    "${LIBUNWIND_RESTORE_OBJ}" \
    -not_for_dyld_shared_cache \
    -undefined dynamic_lookup \
    -unexported_symbols_list "${UNEXPORTED}" \
    -platform_version macos 14.0.0 26.0.0

nm -gU "${CORE_OBJ}" "${STUBS_OBJ}" "${BRIDGE_OBJ}" "${ALIASES_OBJ}" "${FMTCHECK_OBJ}" "${CACHE_BRIDGE_OBJ}" "${CACHE_BRIDGE_ASM_OBJ}" $(test -f "${TIER1_OBJ}" && echo "${TIER1_OBJ}") "${LIBUNWIND_ARCHIVE}" "${LIBUNWIND_SAVE_OBJ}" "${LIBUNWIND_RESTORE_OBJ}" \
    | awk '{print $NF}' | sed '/^$/d' | sort -u > "${NEW_EXPORTS}"

missing_from_new="$(comm -23 "${CURRENT_EXPORTS}" "${NEW_EXPORTS}" || true)"
if [[ -n "${missing_from_new}" ]]; then
    echo "warning: consolidated sources do not cover all previous raw exports:" >&2
    echo "${missing_from_new}" >&2
fi

final_count="$(nm -gU "${OUT}" | awk '{print $NF}' | sed '/^$/d' | sort -u | wc -l | tr -d ' ')"
echo "libpanthera_extra exports ${final_count} symbols"
