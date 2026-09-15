#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MANIFEST="${ROOT}/docs/provenance/THIRD_PARTY_CHECKSUMS.txt"

usage() {
  cat <<EOF
Usage: $(basename "$0") URL OUTPUT_PATH

Fetches OUTPUT_PATH when missing, then verifies its SHA256 against
docs/provenance/THIRD_PARTY_CHECKSUMS.txt. OUTPUT_PATH may be absolute or
relative to the repository root, but the checksum manifest stores repo-relative
paths.
EOF
}

if [[ $# -ne 2 || "${1:-}" == "--help" ]]; then
  usage
  if [[ $# -eq 1 && "${1:-}" == "--help" ]]; then
    exit 0
  fi
  exit 2
fi

URL="$1"
OUTPUT="$2"

case "${OUTPUT}" in
  /*) ABS_OUTPUT="${OUTPUT}" ;;
  *) ABS_OUTPUT="${ROOT}/${OUTPUT}" ;;
esac

REL_OUTPUT="${ABS_OUTPUT#"${ROOT}/"}"

expected="$(awk -v path="${REL_OUTPUT}" '$1 !~ /^#/ && $2 == path { print $1; found=1 } END { if (!found) exit 1 }' "${MANIFEST}" 2>/dev/null || true)"
if [[ -z "${expected}" ]]; then
  echo "No checksum entry for ${REL_OUTPUT} in ${MANIFEST}" >&2
  exit 1
fi

compute_sha256() {
  local target="$1"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "${target}" | awk '{print $1}'
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "${target}" | awk '{print $1}'
  elif command -v openssl >/dev/null 2>&1; then
    openssl dgst -sha256 "${target}" | awk '{print $NF}'
  else
    echo "No SHA-256 tool available (sha256sum, shasum, or openssl required)" >&2
    return 1
  fi
}

if [[ -f "${ABS_OUTPUT}" ]]; then
  actual="$(compute_sha256 "${ABS_OUTPUT}")"
  if [[ "${actual}" == "${expected}" ]]; then
    echo "Verified ${REL_OUTPUT}"
    exit 0
  fi
fi

mkdir -p "$(dirname "${ABS_OUTPUT}")"

TMP_OUTPUT="$(mktemp "${ABS_OUTPUT}.tmp.XXXXXX" 2>/dev/null || echo "${ABS_OUTPUT}.tmp.$$")"
trap 'rm -f "${TMP_OUTPUT}"' EXIT INT TERM

curl --fail --location --retry 3 --retry-all-errors -o "${TMP_OUTPUT}" "${URL}"

actual="$(compute_sha256 "${TMP_OUTPUT}")"
if [[ "${actual}" != "${expected}" ]]; then
  echo "Checksum mismatch for ${REL_OUTPUT}" >&2
  echo "  expected ${expected}" >&2
  echo "  actual   ${actual}" >&2
  exit 1
fi

mv -f "${TMP_OUTPUT}" "${ABS_OUTPUT}"
echo "Verified ${REL_OUTPUT}"
