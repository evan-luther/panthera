#!/usr/bin/env bash
set -euo pipefail

HOST_ZFS_PATHS=(
  /usr/local/zfs/bin
  /opt/openzfs/bin
  /opt/homebrew/bin
  /opt/homebrew/sbin
  /usr/local/bin
  /usr/local/sbin
  /opt/local/bin
  /opt/local/sbin
)

for zfs_path in "${HOST_ZFS_PATHS[@]}"; do
  if [[ -d "${zfs_path}" ]]; then
    PATH="${zfs_path}:${PATH}"
  fi
done
export PATH

fail=0

echo "# Panthera host OpenZFS audit"
echo

for tool in zpool zfs; do
  if command -v "${tool}" >/dev/null 2>&1; then
    echo "- PASS ${tool}: $(command -v "${tool}")"
  else
    echo "- FAIL ${tool}: not found"
    fail=1
  fi
done

if [[ "${fail}" == "0" ]]; then
  if zpool_version="$(zpool version 2>&1)"; then
    echo "- PASS zpool version"
    printf '%s\n' "${zpool_version}" | sed 's/^/  /'
  else
    echo "- FAIL zpool version"
    printf '%s\n' "${zpool_version}" | sed 's/^/  /'
    fail=1
  fi

  if zfs_version="$(zfs version 2>&1)"; then
    echo "- PASS zfs version"
    printf '%s\n' "${zfs_version}" | sed 's/^/  /'
  else
    echo "- FAIL zfs version"
    printf '%s\n' "${zfs_version}" | sed 's/^/  /'
    fail=1
  fi
fi

if kextstat 2>/dev/null | grep -Eq 'org\.openzfsonosx\.zfs'; then
  echo "- PASS org.openzfsonosx.zfs kext is loaded"
elif kmutil showloaded 2>/dev/null | grep -Eq 'org\.openzfsonosx\.zfs'; then
  echo "- PASS org.openzfsonosx.zfs kext is loaded"
else
  echo "- FAIL org.openzfsonosx.zfs kext is not loaded"
  fail=1
fi

echo
if [[ "${fail}" == "0" ]]; then
  echo "Result: PASS"
else
  echo "Result: FAIL"
fi

exit "${fail}"
