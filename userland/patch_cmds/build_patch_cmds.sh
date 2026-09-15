#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PATCH_SRC="${PANTHERA_ROOT}/src/patch_cmds-72/patch"
DIFF_SRC="${PANTHERA_ROOT}/src/patch_cmds-72/diff"
BIN_DIR="${PANTHERA_ROOT}/userland/patch_cmds/bin"

CC="$(xcrun -find clang)"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
TARGET="x86_64-apple-darwin23.0"

mkdir -p "${BIN_DIR}"

if [[ "${PANTHERA_FORCE_REBUILD:-0}" != "1" && -x "${BIN_DIR}/patch" ]]; then
    echo "patch_cmds already built"
    exit 0
fi

CFLAGS="-target ${TARGET} -mmacosx-version-min=14.0 -isysroot ${SDKROOT} -O2"
CFLAGS="${CFLAGS} -Wno-implicit-function-declaration"

# Build patch
"${CC}" ${CFLAGS} -o "${BIN_DIR}/patch" \
    "${PATCH_SRC}/backupfile.c" \
    "${PATCH_SRC}/inp.c" \
    "${PATCH_SRC}/mkpath.c" \
    "${PATCH_SRC}/patch.c" \
    "${PATCH_SRC}/pch.c" \
    "${PATCH_SRC}/util.c" \
    "${PATCH_SRC}/vcs.c" \
    -lSystem

bash "${PANTHERA_ROOT}/tools/audit_package.sh" "${BIN_DIR}/patch"

# Build diff if source exists
if [[ -d "${DIFF_SRC}" ]]; then
    DIFF_SRCS=()
    shopt -s nullglob
    for f in "${DIFF_SRC}"/*.c; do
        DIFF_SRCS+=("${f}")
    done
    shopt -u nullglob
    if [[ ${#DIFF_SRCS[@]} -gt 0 ]]; then
        "${CC}" ${CFLAGS} -o "${BIN_DIR}/diff" "${DIFF_SRCS[@]}" -lSystem 2>/dev/null || \
            echo "diff build failed (optional, may need more headers)"
    fi
fi

echo "Built patch: ${BIN_DIR}/patch ($(wc -c < "${BIN_DIR}/patch") bytes)"
