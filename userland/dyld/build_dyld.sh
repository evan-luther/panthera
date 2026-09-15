#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DYLD_DIR="${PANTHERA_ROOT}/userland/dyld"

xcrun -sdk macosx clang++ \
    -target x86_64-apple-darwin23.0 \
    -mmacosx-version-min=14.0 \
    -std=c++17 \
    -nostdlib \
    -fno-exceptions \
    -fno-rtti \
    -fno-stack-protector \
    -fno-builtin \
    -ffreestanding \
    -Wall \
    -Wextra \
    -Wno-unused-parameter \
    -Wno-unused-function \
    -Wno-c++98-compat \
    -c "${DYLD_DIR}/panthera_dyld.cpp" \
    -o "${DYLD_DIR}/panthera_dyld.o"

xcrun -sdk macosx clang \
    -target x86_64-apple-darwin23.0 \
    -mmacosx-version-min=14.0 \
    -c "${DYLD_DIR}/dyldStartup.s" \
    -o "${DYLD_DIR}/dyldStartup.o"

xcrun ld \
    -arch x86_64 \
    -dylinker \
    -dylinker_install_name /usr/lib/dyld \
    -e __dyld_start \
    -o "${DYLD_DIR}/dyld" \
    "${DYLD_DIR}/dyldStartup.o" \
    "${DYLD_DIR}/panthera_dyld.o" \
    -platform_version macos 14.0.0 26.0.0

file "${DYLD_DIR}/dyld"
