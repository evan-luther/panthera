#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
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

"${CC}" \
  "${common_cflags[@]}" \
  "${SCRIPT_DIR}/login.c" \
  -o "${OUT_DIR}/login" \
  "${common_ldflags[@]}"

echo "Built ${OUT_DIR}/login"
