#!/bin/bash
# build_shared_cache.sh — Compile both cache builders and publish the default cache
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CC="$(xcrun -find clang)"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
SYSROOT="$PANTHERA_ROOT/userland/libsystem/build/sysroot"
OUT_DIR="$PANTHERA_ROOT/images/shared_cache"
DEFAULT_CACHE="$OUT_DIR/dyld_shared_cache_x86_64"
REAL_CACHE="$OUT_DIR/dyld_shared_cache_x86_64_real"
SIMPLE_CACHE="$OUT_DIR/dyld_shared_cache_x86_64_simple"

echo "=== Compiling Phase A cache builder ==="
$CC -O2 -std=c17 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
    -isysroot "$SDKROOT" \
    -o "$PANTHERA_ROOT/tools/build_shared_cache" \
    "$PANTHERA_ROOT/tools/build_shared_cache_simple.c"

echo "=== Compiling real cache builder ==="
$CC -O2 -std=c17 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
    -isysroot "$SDKROOT" \
    -o "$PANTHERA_ROOT/tools/build_shared_cache_real" \
    "$PANTHERA_ROOT/tools/build_shared_cache_real.c"

echo "=== Generating Phase A cache ==="
mkdir -p "$OUT_DIR"
"$PANTHERA_ROOT/tools/build_shared_cache" \
    --sysroot "$SYSROOT" \
    --output "$SIMPLE_CACHE"

echo "=== Generating real cache ==="
"$PANTHERA_ROOT/tools/build_shared_cache_real" \
    --sysroot "$SYSROOT" \
    --output "$REAL_CACHE"

cp -f "$REAL_CACHE" "$DEFAULT_CACHE"

echo ""
echo "=== Done ==="
ls -lh "$DEFAULT_CACHE" "$REAL_CACHE" "$SIMPLE_CACHE"
