#!/usr/bin/env bash
# tools/runtime_smoke.sh — Fast libSystem runtime diagnostics and staging tool
#
# Provides targeted component rebuilds, real shared cache generation, HFS
# rootfs staging, and focused early-boot verification without running full world
# or release gate suites.
#
# Usage:
#   tools/runtime_smoke.sh [options]
#
# Options:
#   --rebuild COMPONENT    Rebuild constituent runtime component:
#                          pthread, dispatch, libsystem-c, extra,
#                          systemconfiguration, ipconfiguration, configd
#                          (may be repeated; default: none)
#   --probe PROBE          Boot verification probe: dispatch-timer, configd
#                          (default: dispatch-timer)
#   --tag NAME             Artifact tag name (default: runtime-smoke-<timestamp>)
#   --artifacts DIR        Artifact directory (default: artifacts/runtime_smoke)
#   --timeout SECONDS      Boot verification timeout (default: 180)
#   --stage-only           Recreate/stage HFS root image without booting
#   --help                 Show this help message

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

TAG="runtime-smoke-$(date +%Y%m%d-%H%M%S)"
ARTIFACT_DIR="${PANTHERA_ROOT}/artifacts/runtime_smoke"
TIMEOUT_SECONDS=180
PROBE="dispatch-timer"
STAGE_ONLY=0

REBUILD_PTHREAD=0
REBUILD_DISPATCH=0
REBUILD_LIBSYSTEM_C=0
REBUILD_EXTRA=0
REBUILD_SYSTEMCONFIGURATION=0
REBUILD_IPCONFIGURATION=0
REBUILD_CONFIGD=0
ANY_REBUILD=0

