#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "Usage: $(basename "$0") MOUNTED_VOLUME SHARED_CACHE [MANIFEST ...]" >&2
  exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
mounted_volume="$1"
shared_cache="$2"
shift 2
manifests=("$@")
guest_cache_path="${mounted_volume}/System/Library/dyld/dyld_shared_cache_x86_64"
shared_cache_rel="${shared_cache#${PANTHERA_ROOT}/}"

# shellcheck source=/dev/null
source "${SCRIPT_DIR}/manifest_lib.sh"

if manifest_source_selected "${shared_cache_rel}" "${manifests[@]}" \
   && [[ -f "${shared_cache}" && "${PANTHERA_NO_SHARED_CACHE:-0}" != "1" ]]; then
  cp "${shared_cache}" "${guest_cache_path}"
  printf '%s\n' "${guest_cache_path}"
fi
