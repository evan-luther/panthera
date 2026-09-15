#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# shellcheck source=/dev/null
source "${PANTHERA_ROOT}/rootfs/scripts/manifest_lib.sh"

fail=0

echo "# Panthera ZFS staging audit"
echo

default_zfs_destinations="$(
  env -u PANTHERA_STAGE_ZFS_MOUNT_SMOKE \
      -u PANTHERA_STAGE_ZFS_HYBRID \
      bash -c '
        source "$1"
        manifest_selected_destinations "$2"
      ' _ \
      "${PANTHERA_ROOT}/rootfs/scripts/manifest_lib.sh" \
      "${PANTHERA_ROOT}/manifests/zfs.system"
)"

if printf '%s\n' "${default_zfs_destinations}" \
  | grep -Eq '/usr/libexec/zfs_mount_smoke.sh|com\.panthera\.zfs-mount-smoke\.plist'; then
  echo "- FAIL historical launchd ZFS mount smoke is selected by default"
  printf '%s\n' "${default_zfs_destinations}" \
    | grep -E '/usr/libexec/zfs_mount_smoke.sh|com\.panthera\.zfs-mount-smoke\.plist' \
    | sed 's/^/  /'
  fail=1
else
  echo "- PASS historical launchd ZFS mount smoke is opt-in only"
fi

if printf '%s\n' "${default_zfs_destinations}" \
  | grep -Eq '/etc/panthera/zfs-hybrid.conf|/System/Library/PantheraSeed/'; then
  echo "- FAIL hybrid ZFS state config/seed is selected without PANTHERA_STAGE_ZFS_HYBRID=1"
  printf '%s\n' "${default_zfs_destinations}" \
    | grep -E '/etc/panthera/zfs-hybrid.conf|/System/Library/PantheraSeed/' \
    | sed 's/^/  /'
  fail=1
else
  echo "- PASS hybrid ZFS state config/seed is opt-in only"
fi

hybrid_destinations="$(
  env -u PANTHERA_STAGE_ZFS_MOUNT_SMOKE \
      PANTHERA_STAGE_ZFS_HYBRID=1 \
      bash -c '
        source "$1"
        manifest_selected_destinations "$2"
      ' _ \
      "${PANTHERA_ROOT}/rootfs/scripts/manifest_lib.sh" \
      "${PANTHERA_ROOT}/manifests/zfs.system"
)"

for required in \
  /sbin/panthera_zfs_hybrid_mount \
  /etc/panthera/zfs-hybrid.conf \
  /System/Library/PantheraSeed/Library/Preferences/SystemConfiguration/preferences.plist; do
  if printf '%s\n' "${hybrid_destinations}" | grep -Fxq "${required}"; then
    echo "- PASS hybrid destination selected: ${required}"
  else
    echo "- FAIL hybrid destination missing: ${required}"
    fail=1
  fi
done

echo
if [[ "${fail}" == "0" ]]; then
  echo "Result: PASS"
else
  echo "Result: FAIL"
fi

exit "${fail}"
