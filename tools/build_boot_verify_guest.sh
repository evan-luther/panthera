#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"
SDKROOT="${SDKROOT:-$(xcrun -sdk macosx --show-sdk-path)}"
CC="${CC:-$(xcrun -find clang)}"
TARGET="${TARGET:-x86_64-apple-darwin23.0}"
MINVER="${MINVER:-14.0}"
RUN_NETWORK="${PANTHERA_BOOT_VERIFY_RUN_NETWORK:-0}"

LIBSYSTEM_B="${SYSROOT}/usr/lib/libSystem.B.dylib"
CORE_FOUNDATION_DYLIB="${SYSROOT}/usr/lib/libCoreFoundation.dylib"
TESTS_DIR="${PANTHERA_ROOT}/tests"

usage() {
  cat <<EOF
Usage: $(basename "$0") [--run-network]

Builds tools/boot_verify_guest, tests/test_bootstrap_simple, and tests/test_cf.
By default, network probes are compiled as expected skips. Use --run-network or
PANTHERA_BOOT_VERIFY_RUN_NETWORK=1 to compile bounded network probes into the
guest verifier.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --run-network)
      RUN_NETWORK=1
      shift
      ;;
    --help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ ! -f "${LIBSYSTEM_B}" ]]; then
  echo "Error: Canonical aggregate runtime missing: ${LIBSYSTEM_B}" >&2
  exit 1
fi

if [[ ! -f "${CORE_FOUNDATION_DYLIB}" && -x "${PANTHERA_ROOT}/userland/corefoundation/build_cf.sh" ]]; then
  bash "${PANTHERA_ROOT}/userland/corefoundation/build_cf.sh"
fi

if [[ ! -f "${CORE_FOUNDATION_DYLIB}" ]]; then
  echo "Error: CoreFoundation runtime missing: ${CORE_FOUNDATION_DYLIB}" >&2
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

CFLAGS=()
if [[ "${RUN_NETWORK}" == "1" ]]; then
  CFLAGS+=(-DPANTHERA_BOOT_VERIFY_RUN_NETWORK=1)
fi

mkdir -p "${TESTS_DIR}"

"${CC}" \
  -target "${TARGET}" \
  -mmacosx-version-min="${MINVER}" \
  -isysroot "${SDKROOT}" \
  -O2 \
  -Wall \
  -Wextra \
  "${CFLAGS[@]}" \
  "${SCRIPT_DIR}/boot_verify_guest.c" \
  -o "${SCRIPT_DIR}/boot_verify_guest" \
  -Wl,-syslibroot,"${SYSROOT}" \
  -nodefaultlibs \
  "${LIBSYSTEM_B}" \
  "${CLANG_RT}" \
  -Wl,-not_for_dyld_shared_cache

echo "Built ${SCRIPT_DIR}/boot_verify_guest"

"${CC}" \
  -target "${TARGET}" \
  -mmacosx-version-min="${MINVER}" \
  -isysroot "${SDKROOT}" \
  -O2 \
  -Wall \
  -Wextra \
  "${TESTS_DIR}/test_bootstrap_simple.c" \
  -o "${TESTS_DIR}/test_bootstrap_simple" \
  -Wl,-syslibroot,"${SYSROOT}" \
  -nodefaultlibs \
  "${LIBSYSTEM_B}" \
  "${CLANG_RT}" \
  -Wl,-not_for_dyld_shared_cache

echo "Built ${TESTS_DIR}/test_bootstrap_simple"

"${CC}" \
  -target "${TARGET}" \
  -mmacosx-version-min="${MINVER}" \
  -isysroot "${SDKROOT}" \
  -O2 \
  -Wall \
  -Wextra \
  "${TESTS_DIR}/test_cf.c" \
  -o "${TESTS_DIR}/test_cf" \
  -Wl,-syslibroot,"${SYSROOT}" \
  -nodefaultlibs \
  -L"${SYSROOT}/usr/lib" \
  -L"${SYSROOT}/usr/lib/system" \
  -lpanthera_extra \
  "${CORE_FOUNDATION_DYLIB}" \
  "${LIBSYSTEM_B}" \
  "${CLANG_RT}" \
  -Wl,-not_for_dyld_shared_cache

echo "Built ${TESTS_DIR}/test_cf"

if [[ "${RUN_NETWORK}" == "1" ]]; then
  echo "Network probes: enabled"
else
  echo "Network probes: skipped"
fi
