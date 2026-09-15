#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "Usage: $(basename "$0") MOUNTED_VOLUME LAUNCHDAEMONS_DIR [MANIFEST ...]" >&2
  exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mounted_volume="$1"
launch_daemons_dir="$2"
shift 2
manifests=("$@")

# shellcheck source=/dev/null
source "${SCRIPT_DIR}/manifest_lib.sh"

launch_daemon_requested() {
  local plist_name="$1"
  local csv="$2"
  local entry

  IFS=',' read -r -a entries <<< "${csv}"
  for entry in "${entries[@]}"; do
    entry="${entry## }"
    entry="${entry%% }"
    [[ -z "${entry}" ]] && continue
    if [[ "${entry}" == "${plist_name}" ]]; then
      return 0
    fi
  done
  return 1
}

plist_add_env() {
  local plist_path="$1"
  local key="$2"
  local value="$3"

  [[ -n "${value}" ]] || return 0
  /usr/libexec/PlistBuddy -c "Add :EnvironmentVariables dict" "${plist_path}" >/dev/null 2>&1 || true
  /usr/libexec/PlistBuddy -c "Delete :EnvironmentVariables:${key}" "${plist_path}" >/dev/null 2>&1 || true
  /usr/libexec/PlistBuddy -c "Add :EnvironmentVariables:${key} string ${value}" "${plist_path}"
}

if [[ ! -d "${launch_daemons_dir}" ]]; then
  exit 0
fi

declare -A manifest_launch_daemons=()
manifest_selection_enabled=0

if [[ "${#manifests[@]}" -gt 0 ]]; then
  manifest_selection_enabled=1
  for manifest in "${manifests[@]}"; do
    manifest_destinations="$(manifest_selected_destinations "${manifest}")"
    while IFS= read -r destination; do
      case "${destination}" in
        /System/Library/LaunchDaemons/*.plist)
          manifest_launch_daemons["${destination##*/}"]=1
          ;;
      esac
    done <<< "${manifest_destinations}"
  done
fi

