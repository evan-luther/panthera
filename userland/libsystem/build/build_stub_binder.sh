#!/bin/bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILDDIR="${PANTHERA_ROOT}/userland/libsystem/build"
OBJDIR="${BUILDDIR}/obj"
SOURCESDIR="${BUILDDIR}/sources"
SRC="${SOURCESDIR}/dyld_stub_binder.s"
OUT="${OBJDIR}/dyld_stub_binder.o"
TARGET="x86_64-apple-darwin23.0"

if [[ ! -f "${SRC}" ]]; then
    echo "missing required input: ${SRC}" >&2
    exit 1
fi

mkdir -p "${OBJDIR}"

xcrun -sdk macosx clang -target "${TARGET}" -mmacosx-version-min=14.0 \
    -c "${SRC}" -o "${OUT}"
