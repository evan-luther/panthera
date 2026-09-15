#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Usage: $(basename "$0") MOUNTED_VOLUME [MANIFEST ...]" >&2
  exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
mounted_volume="$1"
shift
manifests=("$@")
missing=0

# shellcheck source=/dev/null
source "${SCRIPT_DIR}/manifest_lib.sh"

require_path() {
  local rel_path="$1"
  local abs_path="${mounted_volume}${rel_path}"

  if [[ ! -e "${abs_path}" && ! -L "${abs_path}" ]]; then
    echo "required rootfs path missing: ${rel_path}" >&2
    missing=$((missing + 1))
  fi
}

require_executable() {
  local rel_path="$1"
  local abs_path="${mounted_volume}${rel_path}"

  if [[ ! -x "${abs_path}" ]]; then
    echo "required rootfs executable missing or not executable: ${rel_path}" >&2
    missing=$((missing + 1))
  fi
}

require_executable /sbin/launchd
require_executable /bin/zsh
require_path /bin/sh
require_executable /usr/lib/dyld
require_path /usr/lib/libSystem.B.dylib
require_path /usr/lib/system/libsystem_kernel.dylib
require_path /System/Library/LaunchDaemons/com.panthera.login.plist
if [[ ",${PANTHERA_SKIP_LAUNCHDAEMONS:-}," != *",com.panthera.netbringup.plist,"* ]]; then
  require_path /System/Library/LaunchDaemons/com.panthera.netbringup.plist
fi
require_executable /sbin/netbringup
require_path /etc/passwd
require_path /etc/group
require_path /etc/master.passwd

if [[ "${PANTHERA_NO_SHARED_CACHE:-0}" != "1" ]]; then
  require_path /System/Library/dyld/dyld_shared_cache_x86_64
fi

trim_spaces() {
  local value="$1"
  value="${value#"${value%%[![:space:]]*}"}"
  value="${value%"${value##*[![:space:]]}"}"
  printf '%s' "${value}"
}

verify_manifest_destinations() {
  local manifest kind required source_path destination producer verification condition note extra
  local destinations dest

  for manifest in "${manifests[@]}"; do
    if [[ ! -f "${manifest}" ]]; then
      echo "Missing manifest: ${manifest}" >&2
      missing=$((missing + 1))
      continue
    fi

    while IFS='|' read -r kind required source_path destination producer verification condition note extra; do
      [[ -n "${kind}" ]] || continue
      [[ "${kind}" == \#* ]] && continue

      if [[ -n "${extra:-}" ]]; then
        echo "Malformed manifest entry in ${manifest}: too many fields: ${source_path}" >&2
        missing=$((missing + 1))
        continue
      fi

      if ! manifest_condition_enabled "${condition:-always}"; then
        continue
      fi

      case "${kind}" in
        file|dir|glob|generated|derived)
          ;;
        *)
          echo "Unsupported manifest kind: ${kind}" >&2
          missing=$((missing + 1))
          continue
          ;;
      esac

      case "${required}" in
        required)
          ;;
        optional)
          if ! manifest_source_exists "${PANTHERA_ROOT}" "${kind}" "${source_path}"; then
            continue
          fi
          ;;
        *)
          echo "Unsupported manifest required field: ${required}" >&2
          missing=$((missing + 1))
          continue
          ;;
      esac

      IFS=',' read -ra destinations <<< "${destination}"
      for dest in "${destinations[@]}"; do
        dest="$(trim_spaces "${dest}")"
        [[ -n "${dest}" ]] || continue
        [[ "${dest}" == "host tool only" ]] && continue
        if [[ "${dest}" == /System/Library/LaunchDaemons/*.plist ]]; then
          local plist_name="${dest##*/}"
          if [[ ",${PANTHERA_SKIP_LAUNCHDAEMONS:-}," == *",${plist_name},"* ]]; then
            continue
          fi
        fi
        require_path "${dest}"
      done
    done < "${manifest}"
  done
}

if [[ "${#manifests[@]}" -gt 0 ]]; then
  verify_manifest_destinations
fi

if [[ "${missing}" -gt 0 ]]; then
  echo "Rootfs verification failed: ${missing} required path(s) missing" >&2
  exit 1
fi

echo "Rootfs verification passed for ${mounted_volume}"
