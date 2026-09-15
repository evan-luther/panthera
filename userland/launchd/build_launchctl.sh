#!/bin/bash
# build_launchctl.sh — Reproducible build for Panthera launchctl
# Pinned support/launchctl.c linked target-only against staged userland libraries.
set -euo pipefail

DEPLOYMENT_TARGET="${DEPLOYMENT_TARGET:-14.0}"
TARGET="x86_64-apple-darwin23.0"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"
OBJDIR="${SCRIPT_DIR}/obj"
MIG_GEN_SRC="${OBJDIR}/mig_gen"
CF_INCLUDE_DIR="${PANTHERA_ROOT}/userland/configd/build/SystemConfiguration/include"
CF_PRIV_HEADER="${CF_INCLUDE_DIR}/CoreFoundation/CFPriv.h"

LAUNCHD_SRC="${PANTHERA_ROOT}/src/launchd-842.92.1/src"
LIBLAUNCH_SRC="${PANTHERA_ROOT}/src/launchd-842.92.1/liblaunch"
LAUNCHCTL_SRC="${PANTHERA_ROOT}/src/launchd-842.92.1/support/launchctl.c"
OUT_BIN="${SCRIPT_DIR}/launchctl"

SDK="$(xcrun -sdk macosx --show-sdk-path)"
CC="$(xcrun -sdk macosx -find clang)"
LD="$(xcrun -sdk macosx -find ld)"

# Locate Darwin compiler runtime (libclang_rt.osx.a)
CLANG_RT="$("${CC}" -target "${TARGET}" -print-libgcc-file-name 2>/dev/null || true)"
if [ -z "${CLANG_RT}" ] || [ ! -f "${CLANG_RT}" ]; then
  CLANG_RT="$("${CC}" -print-file-name=libclang_rt.osx.a 2>/dev/null || true)"
fi
if [ -z "${CLANG_RT}" ] || [ ! -f "${CLANG_RT}" ]; then
  CLANG_RESOURCE_DIR="$("${CC}" -print-resource-dir 2>/dev/null || true)"
  if [ -n "${CLANG_RESOURCE_DIR}" ] && [ -f "${CLANG_RESOURCE_DIR}/lib/darwin/libclang_rt.osx.a" ]; then
    CLANG_RT="${CLANG_RESOURCE_DIR}/lib/darwin/libclang_rt.osx.a"
  fi
fi

if [ -z "${CLANG_RT}" ] || [ ! -f "${CLANG_RT}" ]; then
  echo "ERROR: Unable to locate Darwin compiler runtime archive (libclang_rt.osx.a) using ${CC}" >&2
  exit 1
fi

LIBCOREFOUNDATION="${SYSROOT}/usr/lib/libCoreFoundation.dylib"
LIBIOKIT="${SYSROOT}/usr/lib/libIOKit.dylib"
LIBEDIT="${SYSROOT}/usr/lib/libedit.dylib"
LIBBSM="${SYSROOT}/usr/lib/libbsm.0.dylib"
LIBSYSTEM="${SYSROOT}/usr/lib/libSystem.B.dylib"

for lib in "${LIBCOREFOUNDATION}" "${LIBIOKIT}" "${LIBEDIT}" "${LIBBSM}" "${LIBSYSTEM}"; do
  if [[ ! -f "${lib}" ]]; then
    echo "Error: missing required staged library: ${lib}" >&2
    exit 1
  fi
done

if [[ ! -f "${LAUNCHCTL_SRC}" ]]; then
  echo "Error: missing pinned launchctl source file: ${LAUNCHCTL_SRC}" >&2
  exit 1
fi
if [[ ! -f "${CF_PRIV_HEADER}" ]]; then
  echo "Error: missing required staged CoreFoundation private header: ${CF_PRIV_HEADER}" >&2
  exit 1
fi


REQUIRED_LAUNCHD_OBJS=(
  "${OBJDIR}/liblaunch_libbootstrap.o"
  "${OBJDIR}/liblaunch_liblaunch.o"
  "${OBJDIR}/liblaunch_libvproc.o"
  "${OBJDIR}/mig_gen_jobUser.o"
  "${OBJDIR}/mig_gen_internalUser.o"
  "${OBJDIR}/mig_gen_helperServer.o"
  "${OBJDIR}/panthera_link_stubs.o"
)

missing_prereqs=0
for obj in "${REQUIRED_LAUNCHD_OBJS[@]}"; do
  if [[ ! -f "${obj}" ]]; then
    missing_prereqs=1
    break
  fi
