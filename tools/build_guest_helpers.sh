#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SDKROOT="${SDKROOT:-$(xcrun -sdk macosx --show-sdk-path)}"
CC="${CC:-$(xcrun -find clang)}"
TARGET="${TARGET:-x86_64-apple-darwin23.0}"
MINVER="${MINVER:-14.0}"
NCURSES_BUILD_SCRIPT="${PANTHERA_ROOT}/tools/build_apple_ncurses.sh"
NCURSES_SRC="${PANTHERA_ROOT}/src/apple-ncurses-71.100.2/ncurses"
NCURSES_BUILD_DIR="${PANTHERA_ROOT}/build/apple-ncurses-target"

if [[ -x "${NCURSES_BUILD_SCRIPT}" ]]; then
  bash "${NCURSES_BUILD_SCRIPT}" --target-lib
fi

common_cflags=(
  -target "${TARGET}"
  -mmacosx-version-min="${MINVER}"
  -isysroot "${SDKROOT}"
  -O2
  -Wall
  -Wextra
  -Werror
)

common_ldflags=(
  -target "${TARGET}"
  -mmacosx-version-min="${MINVER}"
  -isysroot "${SDKROOT}"
  -lSystem
)

ncurses_cflags=(
  -I"${NCURSES_BUILD_DIR}/include"
  -I"${NCURSES_SRC}/include"
  -I"${NCURSES_SRC}/ncurses"
)

ncurses_ldflags=(
  -L"${PANTHERA_ROOT}/userland/libsystem/build/sysroot/usr/lib"
  -lncurses.5.4
  -lSystem
)

"${CC}" \
  "${common_cflags[@]}" \
  "${ncurses_cflags[@]}" \
  "${SCRIPT_DIR}/ncurses_probe.c" \
  -o "${SCRIPT_DIR}/ncurses_probe" \
  "${common_ldflags[@]}" \
  "${ncurses_ldflags[@]}"

echo "Built ${SCRIPT_DIR}/ncurses_probe"

"${CC}" \
  "${common_cflags[@]}" \
  "${ncurses_cflags[@]}" \
  "${SCRIPT_DIR}/setupterm_probe.c" \
  -o "${SCRIPT_DIR}/setupterm_probe" \
  "${common_ldflags[@]}" \
  "${ncurses_ldflags[@]}"

echo "Built ${SCRIPT_DIR}/setupterm_probe"
