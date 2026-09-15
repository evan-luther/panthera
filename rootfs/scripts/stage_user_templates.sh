#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "Usage: $(basename "$0") MOUNTED_VOLUME USERTEMPLATE_SRC_DIR [MANIFEST ...]" >&2
  exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
mounted_volume="$1"
usertemplate_src_dir="$2"
shift 2
manifests=("$@")

# shellcheck source=/dev/null
source "${SCRIPT_DIR}/manifest_lib.sh"

usertemplate_rel="${usertemplate_src_dir#${PANTHERA_ROOT}/}"

if ! manifest_source_selected "${usertemplate_rel}" "${manifests[@]}"; then
  exit 0
fi

if [[ -d "${usertemplate_src_dir}/English.lproj" ]]; then
  mkdir -p "${mounted_volume}/System/Library/User Template/English.lproj"
  cp -R "${usertemplate_src_dir}/English.lproj/." \
    "${mounted_volume}/System/Library/User Template/English.lproj/"
fi

if [[ -d "${usertemplate_src_dir}/Non_localized" ]]; then
  mkdir -p "${mounted_volume}/System/Library/User Template/Non_localized"
  cp -R "${usertemplate_src_dir}/Non_localized/." \
    "${mounted_volume}/System/Library/User Template/Non_localized/"
fi

mkdir -p "${mounted_volume}/Users/panthera"
if [[ -d "${mounted_volume}/System/Library/User Template/English.lproj" ]]; then
  cp -R "${mounted_volume}/System/Library/User Template/English.lproj/." \
    "${mounted_volume}/Users/panthera/"
fi
if [[ -d "${mounted_volume}/System/Library/User Template/Non_localized" ]]; then
  cp -R "${mounted_volume}/System/Library/User Template/Non_localized/." \
    "${mounted_volume}/Users/panthera/"
fi

chmod 700 "${mounted_volume}/Users/panthera"
chmod 1777 "${mounted_volume}/Users/Shared"
if [[ -d "${mounted_volume}/Users/panthera/Public" ]]; then
  chmod 755 "${mounted_volume}/Users/panthera/Public"
fi
if [[ -d "${mounted_volume}/Users/panthera/Public/Drop Box" ]]; then
  chmod 733 "${mounted_volume}/Users/panthera/Public/Drop Box"
fi
