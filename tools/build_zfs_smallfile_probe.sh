#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"
CC="${CC:-$(xcrun -find clang)}"
TARGET="${TARGET:-x86_64-apple-darwin23.0}"
MINVER="${MINVER:-14.0}"

"${CC}" \
  -target "${TARGET}" \
  -mmacosx-version-min="${MINVER}" \
  -isysroot "${SYSROOT}" \
  -O2 \
  -Wall \
  -Wextra \
  "${SCRIPT_DIR}/zfs_smallfile_probe.c" \
  -o "${SCRIPT_DIR}/zfs_smallfile_probe" \
  -L"${SYSROOT}/usr/lib" \
  -L"${SYSROOT}/usr/lib/system" \
  -Wl,-not_for_dyld_shared_cache \
  -lSystem

echo "Built ${SCRIPT_DIR}/zfs_smallfile_probe"
