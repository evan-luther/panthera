#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# shellcheck source=/dev/null
source "${SCRIPT_DIR}/manifest_lib.sh"

usage() {
  cat <<EOF
Usage: $(basename "$0") MANIFEST [...]

Checks manifest-declared rootfs inputs before image creation.
EOF
}

if [[ $# -eq 0 ]]; then
  usage >&2
  exit 2
fi


missing_required=0
required_count=0
optional_count=0
present_optional_count=0

echo ">>> Rootfs input manifests"

for manifest in "$@"; do
  if [[ ! -f "${manifest}" ]]; then
    echo "Missing manifest: ${manifest}" >&2
    missing_required=$((missing_required + 1))
    continue
  fi

  echo "  manifest: ${manifest#${PANTHERA_ROOT}/}"

  while IFS='|' read -r kind required source_path destination producer verification condition note extra; do
    [[ -n "${kind}" ]] || continue
    [[ "${kind}" == \#* ]] && continue

    if [[ -n "${extra:-}" ]]; then
      echo "Malformed manifest entry in ${manifest}: too many fields: ${source_path}" >&2
      missing_required=$((missing_required + 1))
      continue
    fi

    if ! manifest_condition_enabled "${condition:-always}"; then
      continue
    fi

    case "${required}" in
      required)
        required_count=$((required_count + 1))
        if manifest_source_exists "${PANTHERA_ROOT}" "${kind}" "${source_path}"; then
          printf '    required ok: %s -> %s\n' "${source_path}" "${destination}"
        else
          printf '    required missing: %s -> %s\n' "${source_path}" "${destination}" >&2
          printf '      producer: %s\n' "${producer}" >&2
          printf '      verification: %s\n' "${verification}" >&2
          missing_required=$((missing_required + 1))
        fi
        ;;
      optional)
        optional_count=$((optional_count + 1))
        if manifest_source_exists "${PANTHERA_ROOT}" "${kind}" "${source_path}"; then
          present_optional_count=$((present_optional_count + 1))
          printf '    optional present: %s -> %s\n' "${source_path}" "${destination}"
        fi
        ;;
      *)
        echo "Unsupported manifest required field: ${required}" >&2
        missing_required=$((missing_required + 1))
        ;;
    esac
  done < "${manifest}"
done

printf '>>> Rootfs input summary: %d required, %d optional present, %d optional declared\n' \
  "${required_count}" "${present_optional_count}" "${optional_count}"

if [[ "${missing_required}" -gt 0 ]]; then
  echo "Rootfs input verification failed: ${missing_required} required input(s) missing" >&2
  exit 1
fi

echo ">>> Rootfs input verification passed"
