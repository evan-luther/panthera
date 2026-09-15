#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MANIFEST="${ROOT}/docs/provenance/THIRD_PARTY_CHECKSUMS.txt"

usage() {
  cat <<EOF
Usage: $(basename "$0") [--manifest PATH]

Verifies SHA256 checksums listed in docs/provenance/THIRD_PARTY_CHECKSUMS.txt.
Manifest lines use:
  sha256  path-relative-to-repo-root
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --manifest)
      MANIFEST="$2"
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

if [[ ! -f "${MANIFEST}" ]]; then
  echo "checksum manifest missing: ${MANIFEST}" >&2
  exit 1
fi

cd "${ROOT}"

total=0
failed=0
missing=0

while read -r expected path _; do
  [[ -z "${expected:-}" ]] && continue
  [[ "${expected}" == \#* ]] && continue
  if [[ -z "${path:-}" ]]; then
    echo "malformed checksum line: ${expected}" >&2
    failed=$((failed + 1))
    continue
  fi
  total=$((total + 1))
  if [[ ! -f "${path}" ]]; then
    echo "MISSING  ${path}"
    missing=$((missing + 1))
    failed=$((failed + 1))
    continue
  fi
  actual="$(shasum -a 256 "${path}" | awk '{print $1}')"
  if [[ "${actual}" == "${expected}" ]]; then
    echo "OK       ${path}"
  else
    echo "FAIL     ${path}"
    echo "         expected ${expected}"
    echo "         actual   ${actual}"
    failed=$((failed + 1))
  fi
done < "${MANIFEST}"

echo
echo "Checked ${total} files; ${failed} failed; ${missing} missing."

if [[ "${failed}" -ne 0 ]]; then
  exit 1
fi
