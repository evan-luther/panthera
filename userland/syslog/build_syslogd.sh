#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT_ROOT="${PANTHERA_ROOT}/userland/syslog"
SBIN_DIR="${OUT_ROOT}/sbin"

SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"
SDKROOT="${SDKROOT:-$(xcrun -sdk macosx --show-sdk-path)}"
CC="${CC:-$(xcrun -find clang)}"
TARGET="${TARGET:-x86_64-apple-darwin23.0}"
MINVER="${MINVER:-14.0}"

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

mkdir -p "${SBIN_DIR}"

if [[ "${PANTHERA_FORCE_REBUILD:-0}" != "1" \
   && -x "${SBIN_DIR}/syslogd" ]]; then
  echo "syslogd already built"
  exit 0
fi

"${CC}" -target "${TARGET}" -mmacosx-version-min="${MINVER}" -isysroot "${SDKROOT}" \
    -Wl,-syslibroot,"${SYSROOT}" \
    -nodefaultlibs \
    -O2 -Wall -Wextra \
    -o "${SBIN_DIR}/syslogd" \
    "${OUT_ROOT}/syslogd.c" \
    "${SYSROOT}/usr/lib/system/libpanthera_extra.dylib" \
    "${SYSROOT}/usr/lib/libSystem.B.dylib" \
    "${CLANG_RT}" \
    -Wl,-not_for_dyld_shared_cache

echo "Built syslogd: ${SBIN_DIR}/syslogd"
