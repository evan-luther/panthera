#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT=""

usage() {
  cat <<EOF
Usage: $(basename "$0") [--output PATH]

Summarizes active Panthera runtime debt for Phase 6:
  - launchd linkage mode
  - Panthera conditional blocks in launchd/dyld
  - flat namespace and dynamic lookup sightings
  - inactive-looking shim file references
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --output)
      OUTPUT="$2"
      shift 2
      ;;
    --help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

cd "${PANTHERA_ROOT}"

if [[ -n "${OUTPUT}" ]]; then
  mkdir -p "$(dirname "${OUTPUT}")"
  exec >"${OUTPUT}"
fi

count_matches() {
	local pattern="$1"
	shift
	{
		grep -R -n -E -- "$pattern" "$@" 2>/dev/null || true
	} | wc -l | tr -d ' '
}

print_matches() {
	local pattern="$1"
	shift
	grep -R -n -E -- "$pattern" "$@" 2>/dev/null || true
}

launchd_linkage="missing"
if [[ -f userland/launchd/launchd ]]; then
  if otool -hv userland/launchd/launchd 2>/dev/null | grep -q "TWOLEVEL"; then
    launchd_linkage="two-level"
  else
    launchd_linkage="flat-or-unknown"
  fi
fi

launchd_panthera_count="$(
  count_matches '#if PANTHERA|#ifdef PANTHERA|#if !PANTHERA|#ifndef PANTHERA' \
    userland/launchd/real
)"
dyld_panthera_count="$(
  count_matches 'Panthera|PANTHERA|PID 1|bootstrap_port|ttyname_shim' \
    userland/dyld/panthera_dyld.cpp
)"
active_flat_namespace_count="$(
  {
    grep -R -n -E -- '-flat_namespace|flat_namespace' userland tools 2>/dev/null \
      | grep -E -v '^tools/audit_runtime_debt.sh:' || true
  } | wc -l | tr -d ' '
)"
active_dynamic_lookup_count="$(
  {
    grep -R -n -E -- '-undefined dynamic_lookup|undefined dynamic_lookup' userland tools 2>/dev/null \
      | grep -E -v '^tools/audit_runtime_debt.sh:' || true
  } | wc -l | tr -d ' '
)"
all_flat_namespace_count="$(
  count_matches '-flat_namespace|flat_namespace' \
    userland tools docs 2>/dev/null || true
)"
all_dynamic_lookup_count="$(
  count_matches '-undefined dynamic_lookup|undefined dynamic_lookup' \
    userland tools docs 2>/dev/null || true
)"

cat <<EOF
# Panthera Runtime Debt Audit

## Summary

| Area | Count/Status |
|---|---|
| launchd linkage | ${launchd_linkage} |
| launchd Panthera conditional blocks | ${launchd_panthera_count} |
| dyld Panthera/PID1 markers | ${dyld_panthera_count} |
| active flat namespace sightings | ${active_flat_namespace_count} |
| active dynamic lookup sightings | ${active_dynamic_lookup_count} |
| all flat namespace text sightings | ${all_flat_namespace_count} |
| all dynamic lookup text sightings | ${all_dynamic_lookup_count} |

## launchd Linkage

EOF

if [[ -f userland/launchd/launchd ]]; then
  otool -hv userland/launchd/launchd 2>/dev/null
else
  echo "userland/launchd/launchd is missing."
fi

cat <<EOF

## launchd Panthera Conditionals

EOF
print_matches '#if PANTHERA|#ifdef PANTHERA|#if !PANTHERA|#ifndef PANTHERA' \
  userland/launchd/real

cat <<EOF

## dyld Panthera/PID1 Markers

EOF
print_matches 'Panthera|PANTHERA|PID 1|bootstrap_port|ttyname_shim' \
  userland/dyld/panthera_dyld.cpp

cat <<EOF

## Namespace And Dynamic Lookup Sightings

EOF
print_matches '-flat_namespace|flat_namespace|-undefined dynamic_lookup|undefined dynamic_lookup' \
  userland tools | grep -E -v '^tools/audit_runtime_debt.sh:' || true

cat <<EOF

## Documentation And Historical Namespace Mentions

EOF
print_matches '-flat_namespace|flat_namespace|-undefined dynamic_lookup|undefined dynamic_lookup' \
  docs

cat <<EOF

## Inactive-Looking Shim File Recheck

Referenced means the filename appears in active userland/tool build paths,
excluding documentation and the candidate file itself.

| File | Referenced Outside Itself |
|---|---|
EOF

dead_candidates=(
  panthera_runtime_bridge.c
  panthera_boot_bridge.c
  panthera_layer7.c
  panthera_mach_funcs.c
  panthera_patch.c
  panthera_printf.c
  panthera_remaining.c
  panthera_stdio_init.c
  panthera_if_nametoindex.c
  panthera_inet_ntop.c
)

for file in "${dead_candidates[@]}"; do
  refs="$(
    grep -R -n -F -- "${file}" userland tools 2>/dev/null \
      | grep -E -v "${file}:|^tools/audit_runtime_debt.sh:" || true
  )"
  if [[ -n "${refs}" ]]; then
    printf '| `%s` | yes |\n' "${file}"
  else
    printf '| `%s` | no |\n' "${file}"
  fi
done

cat <<EOF

## Active Build References For Inactive-Looking Shim Files

EOF
for file in "${dead_candidates[@]}"; do
  refs="$(
    grep -R -n -F -- "${file}" userland tools 2>/dev/null \
      | grep -E -v "${file}:|^tools/audit_runtime_debt.sh:" || true
  )"
  if [[ -n "${refs}" ]]; then
    echo "### ${file}"
    echo
    echo "${refs}"
    echo
  fi
done