shopt -s nullglob
for plist_file in "${launch_daemons_dir}"/*.plist; do
  plist_name="$(basename "${plist_file}")"
  launch_daemon_selected=0

  if [[ "${manifest_selection_enabled}" == "1" ]]; then
    if [[ -n "${manifest_launch_daemons[${plist_name}]+x}" ]]; then
      launch_daemon_selected=1
    fi
  else
    launch_daemon_selected=1
  fi

  if [[ "${PANTHERA_LOGIN_ONLY_RECOVERY:-0}" == "1" && "${plist_name}" != "com.panthera.login.plist" ]]; then
    launch_daemon_selected=0
  fi

  if [[ "${launch_daemon_selected}" == "0" \
     && -n "${PANTHERA_EXTRA_LAUNCHDAEMONS:-}" ]] \
     && launch_daemon_requested "${plist_name}" "${PANTHERA_EXTRA_LAUNCHDAEMONS}"; then
    launch_daemon_selected=1
  fi

  if [[ "${launch_daemon_selected}" == "1" \
     && -n "${PANTHERA_SKIP_LAUNCHDAEMONS:-}" ]] \
     && launch_daemon_requested "${plist_name}" "${PANTHERA_SKIP_LAUNCHDAEMONS}"; then
    launch_daemon_selected=0
  fi

  if [[ "${launch_daemon_selected}" == "0" ]]; then
    continue
  fi

  if [[ "${plist_name}" == "com.apple.IPConfiguration.plist" \
     && "${PANTHERA_CONFIGD_IPCONFIGURATION:-0}" == "1" ]]; then
    continue
  fi

  if [[ "${manifest_selection_enabled}" == "0" ]]; then
    if [[ "${plist_name}" == "com.panthera.test.xpc.plist" && "${PANTHERA_STAGE_TEST_XPC:-1}" != "1" ]]; then
      continue
    fi
    if [[ "${plist_name}" == "com.apple.notifyd.plist" && "${PANTHERA_STAGE_NOTIFYD:-1}" != "1" ]]; then
      continue
    fi
    if [[ "${plist_name}" == "com.apple.configd.plist" && "${PANTHERA_STAGE_CONFIGD:-0}" != "1" ]]; then
      continue
    fi
    if [[ "${plist_name}" == "com.apple.SystemConfiguration.KernelEventMonitor.plist" && "${PANTHERA_STAGE_KERNEL_EVENT_MONITOR:-0}" != "1" ]]; then
      continue
    fi
    if [[ "${plist_name}" == "com.apple.IPConfiguration.plist" && "${PANTHERA_STAGE_IPCONFIGURATION:-0}" != "1" ]]; then
      continue
    fi
    if [[ "${plist_name}" == "com.panthera.sc-dynamic-store-probe.plist" && "${PANTHERA_STAGE_SC_PROBE:-0}" != "1" ]]; then
      continue
    fi
    if [[ "${plist_name}" == "com.panthera.sc-network-state.plist" && "${PANTHERA_STAGE_SC_NETWORK_STATE:-0}" != "1" ]]; then
      continue
    fi
    if [[ "${plist_name}" == "com.panthera.sc-network-state-probe.plist" && "${PANTHERA_STAGE_SC_NETWORK_PROBE:-0}" != "1" ]]; then
      continue
    fi
  fi
  cp "${plist_file}" "${mounted_volume}/System/Library/LaunchDaemons/"
  staged_plist="${mounted_volume}/System/Library/LaunchDaemons/${plist_name}"
  case "${plist_name}" in
    com.apple.configd.plist)
      plist_add_env "${staged_plist}" PANTHERA_CONFIGD_TRACE "${PANTHERA_CONFIGD_TRACE:-}"
      plist_add_env "${staged_plist}" PANTHERA_CONFIGD_ROUTE_TRACE "${PANTHERA_CONFIGD_ROUTE_TRACE:-}"
      plist_add_env "${staged_plist}" PANTHERA_CONFIGD_KERNEL_EVENT_MONITOR_TRACE "${PANTHERA_CONFIGD_KERNEL_EVENT_MONITOR_TRACE:-}"
      plist_add_env "${staged_plist}" PANTHERA_SYSTEMCONFIGURATION_TRACE "${PANTHERA_SYSTEMCONFIGURATION_TRACE:-}"
      plist_add_env "${staged_plist}" PANTHERA_IOKIT_TRACE "${PANTHERA_IOKIT_TRACE:-}"
      plist_add_env "${staged_plist}" PANTHERA_IPCONFIGURATION_TRACE "${PANTHERA_IPCONFIGURATION_TRACE:-}"
      ;;
    com.panthera.bootverify.plist)
      plist_add_env "${staged_plist}" PANTHERA_BOOT_VERIFY_RUN_DAEMON_NETWORK "${PANTHERA_BOOT_VERIFY_RUN_DAEMON_NETWORK:-}"
      ;;
    com.apple.IPConfiguration.plist)
      plist_add_env "${staged_plist}" PANTHERA_IPCONFIGURATION_TRACE "${PANTHERA_IPCONFIGURATION_TRACE:-}"
      ;;
    com.apple.SystemConfiguration.KernelEventMonitor.plist)
      plist_add_env "${staged_plist}" PANTHERA_CONFIGD_KERNEL_EVENT_MONITOR_TRACE "${PANTHERA_CONFIGD_KERNEL_EVENT_MONITOR_TRACE:-}"
      plist_add_env "${staged_plist}" PANTHERA_SYSTEMCONFIGURATION_TRACE "${PANTHERA_SYSTEMCONFIGURATION_TRACE:-}"
      plist_add_env "${staged_plist}" PANTHERA_IOKIT_TRACE "${PANTHERA_IOKIT_TRACE:-}"
      ;;
  esac
  if [[ "${plist_name}" == "com.apple.configd.plist" \
     && "${PANTHERA_CONFIGD_IPCONFIGURATION:-0}" == "1" ]]; then
    /usr/libexec/PlistBuddy -c "Add :EnvironmentVariables dict" "${staged_plist}" >/dev/null 2>&1 || true
    /usr/libexec/PlistBuddy -c "Delete :EnvironmentVariables:PANTHERA_CONFIGD_IPCONFIGURATION" "${staged_plist}" >/dev/null 2>&1 || true
    /usr/libexec/PlistBuddy -c "Add :EnvironmentVariables:PANTHERA_CONFIGD_IPCONFIGURATION string 1" "${staged_plist}"
    /usr/libexec/PlistBuddy -c "Add :MachServices dict" "${staged_plist}" >/dev/null 2>&1 || true
    /usr/libexec/PlistBuddy -c "Delete :MachServices:com.apple.network.IPConfiguration" "${staged_plist}" >/dev/null 2>&1 || true
    /usr/libexec/PlistBuddy -c "Add :MachServices:com.apple.network.IPConfiguration bool true" "${staged_plist}"
  fi
done
shopt -u nullglob