done

if [[ "${missing_prereqs}" -eq 1 ]]; then
  echo "Prerequisite launchd objects missing; invoking canonical build_launchd.sh..."
  bash "${SCRIPT_DIR}/build_launchd.sh"
fi

for obj in "${REQUIRED_LAUNCHD_OBJS[@]}"; do
  if [[ ! -f "${obj}" ]]; then
    echo "Error: required prerequisite launchd object not found: ${obj}" >&2
    exit 1
  fi
done

mkdir -p "${OBJDIR}"

echo "=== Building launchctl ==="
echo "  Sysroot: ${SYSROOT}"
echo "  Output:  ${OUT_BIN}"

CFLAGS="-target ${TARGET} -mmacosx-version-min=${DEPLOYMENT_TARGET} -isysroot ${SDK}"
CFLAGS+=" -I ${SCRIPT_DIR}/real/shims"
CFLAGS+=" -I ${MIG_GEN_SRC}"
CFLAGS+=" -I ${PANTHERA_ROOT}/src/launchd-842.92.1/stubs"
CFLAGS+=" -I ${PANTHERA_ROOT}/src/launchd-842.92.1/liblaunch"
CFLAGS+=" -I ${LAUNCHD_SRC}"
CFLAGS+=" -I ${CF_INCLUDE_DIR}"
CFLAGS+=" -include ${SCRIPT_DIR}/real/panthera_launchd_config.h"
CFLAGS+=" -include ${SCRIPT_DIR}/launchd_build_compat.h"
CFLAGS+=" -D_DARWIN_C_SOURCE -DPANTHERA=1"
CFLAGS+=" -Wno-everything -O2"

echo "  [CC] launchctl.c"
"${CC}" ${CFLAGS} \
  -c -o "${OBJDIR}/src_launchctl.o" \
  "${LAUNCHCTL_SRC}"

echo "  [LD] launchctl"
LAUNCHCTL_OBJECTS=(
  "${OBJDIR}/src_launchctl.o"
  "${REQUIRED_LAUNCHD_OBJS[@]}"
)

"${LD}" -arch x86_64 \
  -platform_version macos "${DEPLOYMENT_TARGET}" 14.0.0 \
  -syslibroot "${SYSROOT}" \
  -o "${OUT_BIN}" \
  "${LAUNCHCTL_OBJECTS[@]}" \
  "${LIBCOREFOUNDATION}" \
  "${LIBIOKIT}" \
  "${LIBEDIT}" \
  "${LIBBSM}" \
  "${LIBSYSTEM}" \
  "${CLANG_RT}"

echo ""
echo "=== Verification ==="
file "${OUT_BIN}"
ls -la "${OUT_BIN}"
echo "Exported symbols: $(nm -gU "${OUT_BIN}" 2>/dev/null | wc -l | tr -d ' ')"

echo ""
echo "Checking undefined symbols against sysroot..."
EXPORT_TABLE="$(mktemp)"
trap 'rm -f "${EXPORT_TABLE}"' EXIT
for lib in "${SYSROOT}"/usr/lib/system/*.dylib "${SYSROOT}"/usr/lib/*.dylib; do
  [ -f "${lib}" ] || continue
  nm -gU "${lib}" 2>/dev/null | awk '{print $NF}' >> "${EXPORT_TABLE}"
done
sort -u "${EXPORT_TABLE}" -o "${EXPORT_TABLE}"

UNRESOLVED_LOG="$(mktemp)"
nm -gu "${OUT_BIN}" 2>/dev/null | awk '{print $NF}' | while IFS= read -r sym; do
  [ -z "${sym}" ] && continue
  if ! grep -qFx "${sym}" "${EXPORT_TABLE}"; then
    echo "  UNRESOLVED: ${sym}"
  fi
done > "${UNRESOLVED_LOG}" 2>&1

if [ -s "${UNRESOLVED_LOG}" ]; then
  cat "${UNRESOLVED_LOG}"
  MISSING="$(grep -c UNRESOLVED "${UNRESOLVED_LOG}" || true)"
  echo "  WARNING: ${MISSING} unresolved symbol(s)"
  echo "  launchctl may crash at runtime due to missing symbols."
  rm -f "${UNRESOLVED_LOG}"
  exit 1
else
  echo "  All symbols resolved against sysroot."
  rm -f "${UNRESOLVED_LOG}"
fi

echo ""
echo "=== Build complete: ${OUT_BIN} ==="
