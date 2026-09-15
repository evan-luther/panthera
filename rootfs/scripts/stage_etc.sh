#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 3 ]]; then
  echo "Usage: $(basename "$0") MOUNTED_VOLUME ROOTFS_ETC_DIR SYSTEM_VERSION_PLIST [MANIFEST ...]" >&2
  exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
mounted_volume="$1"
rootfs_etc_dir="$2"
system_version_plist="$3"
shift 3
manifests=("$@")

# shellcheck source=/dev/null
source "${SCRIPT_DIR}/manifest_lib.sh"

selected() {
  local source_path="$1"
  manifest_source_selected "${source_path}" "${manifests[@]}"
}

if selected "rootfs/etc"; then
  for etc_file in hosts hostname motd panthera-release resolv.conf shells termcap zprofile zshrc; do
    if [[ -f "${rootfs_etc_dir}/${etc_file}" ]]; then
      cp "${rootfs_etc_dir}/${etc_file}" "${mounted_volume}/etc/${etc_file}"
    fi
  done
fi

system_version_rel="${system_version_plist#${PANTHERA_ROOT}/}"
if selected "${system_version_rel}" && [[ -f "${system_version_plist}" ]]; then
  cp "${system_version_plist}" "${mounted_volume}/System/Library/CoreServices/SystemVersion.plist"
fi