usage() {
  cat <<'EOF'
Usage: tools/runtime_smoke.sh [options]

Fast libSystem runtime diagnostics and staging tool.

Options:
  --rebuild COMPONENT    Rebuild constituent component: pthread, dispatch,
                         libsystem-c, extra, systemconfiguration,
                         ipconfiguration, configd
                         (repeatable; default: none)
  --probe PROBE          Boot verification probe: dispatch-timer, configd
                         (default: dispatch-timer)
  --tag NAME             Artifact tag name (default: runtime-smoke-<timestamp>)
  --artifacts DIR        Artifact directory (default: artifacts/runtime_smoke)
  --timeout SECONDS      Boot verification timeout (default: 180)
  --stage-only           Recreate/stage HFS root image without booting QEMU
  --help                 Show this help message
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --rebuild)
      if [[ $# -lt 2 ]]; then
        echo "Error: --rebuild requires a component name (pthread, dispatch, libsystem-c, extra, systemconfiguration, ipconfiguration, configd)" >&2
        exit 2
      fi
      case "$2" in
        pthread)
          REBUILD_PTHREAD=1
          ANY_REBUILD=1
          ;;
        dispatch)
          REBUILD_DISPATCH=1
          ANY_REBUILD=1
          ;;
        libsystem-c|libsystem_c)
          REBUILD_LIBSYSTEM_C=1
          ANY_REBUILD=1
          ;;
        extra)
          REBUILD_EXTRA=1
          ANY_REBUILD=1
          ;;
        systemconfiguration|system-configuration|system_configuration|SystemConfiguration)
          REBUILD_SYSTEMCONFIGURATION=1
          ANY_REBUILD=1
          ;;
        ipconfiguration|ip-configuration|ip_configuration|IPConfiguration)
          REBUILD_IPCONFIGURATION=1
          ANY_REBUILD=1
          ;;
        configd)
          REBUILD_CONFIGD=1
          ANY_REBUILD=1
          ;;
        *)
          echo "Error: Unknown rebuild component: $2" >&2
          echo "Supported components: pthread, dispatch, libsystem-c, extra, systemconfiguration, ipconfiguration, configd" >&2
          exit 2
          ;;
      esac
      shift 2
      ;;
    --probe)
      if [[ $# -lt 2 ]]; then
        echo "Error: --probe requires a probe name (dispatch-timer, configd)" >&2
        exit 2
      fi
      case "$2" in
        dispatch-timer)
          PROBE="dispatch-timer"
          ;;
        configd)
          PROBE="configd"
          ;;
        *)
          echo "Error: Unknown probe: $2" >&2
          echo "Supported probes: dispatch-timer, configd" >&2
          exit 2
          ;;
      esac
      shift 2
      ;;
    --tag)
      if [[ $# -lt 2 ]]; then
        echo "Error: --tag requires a name" >&2
        exit 2
      fi
      TAG="$2"
      shift 2
      ;;
    --artifacts)
      if [[ $# -lt 2 ]]; then
        echo "Error: --artifacts requires a directory path" >&2
        exit 2
      fi
      ARTIFACT_DIR="$2"
      shift 2
      ;;
    --timeout)
      if [[ $# -lt 2 ]]; then
        echo "Error: --timeout requires seconds" >&2
        exit 2
      fi
      if ! [[ "$2" =~ ^[1-9][0-9]*$ ]]; then
        echo "Error: --timeout must be a positive integer" >&2
        exit 2
      fi
      TIMEOUT_SECONDS="$2"
      shift 2
      ;;
    --stage-only)
      STAGE_ONLY=1
      shift
      ;;
    --help)
      usage
      exit 0
      ;;
    *)
      echo "Error: Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

mkdir -p "${ARTIFACT_DIR}"
cd "${PANTHERA_ROOT}"

SUMMARY_PATH="${ARTIFACT_DIR}/${TAG}.summary.md"
BOOT_VERIFY_PLIST_PATH="${PANTHERA_ROOT}/rootfs/System/Library/LaunchDaemons/com.panthera.bootverify.plist"
ROOT_DISK="${PANTHERA_ROOT}/images/qemu/panthera-root.img"

RESTORE_BOOTVERIFY_PLIST=0
ORIGINAL_BOOTVERIFY_PLIST=""

cleanup() {
  if [[ "${RESTORE_BOOTVERIFY_PLIST}" == "1" && -n "${ORIGINAL_BOOTVERIFY_PLIST}" && -f "${ORIGINAL_BOOTVERIFY_PLIST}" ]]; then
    cp -p "${ORIGINAL_BOOTVERIFY_PLIST}" "${BOOT_VERIFY_PLIST_PATH}"
    rm -f "${ORIGINAL_BOOTVERIFY_PLIST}"
  fi
}
trap cleanup EXIT

EXECUTED_STEPS=()
ARTIFACT_PATHS=()

run_step() {
  local step_name="$1"
  local log_file="${ARTIFACT_DIR}/${TAG}.${step_name}.log"
  shift

  echo ">>> [${step_name}] $*"
  if "$@" >"${log_file}" 2>&1; then
    EXECUTED_STEPS+=("${step_name}: PASS")
    ARTIFACT_PATHS+=("${log_file}")
  else
    local status=$?
    EXECUTED_STEPS+=("${step_name}: FAIL (exit ${status})")
    ARTIFACT_PATHS+=("${log_file}")
    echo "FAIL: step '${step_name}' failed with exit ${status}. Log: ${log_file}" >&2
    exit "${status}"
  fi
}

# 1. Run selected component builders
if [[ "${REBUILD_EXTRA}" == "1" ]]; then
  run_step rebuild_extra bash "${PANTHERA_ROOT}/userland/libsystem/build/relink_libpanthera_extra.sh"
fi

if [[ "${REBUILD_PTHREAD}" == "1" ]]; then
  run_step rebuild_pthread bash "${PANTHERA_ROOT}/userland/libsystem/build/build_libsystem_pthread.sh"
fi

if [[ "${REBUILD_DISPATCH}" == "1" ]]; then
  run_step rebuild_dispatch bash "${PANTHERA_ROOT}/userland/libsystem/build/build_libdispatch.sh"
fi

if [[ "${REBUILD_LIBSYSTEM_C}" == "1" ]]; then
  run_step rebuild_libsystem_c bash "${PANTHERA_ROOT}/userland/libsystem/build/build_libsystem_c.sh"
fi

# 2. Relink libSystem.B.dylib when components were rebuilt or if missing
SYSROOT_LIBSYSTEM="${PANTHERA_ROOT}/userland/libsystem/build/sysroot/usr/lib/libSystem.B.dylib"
if [[ "${ANY_REBUILD}" == "1" || ! -f "${SYSROOT_LIBSYSTEM}" ]]; then
  run_step relink_libSystem bash "${PANTHERA_ROOT}/userland/libsystem/build/relink_libSystem.sh"
fi

if [[ "${REBUILD_SYSTEMCONFIGURATION}" == "1" ]]; then
  run_step rebuild_systemconfiguration bash "${PANTHERA_ROOT}/userland/configd/build_systemconfiguration.sh"
fi
if [[ "${REBUILD_IPCONFIGURATION}" == "1" ]]; then
  run_step rebuild_ipconfiguration bash "${PANTHERA_ROOT}/userland/bootp/build_ipconfiguration.sh"
fi

if [[ "${REBUILD_CONFIGD}" == "1" ]]; then
  run_step rebuild_configd bash "${PANTHERA_ROOT}/userland/configd/build_configd.sh"
fi

# 3. Always rebuild the real dyld shared cache
run_step build_shared_cache bash "${PANTHERA_ROOT}/tools/build_shared_cache.sh"

# 4. Recreate/stage HFS image or run targeted boot verification
DISPATCH_TIMER_SKIP_LAUNCHDAEMONS="com.apple.configd.plist,com.apple.IPConfiguration.plist,com.apple.SystemConfiguration.KernelEventMonitor.plist,com.panthera.sc-dynamic-store-probe.plist,com.panthera.sc-network-state.plist,com.panthera.sc-network-state-probe.plist,com.panthera.netbringup.plist,com.panthera.poll-listen-probe.plist,com.panthera.poll-hostfwd-probe.plist,com.panthera.ppoll-hostfwd-probe.plist,com.panthera.ppoll-empty-hostfwd-probe.plist,com.panthera.sshd.plist,com.apple.mDNSResponder.plist"
CONFIGD_SKIP_LAUNCHDAEMONS="com.panthera.netbringup.plist,com.apple.IPConfiguration.plist,com.panthera.sshd.plist,com.apple.mDNSResponder.plist,com.panthera.poll-listen-probe.plist,com.panthera.poll-hostfwd-probe.plist,com.panthera.ppoll-hostfwd-probe.plist,com.panthera.ppoll-empty-hostfwd-probe.plist"

if [[ "${STAGE_ONLY}" == "1" ]]; then
  run_step build_boot_verify_guest bash "${PANTHERA_ROOT}/tools/build_boot_verify_guest.sh"

  if [[ "${PROBE}" == "dispatch-timer" ]]; then
    run_step build_dispatch_timer_probe bash "${PANTHERA_ROOT}/tools/build_dispatch_timer_probe.sh"

    ORIGINAL_BOOTVERIFY_PLIST="$(mktemp "${ARTIFACT_DIR}/${TAG}.bootverify.plist.XXXXXX")"
    cp -p "${BOOT_VERIFY_PLIST_PATH}" "${ORIGINAL_BOOTVERIFY_PLIST}"
    RESTORE_BOOTVERIFY_PLIST=1
    /usr/libexec/PlistBuddy -c "Add :EnvironmentVariables dict" "${BOOT_VERIFY_PLIST_PATH}" >/dev/null 2>&1 || true
    /usr/libexec/PlistBuddy -c "Delete :EnvironmentVariables:PANTHERA_BOOT_VERIFY_RUN_DISPATCH_TIMER" "${BOOT_VERIFY_PLIST_PATH}" >/dev/null 2>&1 || true
    /usr/libexec/PlistBuddy -c "Add :EnvironmentVariables:PANTHERA_BOOT_VERIFY_RUN_DISPATCH_TIMER string 1" "${BOOT_VERIFY_PLIST_PATH}"

    run_step stage_hfs env PANTHERA_ROOT_SHELL=/bin/mini_sh \
      PANTHERA_DEFAULT_NETWORK_OWNER=netbringup \
      PANTHERA_SKIP_LAUNCHDAEMONS="${DISPATCH_TIMER_SKIP_LAUNCHDAEMONS}" \
      bash "${PANTHERA_ROOT}/rootfs/create_hfs_root_image.sh" --force --no-build-components
  elif [[ "${PROBE}" == "configd" ]]; then
    run_step stage_hfs env PANTHERA_ROOT_SHELL=/bin/mini_sh \
      PANTHERA_STAGE_CONFIGD=1 \
      PANTHERA_STAGE_SC_PROBE=1 \
      PANTHERA_STAGE_IPCONFIGURATION=1 \
      PANTHERA_CONFIGD_IPCONFIGURATION=1 \
      PANTHERA_CONFIGD_TRACE=1 \
      PANTHERA_CONFIGD_ROUTE_TRACE=1 \
      PANTHERA_CONFIGD_KERNEL_EVENT_MONITOR_TRACE=1 \
      PANTHERA_SYSTEMCONFIGURATION_TRACE=1 \
      PANTHERA_IOKIT_TRACE=1 \
      PANTHERA_SKIP_LAUNCHDAEMONS="${CONFIGD_SKIP_LAUNCHDAEMONS}" \
      bash "${PANTHERA_ROOT}/rootfs/create_hfs_root_image.sh" --force --no-build-components
  fi
else
  # Non-stage-only: run boot_verify.sh with targeted probe
  boot_verify_args=(
    --rebuild-rootfs
    --root-kind hfs
    --root-disk "${ROOT_DISK}"
    --timeout "${TIMEOUT_SECONDS}"
    --tag "${TAG}-boot"
  )

  if [[ "${PROBE}" == "dispatch-timer" ]]; then
    boot_verify_args+=(--strict-dispatch-timer)
    run_step boot_verify env PANTHERA_DEFAULT_NETWORK_OWNER=netbringup \
      PANTHERA_SKIP_LAUNCHDAEMONS="${DISPATCH_TIMER_SKIP_LAUNCHDAEMONS}" \
      bash "${PANTHERA_ROOT}/tools/boot_verify.sh" "${boot_verify_args[@]}"
  elif [[ "${PROBE}" == "configd" ]]; then
    boot_verify_args+=(
      --strict-configd
      --strict-ipconfiguration
      --configd-ipconfiguration-plugin
    )
    run_step boot_verify env \
      PANTHERA_SKIP_LAUNCHDAEMONS="${CONFIGD_SKIP_LAUNCHDAEMONS}" \
      bash "${PANTHERA_ROOT}/tools/boot_verify.sh" "${boot_verify_args[@]}"
  fi
  BOOT_SUMMARY="${PANTHERA_ROOT}/artifacts/boot/${TAG}-boot.summary.md"
  if [[ -f "${BOOT_SUMMARY}" ]]; then
    ARTIFACT_PATHS+=("${BOOT_SUMMARY}")
  fi
fi

# Write concise summary markdown artifact
{
  echo "# Panthera Runtime Smoke Summary"
  echo ""
  echo "- Tag: \`${TAG}\`"
  echo "- Probe: \`${PROBE}\`"
  echo "- Stage Only: \`${STAGE_ONLY}\`"
  echo "- Status: **PASS**"
  echo ""
  echo "## Executed Steps"
  for step in "${EXECUTED_STEPS[@]}"; do
    echo "- ${step}"
  done
  echo ""
  echo "## Artifacts"
  for art in "${ARTIFACT_PATHS[@]}"; do
    echo "- \`${art}\`"
  done
} > "${SUMMARY_PATH}"

cp -f "${SUMMARY_PATH}" "${ARTIFACT_DIR}/latest.summary.md"

# Emit exactly one concise PASS line with artifact paths
echo "PASS runtime_smoke probe=${PROBE} stage_only=${STAGE_ONLY} summary=${SUMMARY_PATH}"
