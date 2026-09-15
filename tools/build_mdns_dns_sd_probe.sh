#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"
SDKROOT="${SDKROOT:-$(xcrun -sdk macosx --show-sdk-path)}"
CC="${CC:-$(xcrun -find clang)}"
TARGET="${TARGET:-x86_64-apple-darwin23.0}"
MINVER="${MINVER:-14.0}"
MDNS_SRC="${PANTHERA_ROOT}/src/mDNSResponder-1790.80.10"
OUT="${SCRIPT_DIR}/mdns_dns_sd_probe"
LIBSYSTEM_B="${SYSROOT}/usr/lib/libSystem.B.dylib"

if [[ ! -f "${LIBSYSTEM_B}" ]]; then
  echo "Error: Canonical aggregate runtime missing: ${LIBSYSTEM_B}" >&2
  exit 1
fi

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

"${CC}" \
  -target "${TARGET}" \
  -mmacosx-version-min="${MINVER}" \
  -isysroot "${SDKROOT}" \
  -O2 \
  -Wall \
  -Wextra \
  -I"${MDNS_SRC}/mDNSShared" \
  "${SCRIPT_DIR}/mdns_dns_sd_probe.c" \
  -o "${OUT}" \
  -Wl,-syslibroot,"${SYSROOT}" \
  -nodefaultlibs \
  "${LIBSYSTEM_B}" \
  "${CLANG_RT}" \
  -Wl,-not_for_dyld_shared_cache

echo "Built ${OUT}"
