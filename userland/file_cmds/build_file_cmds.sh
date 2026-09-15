#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRCDIR="${PANTHERA_ROOT}/src/file_cmds-475"
BIN_DIR="${PANTHERA_ROOT}/userland/file_cmds/bin"

CC="${CC:-$(xcrun -find clang)}"
SDKROOT="${SDKROOT:-$(xcrun -sdk macosx --show-sdk-path)}"
TARGET="${TARGET:-x86_64-apple-darwin23.0}"
MINVER="${MINVER:-14.0}"

mkdir -p "${BIN_DIR}"

if [[ "${PANTHERA_FORCE_REBUILD:-0}" != "1" && -x "${BIN_DIR}/install" ]]; then
  echo "file_cmds already built"
  exit 0
fi

"${CC}" \
  -target "${TARGET}" \
  -mmacosx-version-min="${MINVER}" \
  -isysroot "${SDKROOT}" \
  -O2 \
  -UTARGET_OS_OSX \
  -Wno-implicit-function-declaration \
  -I"${SRCDIR}/install" \
  -o "${BIN_DIR}/install" \
  "${SRCDIR}/install/xinstall.c" \
  "${PANTHERA_ROOT}/userland/file_cmds/panthera_setmode.c" \
  -lSystem

bash "${PANTHERA_ROOT}/tools/audit_package.sh" "${BIN_DIR}/install"

echo "Built install: ${BIN_DIR}/install ($(wc -c < "${BIN_DIR}/install") bytes)"
