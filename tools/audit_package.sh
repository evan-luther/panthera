#!/bin/bash
# audit_package.sh — Pre-flight symbol audit for Panthera packages
# Usage: bash tools/audit_package.sh path/to/binary [path/to/lib.dylib ...]
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"

if [[ $# -eq 0 ]]; then
  echo "Usage: $0 <binary> [dylib ...]"
  echo "Audits binaries for missing symbols and flat namespace issues."
  exit 1
fi

undef=$(mktemp /tmp/panthera-audit-undef.XXXXXX)
exports=$(mktemp /tmp/panthera-audit-exports.XXXXXX)
trap 'rm -f "$undef" "$exports"' EXIT

# Collect all undefined symbols from all inputs
for bin in "$@"; do
  if [[ ! -f "$bin" ]]; then
    echo "ERROR: $bin not found"
    exit 1
  fi
  nm -mu "$bin" 2>/dev/null | grep '(undefined)' | sed 's/.*(undefined) \(weak \)\{0,1\}external //' | sed 's/ (from .*//' || true
done | sort -u > "$undef"

# Collect all sysroot exports
{ nm -gU "$SYSROOT"/usr/lib/system/*.dylib "$SYSROOT"/usr/lib/*.dylib 2>/dev/null || true; } \
    | awk '{print $NF}' | sort -u > "$exports"

missing=$(comm -23 "$undef" "$exports" || true)

# Check namespace
flat_issues=""
for bin in "$@"; do
  # Look for undefined externals without "(from ...)" — indicates flat namespace
  bad=$(nm -m "$bin" 2>/dev/null \
    | grep '(undefined) external' \
    | grep -v '(from ' \
    | grep -v 'dyld_stub_binder' \
    | head -5 || true)
  if [[ -n "$bad" ]]; then
    flat_issues="${flat_issues}  ${bin}:\n${bad}\n"
  fi
done

echo "=== Panthera Package Audit ==="
echo "Inputs: $*"
echo "Sysroot: ${SYSROOT}"
echo ""

total_undef=$(wc -l < "$undef" | tr -d ' ')
echo "Total undefined symbols: ${total_undef}"
echo ""

if [[ -z "$missing" ]]; then
  echo "SYMBOLS: PASS — all ${total_undef} symbols resolve in sysroot"
else
  missing_count=$(echo "$missing" | wc -l | tr -d ' ')
  echo "SYMBOLS: FAIL — ${missing_count} missing:"
  echo "$missing" | sed 's/^/  /'
  echo ""
  echo "Fix with:"
  echo "  bash userland/libsystem/build/panthera_patch.sh stub <symbol>    # safe no-op"
  echo "  bash userland/libsystem/build/panthera_patch.sh impl <file.c>    # real implementation"
  echo "  bash userland/libsystem/build/panthera_patch.sh alias <new> <existing>"
  echo "  bash userland/libsystem/build/panthera_patch.sh unix2003 <func>  # \$UNIX2003 variant"
  echo ""
  echo "Then run:"
  echo "  bash userland/libsystem/build/relink_libpanthera_extra.sh"
  echo "  bash userland/libsystem/build/relink_libSystem.sh"
  echo "  bash tools/build_shared_cache.sh"
  echo "  bash userland/libsystem/verify_exports.sh"
fi

echo ""
if [[ -n "$flat_issues" ]]; then
  echo "NAMESPACE: FAIL — flat namespace detected (will break at runtime):"
  echo -e "$flat_issues"
  echo "Remove -Wl,-flat_namespace and -Wl,-undefined,dynamic_lookup from LDFLAGS"
else
  echo "NAMESPACE: PASS — all binaries use two-level namespace"
fi

echo ""
echo "=== Done ==="
