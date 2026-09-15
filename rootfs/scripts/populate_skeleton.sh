#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $(basename "$0") MOUNTED_VOLUME" >&2
  exit 2
fi

mounted_volume="$1"

mkdir -p \
  "${mounted_volume}/bin" \
  "${mounted_volume}/dev" \
  "${mounted_volume}/etc" \
  "${mounted_volume}/private/tmp" \
  "${mounted_volume}/private/var" \
  "${mounted_volume}/Library/Preferences/SystemConfiguration" \
  "${mounted_volume}/sbin" \
  "${mounted_volume}/System/Library/CoreServices" \
  "${mounted_volume}/System/Library/LaunchDaemons" \
  "${mounted_volume}/System/Library/SystemConfiguration" \
  "${mounted_volume}/System/Library/dyld" \
  "${mounted_volume}/System/Library" \
  "${mounted_volume}/tmp" \
  "${mounted_volume}/var/log" \
  "${mounted_volume}/var/empty" \
  "${mounted_volume}/var/root" \
  "${mounted_volume}/Users" \
  "${mounted_volume}/Users/Shared" \
  "${mounted_volume}/usr/bin" \
  "${mounted_volume}/usr/lib/system" \
  "${mounted_volume}/usr/lib/ossl-modules" \
  "${mounted_volume}/usr/sbin" \
  "${mounted_volume}/usr/libexec" \
  "${mounted_volume}/usr/share/nano" \
  "${mounted_volume}/etc/ssh" \
  "${mounted_volume}/var/run" \
  "${mounted_volume}/System/Library/User Template"

if [[ ! -e "${mounted_volume}/etc/fstab" ]]; then
  : > "${mounted_volume}/etc/fstab"
fi
