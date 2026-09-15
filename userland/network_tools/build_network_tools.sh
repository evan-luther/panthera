#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OUT_DIR="${SCRIPT_DIR}/bin"
SDKROOT="${SDKROOT:-$(xcrun -sdk macosx --show-sdk-path)}"
CC="${CC:-$(xcrun -find clang)}"
TARGET="${TARGET:-x86_64-apple-darwin23.0}"
MINVER="${MINVER:-14.0}"

mkdir -p "${OUT_DIR}"

common_cflags=(
  -target "${TARGET}"
  -mmacosx-version-min="${MINVER}"
  -isysroot "${SDKROOT}"
  -O2
  -Wall
  -Wextra
  -Wno-deprecated-declarations
)

common_ldflags=(
  -target "${TARGET}"
  -mmacosx-version-min="${MINVER}"
  -isysroot "${SDKROOT}"
  -lSystem
)

build_one() {
  local src="$1"
  local out="$2"
  echo "Building ${out##*/} from ${src##*/}"
  "${CC}" "${common_cflags[@]}" "${SCRIPT_DIR}/${src}" -o "${OUT_DIR}/${out}" "${common_ldflags[@]}"
}

build_one ifconfig.c ifconfig
build_one route.c route
build_one ping.c ping
build_one netstat.c netstat
build_one netbringup.c netbringup
build_one netprobe_step.c netprobe_step
build_one dnsprobe.c dnsprobe

echo
echo "Built network tools in ${OUT_DIR}"
