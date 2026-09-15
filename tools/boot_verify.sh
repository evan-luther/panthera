#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ARTIFACT_DIR="${PANTHERA_ROOT}/artifacts/boot"
ROOT_KIND="${PANTHERA_BOOT_VERIFY_ROOT_KIND:-${PANTHERA_ROOT_KIND:-hfs}}"
if [[ "${ROOT_KIND}" == "zfs" ]]; then
  ROOT_DISK="${PANTHERA_ZFS_ROOT_DISK:-${PANTHERA_ROOT}/images/qemu/panthera-zfs-root.img}"
else
  ROOT_DISK="${PANTHERA_ROOT}/images/qemu/panthera-root.img"
fi
ZFS_DATASET="${PANTHERA_ZFS_BOOT:-${PANTHERA_ZFS_ROOT_DATASET:-tank/ROOT/panthera}}"
ROOTDEV="${PANTHERA_ROOTDEV:-uuid}"
TIMEOUT_SECONDS="${PANTHERA_BOOT_VERIFY_TIMEOUT:-180}"
TAG="phase5-boot-$(date +%Y%m%d-%H%M%S)"
REBUILD_ROOTFS=0
STRICT_DNS=0
STRICT_NETWORK=0
STRICT_CONFIGD=0
STRICT_IPCONFIGURATION=0
STRICT_IPCONFIGURATION_PRIME=0
STRICT_IPCONFIGURATION_DHCP=0
STRICT_IPCONFIGURATION_DHCP_PACKET=0
STRICT_IPCONFIGURATION_BOOT=0
STRICT_IPCONFIGURATION_NETWORK=0
DEFAULT_IPCONFIGURATION_NETWORK=0
STRICT_DISPATCH_TIMER=0
STRICT_MDNS=0
DISPATCH_TIMER_AVOID_SHARED_REGION=0
PROBE_NETWORK=0
STAGE_CONFIGD=0
STAGE_IPCONFIGURATION=0
CONFIGD_IPCONFIGURATION_PLUGIN=0
VERIFY_ROOT_SHELL="${PANTHERA_BOOT_VERIFY_ROOT_SHELL:-/bin/mini_sh}"
IPCONFIGURATION_SETTLE_MS="${PANTHERA_BOOT_VERIFY_IPCONFIGURATION_SETTLE_MS:-5000}"
BOOT_ARGS_APPEND="${PANTHERA_BOOT_ARGS_APPEND:-}"
BOOT_ARGS_PATH="${PANTHERA_ROOT}/boot/efi/staging/EFI/PANTHERA/boot-args.txt"
BOOT_ARGS_BASE="${PANTHERA_BOOT_ARGS_BASE:-serial=3 dataconstro=0 kernelmanagerd=0}"
BOOTVERIFY_PLIST_PATH="${PANTHERA_ROOT}/rootfs/System/Library/LaunchDaemons/com.panthera.bootverify.plist"
MONITOR_PATH_OVERRIDE="${PANTHERA_BOOT_VERIFY_MONITOR_PATH:-${PANTHERA_BOOT_VERIFY_MONITOR:-${PANTHERA_QEMU_MONITOR_PATH:-}}}"

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --root-disk PATH       Root disk image to boot (default: ${ROOT_DISK})
  --root-kind KIND       Root filesystem kind: hfs or zfs
                         (default: ${ROOT_KIND})
  --zfs-root DATASET     ZFS boot dataset when --root-kind zfs is used
                         (default: ${ZFS_DATASET})
  --rootdev NAME         XNU root device when --root-kind zfs is used
                         (default: ${ROOTDEV})
  --timeout SECONDS      Expect timeout per boot/check step (default: ${TIMEOUT_SECONDS})
  --tag NAME             Artifact tag (default: ${TAG})
  --monitor PATH         QEMU monitor socket path override
  --rebuild-rootfs       Rebuild rootfs with the assembly-only path before boot
  --verify-root-shell PATH
                         Root shell used when --rebuild-rootfs is set
                         (default: ${VERIFY_ROOT_SHELL})
  --strict-dns           Treat DNS/TCP probe failure as blocking
  --probe-network        Run bounded guest network probes, but keep failures
                         expected unless strict flags are also set
  --strict-network       Treat network bring-up/probe failures as blocking
  --stage-configd        Stage experimental configd and dynamic-store probe
                         without making configd checks blocking
  --strict-configd       Stage experimental configd and require it to create
                         the dynamic store
  --stage-ipconfiguration
                         Stage experimental configd and IPConfiguration
                         without making IPConfiguration checks blocking
  --configd-ipconfiguration-plugin
                         Stage IPConfiguration as configd-owned plugin work
                         and skip the standalone launch daemon
  --strict-ipconfiguration
                         Stage experimental configd and IPConfiguration and
                         require IPConfiguration load/start evidence
  --strict-ipconfiguration-prime
                         Stage experimental configd and IPConfiguration and
                         require queued prime initialization evidence
  --strict-ipconfiguration-dhcp
                         Stage experimental configd and IPConfiguration and
                         require DHCP service startup evidence through BOOTP
                         and ARP client initialization
  --strict-ipconfiguration-dhcp-packet
                         Stage experimental configd and IPConfiguration and
                         require DHCP delayed-start and DISCOVER transmit
                         evidence
  --strict-ipconfiguration-boot
                         Stage experimental configd and IPConfiguration, wait
                         for DHCP bind/publish/default-route traces, and
                         require normal boot-to-login completion
  --strict-ipconfiguration-network
                         Stage experimental configd and IPConfiguration, then
                         require daemon-owned address, route, ping, DNS, and
                         dynamic-store probes
  --default-ipconfiguration-network
                         Rebuild with configd-owned IPConfiguration promoted
                         as the default network owner, then run the strict
                         IPConfiguration network gate
  --strict-dispatch-timer
                         Require a guest dispatch timer source callback smoke
                         test from the boot verifier job
  --strict-mdns          Require mDNSResponder to launch and require dns-sd to
                         read the running daemon version through libdns_sd
  --dispatch-timer-avoid-shared-region
                         Run the dispatch timer probe with DYLD_SHARED_REGION=avoid
                         to test disk dylibs instead of the staged shared cache
  --ipconfiguration-no-inline-dhcp-start
                         Historical compatibility no-op; the Panthera
                         zero-delay timer inline fallback has been retired
  --help                 Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --root-disk)
      ROOT_DISK="$2"
      shift 2
      ;;
    --root-kind)
      ROOT_KIND="$2"
      shift 2
      ;;
    --zfs-root)
      ZFS_DATASET="$2"
      shift 2
      ;;
    --rootdev)
      ROOTDEV="$2"
      shift 2
      ;;
    --timeout)
      TIMEOUT_SECONDS="$2"
      shift 2
      ;;
    --tag)
      TAG="$2"
      shift 2
      ;;
    --monitor|--monitor-path)
      MONITOR_PATH_OVERRIDE="$2"
      shift 2
      ;;
    --rebuild-rootfs)
      REBUILD_ROOTFS=1
      shift
      ;;
    --verify-root-shell)
      VERIFY_ROOT_SHELL="$2"
      shift 2
      ;;
    --strict-dns)
      STRICT_DNS=1
      PROBE_NETWORK=1
      shift
      ;;
    --probe-network)
      PROBE_NETWORK=1
      shift
      ;;
    --strict-network)
      STRICT_NETWORK=1
      PROBE_NETWORK=1
      shift
      ;;
    --stage-configd)
      STAGE_CONFIGD=1
      shift
      ;;
    --strict-configd)
      STAGE_CONFIGD=1
      STRICT_CONFIGD=1
      shift
      ;;
    --stage-ipconfiguration)
      STAGE_CONFIGD=1
      STAGE_IPCONFIGURATION=1
      shift
      ;;
    --configd-ipconfiguration-plugin)
      STAGE_CONFIGD=1
      STAGE_IPCONFIGURATION=1
      CONFIGD_IPCONFIGURATION_PLUGIN=1
      shift
      ;;
    --strict-ipconfiguration)
      STAGE_CONFIGD=1
      STRICT_CONFIGD=1
      STAGE_IPCONFIGURATION=1
      STRICT_IPCONFIGURATION=1
      shift
      ;;
    --strict-ipconfiguration-prime)
      STAGE_CONFIGD=1
      STRICT_CONFIGD=1
      STAGE_IPCONFIGURATION=1
      STRICT_IPCONFIGURATION=1
      STRICT_IPCONFIGURATION_PRIME=1
      shift
      ;;
    --strict-ipconfiguration-dhcp)
      STAGE_CONFIGD=1
      STRICT_CONFIGD=1
      STAGE_IPCONFIGURATION=1
      STRICT_IPCONFIGURATION=1
      STRICT_IPCONFIGURATION_PRIME=1
      STRICT_IPCONFIGURATION_DHCP=1
      shift
      ;;
    --strict-ipconfiguration-dhcp-packet)
      STAGE_CONFIGD=1
      STRICT_CONFIGD=1
      STAGE_IPCONFIGURATION=1
      STRICT_IPCONFIGURATION=1
      STRICT_IPCONFIGURATION_PRIME=1
      STRICT_IPCONFIGURATION_DHCP=1
      STRICT_IPCONFIGURATION_DHCP_PACKET=1
      shift
      ;;
    --strict-ipconfiguration-boot)
      STAGE_CONFIGD=1
      STRICT_CONFIGD=1
      STAGE_IPCONFIGURATION=1
      STRICT_IPCONFIGURATION=1
      STRICT_IPCONFIGURATION_PRIME=1
      STRICT_IPCONFIGURATION_DHCP=1
      STRICT_IPCONFIGURATION_DHCP_PACKET=1
      STRICT_IPCONFIGURATION_BOOT=1
      shift
      ;;
    --strict-ipconfiguration-network)
      STAGE_CONFIGD=1
      STRICT_CONFIGD=1
      STAGE_IPCONFIGURATION=1
      STRICT_IPCONFIGURATION=1
      STRICT_IPCONFIGURATION_PRIME=1
      STRICT_IPCONFIGURATION_DHCP=1
      STRICT_IPCONFIGURATION_DHCP_PACKET=1
      STRICT_IPCONFIGURATION_NETWORK=1
      shift
      ;;
    --default-ipconfiguration-network)
      STAGE_CONFIGD=1
      STRICT_CONFIGD=1
      STAGE_IPCONFIGURATION=1
      CONFIGD_IPCONFIGURATION_PLUGIN=1
      DEFAULT_IPCONFIGURATION_NETWORK=1
      STRICT_IPCONFIGURATION=1
      STRICT_IPCONFIGURATION_PRIME=1
      STRICT_IPCONFIGURATION_NETWORK=1
      shift
      ;;
    --ipconfiguration-no-inline-dhcp-start)
      # Historical compatibility no-op: zero-delay timer inline fallback retired.
      shift
      ;;
    --strict-dispatch-timer)
      STRICT_DISPATCH_TIMER=1
      shift
      ;;
    --strict-mdns)
      STRICT_MDNS=1
      shift
      ;;
    --dispatch-timer-avoid-shared-region)
      DISPATCH_TIMER_AVOID_SHARED_REGION=1
      shift
      ;;
    --help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

case "${ROOT_KIND}" in
  hfs|zfs)
    ;;
  *)
    echo "--root-kind must be hfs or zfs" >&2
    exit 2
    ;;
esac

if [[ "${REBUILD_ROOTFS}" == "1" \
   && "${DEFAULT_IPCONFIGURATION_NETWORK}" == "0" \
   && "${STAGE_CONFIGD}" == "0" \
   && "${STAGE_IPCONFIGURATION}" == "0" \
   && "${CONFIGD_IPCONFIGURATION_PLUGIN}" == "0" \
   && "${PANTHERA_DEFAULT_NETWORK_OWNER:-ipconfiguration}" == "ipconfiguration" ]]; then
  STAGE_CONFIGD=1
  STRICT_CONFIGD=1
  STAGE_IPCONFIGURATION=1
  CONFIGD_IPCONFIGURATION_PLUGIN=1
  DEFAULT_IPCONFIGURATION_NETWORK=1
  STRICT_IPCONFIGURATION=1
  STRICT_IPCONFIGURATION_PRIME=1
  STRICT_IPCONFIGURATION_DHCP=1
  STRICT_IPCONFIGURATION_DHCP_PACKET=1
  STRICT_IPCONFIGURATION_NETWORK=1
fi

if [[ "${STRICT_IPCONFIGURATION}" == "1" \
   && "${PANTHERA_STANDALONE_IPCONFIGURATION:-0}" != "1" ]]; then
  CONFIGD_IPCONFIGURATION_PLUGIN=1
fi

if [[ "${REBUILD_ROOTFS}" != "1" \
   && ( "${STRICT_CONFIGD}" == "1" || "${STRICT_IPCONFIGURATION}" == "1" ) ]]; then
  cat >&2 <<EOF
warning: strict configd/IPConfiguration markers are trace-gated by default.
warning: without --rebuild-rootfs, the selected image must already stage the
warning: matching launchd EnvironmentVariables or strict parsing may fail.
EOF
fi

mkdir -p "${ARTIFACT_DIR}"

LOG_PATH="${ARTIFACT_DIR}/${TAG}.log"
SUMMARY_PATH="${ARTIFACT_DIR}/${TAG}.summary.md"
STATUS_PATH="${ARTIFACT_DIR}/${TAG}.status"
VERIFY_EXPORTS_LOG="${ARTIFACT_DIR}/${TAG}.verify_exports.log"
CREATED_MONITOR_DIR=""
if [[ -n "${MONITOR_PATH_OVERRIDE}" ]]; then
  MONITOR_PATH="${MONITOR_PATH_OVERRIDE}"
else
  CREATED_MONITOR_DIR="$(mktemp -d /tmp/panthera-boot-verify.XXXXXX)"
  MONITOR_PATH="${CREATED_MONITOR_DIR}/monitor.sock"
fi

LATEST_LOG="${ARTIFACT_DIR}/latest.log"
LATEST_SUMMARY="${ARTIFACT_DIR}/latest.summary"
LATEST_STATUS="${ARTIFACT_DIR}/latest.status"
LATEST_VERIFY_EXPORTS_LOG="${ARTIFACT_DIR}/latest.verify_exports.log"

rm -f \
  "${LOG_PATH}" \
  "${SUMMARY_PATH}" \
  "${STATUS_PATH}" \
  "${VERIFY_EXPORTS_LOG}" \
  "${MONITOR_PATH}" \
  "${LATEST_LOG}" \
  "${LATEST_SUMMARY}" \
  "${LATEST_STATUS}" \
  "${LATEST_VERIFY_EXPORTS_LOG}"

cd "${PANTHERA_ROOT}"

RESTORE_BOOT_ARGS=0
REMOVE_BOOT_ARGS_ON_RESTORE=0
ORIGINAL_BOOT_ARGS=""
RESTORE_BOOTVERIFY_PLIST=0
ORIGINAL_BOOTVERIFY_PLIST=""

restore_boot_args() {
  if [[ "${RESTORE_BOOT_ARGS}" == "1" ]]; then
    if [[ "${REMOVE_BOOT_ARGS_ON_RESTORE}" == "1" ]]; then
      rm -f "${BOOT_ARGS_PATH}"
    else
      printf '%s\n' "${ORIGINAL_BOOT_ARGS}" > "${BOOT_ARGS_PATH}"
    fi
  fi
}

restore_bootverify_plist() {
  if [[ "${RESTORE_BOOTVERIFY_PLIST}" == "1" && -n "${ORIGINAL_BOOTVERIFY_PLIST}" ]]; then
    cp -p "${ORIGINAL_BOOTVERIFY_PLIST}" "${BOOTVERIFY_PLIST_PATH}"
    rm -f "${ORIGINAL_BOOTVERIFY_PLIST}"
  fi
}

cleanup() {
  restore_boot_args
  restore_bootverify_plist
  if [[ -n "${CREATED_MONITOR_DIR:-}" && -d "${CREATED_MONITOR_DIR}" ]]; then
    rm -rf "${CREATED_MONITOR_DIR}"
  elif [[ -n "${MONITOR_PATH:-}" ]]; then
    rm -f "${MONITOR_PATH}"
  fi
}
trap cleanup EXIT

ensure_bootverify_plist_backup() {
  if [[ "${RESTORE_BOOTVERIFY_PLIST}" == "0" ]]; then
    ORIGINAL_BOOTVERIFY_PLIST="$(mktemp "${ARTIFACT_DIR}/${TAG}.bootverify.plist.XXXXXX")"
    cp -p "${BOOTVERIFY_PLIST_PATH}" "${ORIGINAL_BOOTVERIFY_PLIST}"
    RESTORE_BOOTVERIFY_PLIST=1
  fi
}

stage_bootverify_dispatch_timer() {
  ensure_bootverify_plist_backup
  /usr/libexec/PlistBuddy -c "Add :EnvironmentVariables dict" "${BOOTVERIFY_PLIST_PATH}" >/dev/null 2>&1 || true
  /usr/libexec/PlistBuddy -c "Delete :EnvironmentVariables:PANTHERA_BOOT_VERIFY_RUN_DISPATCH_TIMER" "${BOOTVERIFY_PLIST_PATH}" >/dev/null 2>&1 || true
  /usr/libexec/PlistBuddy -c "Add :EnvironmentVariables:PANTHERA_BOOT_VERIFY_RUN_DISPATCH_TIMER string 1" "${BOOTVERIFY_PLIST_PATH}"
  /usr/libexec/PlistBuddy -c "Delete :EnvironmentVariables:PANTHERA_BOOT_VERIFY_DISPATCH_TIMER_AVOID_SHARED_REGION" "${BOOTVERIFY_PLIST_PATH}" >/dev/null 2>&1 || true
  if [[ "${DISPATCH_TIMER_AVOID_SHARED_REGION}" == "1" ]]; then
    /usr/libexec/PlistBuddy -c "Add :EnvironmentVariables:PANTHERA_BOOT_VERIFY_DISPATCH_TIMER_AVOID_SHARED_REGION string 1" "${BOOTVERIFY_PLIST_PATH}"
  fi
}

stage_bootverify_daemon_network() {
  ensure_bootverify_plist_backup
  /usr/libexec/PlistBuddy -c "Add :EnvironmentVariables dict" "${BOOTVERIFY_PLIST_PATH}" >/dev/null 2>&1 || true
  /usr/libexec/PlistBuddy -c "Delete :EnvironmentVariables:PANTHERA_BOOT_VERIFY_RUN_DAEMON_NETWORK" "${BOOTVERIFY_PLIST_PATH}" >/dev/null 2>&1 || true
  /usr/libexec/PlistBuddy -c "Add :EnvironmentVariables:PANTHERA_BOOT_VERIFY_RUN_DAEMON_NETWORK string 1" "${BOOTVERIFY_PLIST_PATH}"
}

if [[ "${REBUILD_ROOTFS}" == "1" ]]; then
  if [[ "${PROBE_NETWORK}" == "1" ]]; then
    PANTHERA_BOOT_VERIFY_RUN_NETWORK=1 bash tools/build_boot_verify_guest.sh
  else
    bash tools/build_boot_verify_guest.sh
  fi
  if [[ "${STRICT_DISPATCH_TIMER}" == "1" ]]; then
    bash tools/build_dispatch_timer_probe.sh
    stage_bootverify_dispatch_timer
  fi
  if [[ "${STRICT_MDNS}" == "1" ]]; then
    bash tools/build_mdns_dns_sd_probe.sh
  fi
  if [[ "${STRICT_IPCONFIGURATION_NETWORK}" == "1" || "${PANTHERA_BOOT_VERIFY_RUN_DAEMON_NETWORK:-0}" == "1" ]]; then
    stage_bootverify_daemon_network
  fi
  rootfs_env=(PANTHERA_ROOT_SHELL="${VERIFY_ROOT_SHELL}")
  if [[ "${DEFAULT_IPCONFIGURATION_NETWORK}" == "1" ]]; then
    rootfs_env+=(
      PANTHERA_DEFAULT_NETWORK_OWNER=ipconfiguration
    )
  fi
  if [[ "${STAGE_CONFIGD}" == "1" ]]; then
    rootfs_env+=(
      PANTHERA_STAGE_CONFIGD=1
      PANTHERA_STAGE_SC_PROBE=1
    )
  fi
  if [[ "${STAGE_IPCONFIGURATION}" == "1" ]]; then
    rootfs_env+=(
      PANTHERA_STAGE_IPCONFIGURATION=1
    )
  fi
  if [[ "${CONFIGD_IPCONFIGURATION_PLUGIN}" == "1" ]]; then
    rootfs_env+=(
      PANTHERA_CONFIGD_IPCONFIGURATION=1
    )
  fi
  if [[ "${STRICT_CONFIGD}" == "1" ]]; then
    rootfs_env+=(
      PANTHERA_CONFIGD_TRACE=1
      PANTHERA_CONFIGD_ROUTE_TRACE=1
      PANTHERA_CONFIGD_KERNEL_EVENT_MONITOR_TRACE=1
      PANTHERA_SYSTEMCONFIGURATION_TRACE=1
      PANTHERA_IOKIT_TRACE=1
    )
  fi
  if [[ "${STRICT_IPCONFIGURATION}" == "1" ]]; then
    rootfs_env+=(
      PANTHERA_IPCONFIGURATION_TRACE=1
    )
  fi
  skip_launchdaemons="${PANTHERA_SKIP_LAUNCHDAEMONS:-}"
  append_skip_launchdaemon() {
    local plist_name="$1"
    if [[ -n "${skip_launchdaemons}" ]]; then
      skip_launchdaemons+=",${plist_name}"
    else
      skip_launchdaemons="${plist_name}"
    fi
  }
  if [[ "${PROBE_NETWORK}" == "1" || "${STAGE_IPCONFIGURATION}" == "1" ]]; then
    append_skip_launchdaemon "com.panthera.netbringup.plist"
  fi
  if [[ "${CONFIGD_IPCONFIGURATION_PLUGIN}" == "1" ]]; then
    append_skip_launchdaemon "com.apple.IPConfiguration.plist"
  fi
  if [[ -n "${skip_launchdaemons}" ]]; then
    rootfs_env+=(PANTHERA_SKIP_LAUNCHDAEMONS="${skip_launchdaemons}")
  fi
  if [[ "${ROOT_KIND}" == "zfs" ]]; then
    env "${rootfs_env[@]}" PATH="/usr/local/zfs/bin:${PATH}" \
      bash rootfs/create_zfs_root_image.sh \
        --root-disk "${ROOT_DISK}" \
        --dataset "${ZFS_DATASET}" \
        --force
  else
    env "${rootfs_env[@]}" bash rootfs/create_hfs_root_image.sh --force --no-build-components
  fi
fi

if [[ "${ROOT_KIND}" == "zfs" ]]; then
  bash boot/efi/stage_phase2_efi.sh \
    --zfs-root "${ZFS_DATASET}" \
    --rootdev "${ROOTDEV}" \
    > "${ARTIFACT_DIR}/${TAG}.stage_phase2.log" 2>&1
fi

if [[ -n "${BOOT_ARGS_APPEND}" ]]; then
  mkdir -p "$(dirname "${BOOT_ARGS_PATH}")"
  if [[ -f "${BOOT_ARGS_PATH}" ]]; then
    ORIGINAL_BOOT_ARGS="$(cat "${BOOT_ARGS_PATH}")"
  else
    ORIGINAL_BOOT_ARGS="${BOOT_ARGS_BASE}"
    REMOVE_BOOT_ARGS_ON_RESTORE=1
  fi
  RESTORE_BOOT_ARGS=1
  printf '%s %s\n' "${ORIGINAL_BOOT_ARGS}" "${BOOT_ARGS_APPEND}" > "${BOOT_ARGS_PATH}"
fi

export BOOT_VERIFY_LOG="${LOG_PATH}"
export BOOT_VERIFY_MONITOR="${MONITOR_PATH}"
export BOOT_VERIFY_ROOT_DISK="${ROOT_DISK}"
if [[ "${ROOT_KIND}" == "zfs" ]]; then
  export BOOT_VERIFY_ZFS_DATASET="${ZFS_DATASET}"
  export BOOT_VERIFY_ROOTDEV="${ROOTDEV}"
else
  export BOOT_VERIFY_ZFS_DATASET=""
  export BOOT_VERIFY_ROOTDEV=""
fi
export BOOT_VERIFY_TIMEOUT="${TIMEOUT_SECONDS}"
export BOOT_VERIFY_ROOT_SHELL="${VERIFY_ROOT_SHELL}"
export BOOT_VERIFY_PROBE_NETWORK="${PROBE_NETWORK}"
export BOOT_VERIFY_STRICT_CONFIGD="${STRICT_CONFIGD}"
export BOOT_VERIFY_STRICT_IPCONFIGURATION="${STRICT_IPCONFIGURATION}"
export BOOT_VERIFY_STRICT_IPCONFIGURATION_PRIME="${STRICT_IPCONFIGURATION_PRIME}"
export BOOT_VERIFY_STRICT_IPCONFIGURATION_DHCP="${STRICT_IPCONFIGURATION_DHCP}"
export BOOT_VERIFY_STRICT_IPCONFIGURATION_DHCP_PACKET="${STRICT_IPCONFIGURATION_DHCP_PACKET}"
export BOOT_VERIFY_STRICT_IPCONFIGURATION_BOOT="${STRICT_IPCONFIGURATION_BOOT}"
export BOOT_VERIFY_STRICT_IPCONFIGURATION_NETWORK="${STRICT_IPCONFIGURATION_NETWORK}"
export BOOT_VERIFY_IPCONFIGURATION_SETTLE_MS="${IPCONFIGURATION_SETTLE_MS}"
export BOOT_VERIFY_STRICT_DISPATCH_TIMER="${STRICT_DISPATCH_TIMER}"
if [[ "${STRICT_DISPATCH_TIMER}" == "1" && "${REBUILD_ROOTFS}" == "1" ]]; then
  export BOOT_VERIFY_DISPATCH_TIMER_BOOT_STAGED=1
else
  export BOOT_VERIFY_DISPATCH_TIMER_BOOT_STAGED=0
fi
export BOOT_VERIFY_STRICT_MDNS="${STRICT_MDNS}"
if [[ "${ROOT_KIND}" == "zfs" || "${PANTHERA_NO_SHARED_CACHE:-0}" == "1" ]]; then
  export BOOT_VERIFY_FIX_CACHE_OWNERSHIP=0
else
  export BOOT_VERIFY_FIX_CACHE_OWNERSHIP=1
fi

expect_result=0
expect <<'EXPECT' || expect_result=$?
set timeout $env(BOOT_VERIFY_TIMEOUT)
set log_path $env(BOOT_VERIFY_LOG)
set monitor_path $env(BOOT_VERIFY_MONITOR)
set root_disk $env(BOOT_VERIFY_ROOT_DISK)
set verify_root_shell $env(BOOT_VERIFY_ROOT_SHELL)
set probe_network $env(BOOT_VERIFY_PROBE_NETWORK)
set strict_configd $env(BOOT_VERIFY_STRICT_CONFIGD)
set strict_ipconfiguration $env(BOOT_VERIFY_STRICT_IPCONFIGURATION)
set strict_ipconfiguration_prime $env(BOOT_VERIFY_STRICT_IPCONFIGURATION_PRIME)
set strict_ipconfiguration_dhcp_packet $env(BOOT_VERIFY_STRICT_IPCONFIGURATION_DHCP_PACKET)
set strict_ipconfiguration_boot $env(BOOT_VERIFY_STRICT_IPCONFIGURATION_BOOT)
set strict_ipconfiguration_network $env(BOOT_VERIFY_STRICT_IPCONFIGURATION_NETWORK)
set ipconfiguration_settle_ms $env(BOOT_VERIFY_IPCONFIGURATION_SETTLE_MS)
set strict_dispatch_timer $env(BOOT_VERIFY_STRICT_DISPATCH_TIMER)
set dispatch_timer_boot_staged $env(BOOT_VERIFY_DISPATCH_TIMER_BOOT_STAGED)
set strict_mdns $env(BOOT_VERIFY_STRICT_MDNS)
set fix_cache_ownership $env(BOOT_VERIFY_FIX_CACHE_OWNERSHIP)
set send_human {.15 .35 1 .04 2}
set ipconfiguration_network_ready 0

log_file -a $log_path

proc quit_qemu {} {
    global monitor_path
    catch {exec sh -c "printf 'quit\n' | nc -U \"$monitor_path\" >/dev/null 2>&1"}
}

proc fail_and_quit {message} {
    puts "\nPANTHERA_BOOT_VERIFY_EXPECT_FAIL: $message"
    quit_qemu
    exit 1
}

proc wait_prompt {} {
    expect {
        -re {root@panthera:[^\r\n]*[#%]|panthera#} {
            return 0
        }
        timeout {
            return 1
        }
        eof {
            return 1
        }
    }
}

proc wait_prompt_and_guest_end {} {
    set saw_prompt 0
    set saw_guest_end 0
    set saw_bootstrap_pass 0

    while {$saw_prompt == 0 || $saw_guest_end == 0 || $saw_bootstrap_pass == 0} {
        expect {
            -re {PANTHERA_BOOT_VERIFY_GUEST_END} {
                set saw_guest_end 1
            }
            -re {test_bootstrap(_simple)?: PASS} {
                set saw_bootstrap_pass 1
            }
            -re {root@panthera:[^\r\n]*[#%]|panthera#} {
                set saw_prompt 1
            }
            timeout {
                return 1
            }
            eof {
                return 1
            }
        }
    }
    return 0
}

proc wait_bootstrap {} {
    set saw_bootstrap_pass 0

    while {$saw_bootstrap_pass == 0} {
        expect {
            -re {test_bootstrap(_simple)?: PASS} {
                set saw_bootstrap_pass 1
            }
            -re {__PANTHERA_STATUS_bootstrap:0__} {
                set saw_bootstrap_pass 1
            }
            timeout {
                return 1
            }
            eof {
                return 1
            }
        }
    }
    return 0
}

proc observe_network_probe {} {
    global timeout
    set old_timeout $timeout
    set timeout 20
    set saw_netbringup 0
    set saw_ifconfig 0
    set saw_gateway_ping 0
    set saw_dns_tcp 0

    while {$saw_netbringup == 0 || $saw_ifconfig == 0 || $saw_gateway_ping == 0 || $saw_dns_tcp == 0} {
        expect {
            -re {__PANTHERA_STATUS_netbringup:-?[0-9]+__} {
                set saw_netbringup 1
            }
            -re {__PANTHERA_STATUS_ifconfig:-?[0-9]+__} {
                set saw_ifconfig 1
            }
            -re {__PANTHERA_STATUS_gateway_ping:-?[0-9]+__} {
                set saw_gateway_ping 1
            }
            -re {__PANTHERA_STATUS_dns_tcp:-?[0-9]+__} {
                set saw_dns_tcp 1
            }
            -re {PANTHERA_BOOT_VERIFY_GUEST_END} {
                break
            }
            timeout {
                puts "\nPANTHERA_BOOT_VERIFY_EXPECT_TIMEOUT: network_probe_observe"
                break
            }
            eof {
                break
            }
        }
    }

    set timeout $old_timeout
    return 0
}

proc observe_ipconfiguration {} {
    global timeout
    set old_timeout $timeout
    set timeout 45
    set saw_ready 0
    set saw_load_enter 0
    set saw_load_return 0
    set saw_start_return 0
    set saw_prime 0

    while {$saw_ready == 0 || $saw_load_enter == 0 || $saw_load_return == 0 || $saw_start_return == 0 || $saw_prime == 0} {
        expect {
            -re {PANTHERA:IPConfiguration configd ready} {
                set saw_ready 1
            }
            -re {PANTHERA:IPConfiguration load enter} {
                set saw_load_enter 1
            }
            -re {PANTHERA:IPConfiguration load returned} {
                set saw_load_return 1
            }
            -re {PANTHERA:IPConfiguration start returned} {
                set saw_start_return 1
            }
            -re {PANTHERA:IPConfiguration prime queued} {
                set saw_prime 1
            }
            timeout {
                puts "\nPANTHERA_BOOT_VERIFY_EXPECT_TIMEOUT: ipconfiguration_observe"
                break
            }
            eof {
                puts "\nPANTHERA_BOOT_VERIFY_EXPECT_EOF: ipconfiguration_observe"
                break
            }
        }
    }

    set timeout $old_timeout
    if {$saw_prime == 1} {
        return 0
    }
    return 1
}

proc observe_ipconfiguration_prime {} {
    global timeout
    set old_timeout $timeout
    set timeout 60
    set saw_launcher 0
    set saw_ready 0
    set saw_load_enter 0
    set saw_load_return 0
    set saw_start_return 0
    set saw_prime 0
    set saw_handle_enter 0
    set saw_handle_start 0
    set saw_initialization_enter 0
    set saw_initialization_return 0
    set saw_server_init_enter 0
    set saw_server_init_return 0
    set saw_state_handler_enter 0
    set saw_state_handler_return 0
    set saw_handle_return 0

    while {$saw_launcher == 0 || $saw_ready == 0 || $saw_load_enter == 0 || $saw_load_return == 0 || $saw_start_return == 0 || $saw_prime == 0 || $saw_handle_enter == 0 || $saw_handle_start == 0 || $saw_initialization_enter == 0 || $saw_initialization_return == 0 || $saw_server_init_enter == 0 || $saw_server_init_return == 0 || $saw_state_handler_enter == 0 || $saw_state_handler_return == 0 || $saw_handle_return == 0} {
        expect {
            -re {PANTHERA:configd IPConfiguration plugin start} {
                set saw_launcher 1
            }
            -re {PANTHERA:IPConfiguration launcher start} {
                set saw_launcher 1
            }
            -re {PANTHERA:IPConfiguration configd ready} {
                set saw_ready 1
            }
            -re {PANTHERA:IPConfiguration load enter} {
                set saw_load_enter 1
            }
            -re {PANTHERA:IPConfiguration load returned} {
                set saw_load_return 1
            }
            -re {PANTHERA:IPConfiguration start returned} {
                set saw_start_return 1
            }
            -re {PANTHERA:IPConfiguration prime queued} {
                set saw_prime 1
            }
            -re {PANTHERA:IPConfiguration handle prime enter} {
                set saw_handle_enter 1
            }
            -re {PANTHERA:IPConfiguration prime handle start} {
                set saw_handle_start 1
            }
            -re {PANTHERA:IPConfiguration prime start initialization enter} {
                set saw_initialization_enter 1
            }
            -re {PANTHERA:IPConfiguration prime start initialization returned} {
                set saw_initialization_return 1
            }
            -re {PANTHERA:IPConfiguration prime server init enter} {
                set saw_server_init_enter 1
            }
            -re {PANTHERA:IPConfiguration prime server init returned} {
                set saw_server_init_return 1
            }
            -re {PANTHERA:IPConfiguration prime state handler enter} {
                set saw_state_handler_enter 1
            }
            -re {PANTHERA:IPConfiguration prime state handler returned} {
                set saw_state_handler_return 1
            }
            -re {PANTHERA:IPConfiguration handle prime returned} {
                set saw_handle_return 1
            }
            timeout {
                puts "\nPANTHERA_BOOT_VERIFY_EXPECT_TIMEOUT: ipconfiguration_prime_observe"
                break
            }
            eof {
                puts "\nPANTHERA_BOOT_VERIFY_EXPECT_EOF: ipconfiguration_prime_observe"
                break
            }
        }
    }

    set timeout $old_timeout
    if {$saw_handle_return == 1} {
        return 0
    }
    return 1
}

proc observe_ipconfiguration_dhcp_packet {} {
    global timeout
    set old_timeout $timeout
    set timeout 90
    set saw_prime 0
    set saw_delayed_start 0
    set saw_check_link 0
    set saw_init_enter 0
    set saw_request 0
    set saw_enable_receive 0
    set saw_transmit 0

    while {$saw_prime == 0 || $saw_delayed_start == 0 || $saw_check_link == 0 || $saw_init_enter == 0 || $saw_request == 0 || $saw_enable_receive == 0 || $saw_transmit == 0} {
        expect {
            -re {PANTHERA:IPConfiguration prime queued} {
                set saw_prime 1
            }
            -re {PANTHERA:DHCP delayed start fired} {
                set saw_delayed_start 1
            }
            -re {PANTHERA:DHCP check link if=en0 event=0 valid=[01] active=[01] wait=0} {
                set saw_check_link 1
            }
            -re {PANTHERA:DHCP init start enter if=en0} {
                set saw_init_enter 1
            }
            -re {PANTHERA:DHCP init request made ptr=0x[0-9a-fA-F]+} {
                set saw_request 1
            }
            -re {PANTHERA:DHCP init enable receive returned} {
                set saw_enable_receive 1
            }
            -re {PANTHERA:DHCP init transmit returned ok} {
                set saw_transmit 1
            }
            timeout {
                puts "\nPANTHERA_BOOT_VERIFY_EXPECT_TIMEOUT: ipconfiguration_dhcp_packet_observe"
                break
            }
            eof {
                puts "\nPANTHERA_BOOT_VERIFY_EXPECT_EOF: ipconfiguration_dhcp_packet_observe"
                break
            }
        }
    }

    set timeout $old_timeout
    if {$saw_transmit == 1} {
        return 0
    }
    return 1
}

proc observe_ipconfiguration_network_ready {} {
    global timeout
    global ipconfiguration_network_ready
    if {$ipconfiguration_network_ready == 1} {
        return 0
    }
    set old_timeout $timeout
    set timeout 180
    set saw_network_ready 0

    while {$saw_network_ready == 0} {
        expect {
            -re {PANTHERA:configd (route-manager|IPMonitor) default route write=[1-9][0-9]*|PANTHERA:IPConfiguration publish global IPv4 ok=1|PANTHERA:configd service-to-global Global IPv4 published} {
                set saw_network_ready 1
            }
            timeout {
                set timeout $old_timeout
                puts "\nPANTHERA_BOOT_VERIFY_EXPECT_TIMEOUT: ipconfiguration_network_ready"
                return 1
            }
            eof {
                set timeout $old_timeout
                puts "\nPANTHERA_BOOT_VERIFY_EXPECT_EOF: ipconfiguration_network_ready"
                return 1
            }
        }
    }

    set timeout $old_timeout
    set ipconfiguration_network_ready 1
    return 0
}

proc observe_ipconfiguration_boot_ready {} {
    global timeout
    global ipconfiguration_network_ready
    set old_timeout $timeout
    set timeout 180
    set saw_request_transmit 0
    set saw_ack_receive 0
    set saw_bound_address 0
    set saw_address_ioctl 0
    set saw_publish_sync 0
    set saw_publish_global_ipv4 0
    set saw_publish_global_dns 0
    set saw_default_route 0

    while {$saw_request_transmit == 0 || $saw_ack_receive == 0 || $saw_bound_address == 0 || $saw_address_ioctl == 0 || $saw_publish_sync == 0 || $saw_publish_global_ipv4 == 0 || $saw_publish_global_dns == 0 || $saw_default_route == 0} {
        expect {
            -re {PANTHERA:DHCP select transmit returned ok} {
                set saw_request_transmit 1
            }
            -re {PANTHERA:DHCP verify ok type=5 server=10\.0\.2\.2} {
                set saw_ack_receive 1
            }
            -re {PANTHERA:DHCP bound set address returned} {
                set saw_bound_address 1
            }
            -re {PANTHERA:IPConfiguration service set address ioctl returned} {
                set saw_address_ioctl 1
            }
            -re {PANTHERA:IPConfiguration publish (sync|async) returned} {
                set saw_publish_sync 1
            }
            -re {PANTHERA:IPConfiguration publish global IPv4 ok=1|PANTHERA:configd service-to-global Global IPv4 published} {
                set saw_publish_global_ipv4 1
            }
            -re {PANTHERA:IPConfiguration publish global DNS ok=1|PANTHERA:configd service-to-global Global DNS published} {
                set saw_publish_global_dns 1
            }
            -re {PANTHERA:configd (route-manager|IPMonitor) default route write=[1-9][0-9]*} {
                set saw_default_route 1
            }
            timeout {
                puts "\nPANTHERA_BOOT_VERIFY_EXPECT_TIMEOUT: ipconfiguration_boot_ready"
                break
            }
            eof {
                puts "\nPANTHERA_BOOT_VERIFY_EXPECT_EOF: ipconfiguration_boot_ready"
                break
            }
        }
    }

    set timeout $old_timeout
    if {$saw_default_route == 1} {
        set ipconfiguration_network_ready 1
        return 0
    }
    return 1
}

proc send_line {command} {
    after 1000
    send -- "$command\r"
    after 1000
}

proc run_cmd {name command} {
    send_line $command
    expect {
        -re "__PANTHERA_STATUS_${name}:-?\[0-9]+__" {
        }
        timeout {
            puts "\nPANTHERA_BOOT_VERIFY_EXPECT_TIMEOUT: $name"
            return 1
        }
        eof {
            puts "\nPANTHERA_BOOT_VERIFY_EXPECT_EOF: $name"
            return 1
        }
    }
    return [wait_prompt]
}

proc run_mini_cmd {name command} {
    send_line $command
    if {[wait_prompt] != 0} {
        puts "\nPANTHERA_BOOT_VERIFY_EXPECT_TIMEOUT: $name"
        return 1
    }
    send_line "echo __PANTHERA_STATUS_${name}:0__"
    expect {
        -re "__PANTHERA_STATUS_${name}:0__" {
        }
        timeout {
            puts "\nPANTHERA_BOOT_VERIFY_EXPECT_TIMEOUT: ${name}_marker"
            return 1
        }
        eof {
            puts "\nPANTHERA_BOOT_VERIFY_EXPECT_EOF: ${name}_marker"
            return 1
        }
    }
    return [wait_prompt]
}

proc observe_ipconfiguration_daemon_network {} {
    global timeout
    set old_timeout $timeout
    set timeout 180
    set saw_success 0

    while {$saw_success == 0} {
        expect {
            -re {__PANTHERA_STATUS_ipconfiguration_daemon_network:0__} {
                set saw_success 1
            }
            timeout {
                set timeout $old_timeout
                puts "\nPANTHERA_BOOT_VERIFY_EXPECT_TIMEOUT: ipconfiguration_daemon_network"
                return 1
            }
            eof {
                set timeout $old_timeout
                puts "\nPANTHERA_BOOT_VERIFY_EXPECT_EOF: ipconfiguration_daemon_network"
                return 1
            }
        }
    }

    set timeout $old_timeout
    return 0
}

proc run_dispatch_timer_check {} {
    return [run_cmd dispatch_timer {/usr/bin/boot_verify_guest --dispatch-timer}]
}

proc observe_mdns_ready {} {
    global timeout
    set old_timeout $timeout
    set timeout 150
    expect {
        -re {PANTHERA:mDNSResponder MainLoop enter} {
        }
        timeout {
            set timeout $old_timeout
            puts "\nPANTHERA_BOOT_VERIFY_EXPECT_TIMEOUT: mdns_ready"
            return 1
        }
        eof {
            set timeout $old_timeout
            puts "\nPANTHERA_BOOT_VERIFY_EXPECT_EOF: mdns_ready"
            return 1
        }
    }
    set timeout $old_timeout
    return 0
}

proc run_mdns_check {} {
    return [run_cmd mdns_dns_sd_probe {/usr/bin/mdns_dns_sd_probe; rc=$?; /usr/bin/printf '__PANTHERA_STATUS_mdns_dns_sd_probe:%d__\n' $rc}]
}

spawn env \
    PANTHERA_QEMU_MONITOR_PATH=$monitor_path \
    PANTHERA_ROOT_DISK=$root_disk \
    PANTHERA_ZFS_BOOT=$env(BOOT_VERIFY_ZFS_DATASET) \
    PANTHERA_ROOTDEV=$env(BOOT_VERIFY_ROOTDEV) \
    PANTHERA_FIX_CACHE_OWNERSHIP=$fix_cache_ownership \
    PANTHERA_CONFIGD_TRACE=$strict_configd \
    PANTHERA_CONFIGD_ROUTE_TRACE=$strict_configd \
    PANTHERA_CONFIGD_KERNEL_EVENT_MONITOR_TRACE=$strict_configd \
    PANTHERA_SYSTEMCONFIGURATION_TRACE=$strict_configd \
    PANTHERA_IOKIT_TRACE=$strict_configd \
    PANTHERA_IPCONFIGURATION_TRACE=$strict_ipconfiguration \
    boot/qemu/run_phase2_serial.sh --no-reboot --ssh-port 0

expect {
    -re {login: } {
        send -- "root\r"
    }
    timeout {
        fail_and_quit "login prompt timeout"
    }
    eof {
        fail_and_quit "qemu exited before login prompt"
    }
}

expect {
    -re {Password: } {
        send -- "\r"
    }
    timeout {
        fail_and_quit "password prompt timeout"
    }
    eof {
        fail_and_quit "qemu exited before password prompt"
    }
}

set failed 0
if {$verify_root_shell eq "/bin/mini_sh"} {
    if {$probe_network eq "1"} {
        if {[wait_bootstrap] != 0} {
            puts "\nPANTHERA_BOOT_VERIFY_EXPECT_TIMEOUT: boot_verify_bootstrap"
            set failed 1
        } else {
            observe_network_probe
            if {$strict_ipconfiguration eq "1"} {
                if {$strict_ipconfiguration_boot eq "1" || $strict_ipconfiguration_dhcp_packet eq "1"} {
                    if {[observe_ipconfiguration_dhcp_packet] != 0} { set failed 1 }
                } elseif {$strict_ipconfiguration_prime eq "1" && $strict_ipconfiguration_network ne "1"} {
                    if {[observe_ipconfiguration_prime] != 0} { set failed 1 }
                } elseif {$strict_ipconfiguration_network ne "1"} {
                    if {[observe_ipconfiguration] != 0} { set failed 1 }
                }
                if {$strict_ipconfiguration_boot eq "1"} {
                    if {[observe_ipconfiguration_boot_ready] != 0} { set failed 1 }
                }
                if {$strict_ipconfiguration_network eq "1"} {
                    if {[observe_ipconfiguration_network_ready] != 0} {
                        set failed 1
                    } elseif {[observe_ipconfiguration_daemon_network] != 0} {
                        set failed 1
                    }
                }
            }
        }
    } elseif {[wait_prompt] != 0} {
        puts "\nPANTHERA_BOOT_VERIFY_EXPECT_TIMEOUT: boot_verify_prompt"
        set failed 1
    } elseif {$strict_ipconfiguration eq "1"} {
        if {$strict_ipconfiguration_boot eq "1" || $strict_ipconfiguration_dhcp_packet eq "1"} {
            if {[observe_ipconfiguration_dhcp_packet] != 0} { set failed 1 }
        } elseif {$strict_ipconfiguration_prime eq "1" && $strict_ipconfiguration_network ne "1"} {
            if {[observe_ipconfiguration_prime] != 0} { set failed 1 }
        } elseif {$strict_ipconfiguration_network ne "1"} {
            if {[observe_ipconfiguration] != 0} { set failed 1 }
        }
        if {$strict_ipconfiguration_boot eq "1"} {
            if {[observe_ipconfiguration_boot_ready] != 0} { set failed 1 }
        }
        if {$strict_ipconfiguration_network eq "1"} {
            if {[observe_ipconfiguration_network_ready] != 0} {
                set failed 1
            } elseif {[observe_ipconfiguration_daemon_network] != 0} {
                set failed 1
            }
        }
    }
} else {
    if {[wait_prompt] != 0} {
        fail_and_quit "root shell prompt timeout"
    }
    if {$strict_ipconfiguration_boot eq "1" || $strict_ipconfiguration_network eq "1"} {
        if {$strict_ipconfiguration_boot eq "1" || $strict_ipconfiguration_dhcp_packet eq "1"} {
            if {$strict_ipconfiguration_dhcp_packet eq "1"} {
                if {[observe_ipconfiguration_dhcp_packet] != 0} { set failed 1 }
            } elseif {$strict_ipconfiguration_prime eq "1"} {
                if {[observe_ipconfiguration_prime] != 0} { set failed 1 }
            } else {
                if {[observe_ipconfiguration] != 0} { set failed 1 }
            }
        } elseif {$strict_ipconfiguration_network ne "1"} {
            if {[observe_ipconfiguration] != 0} { set failed 1 }
        }
    }
    if {$strict_ipconfiguration_boot eq "1"} {
        if {[observe_ipconfiguration_boot_ready] != 0} { set failed 1 }
    }
    if {$strict_ipconfiguration_network eq "1"} {
        if {[observe_ipconfiguration_network_ready] != 0} {
            set failed 1
        } elseif {[observe_ipconfiguration_daemon_network] != 0} {
            set failed 1
        }
    } elseif {$strict_ipconfiguration_boot eq "1"} {
        # The boot gate is log-evidence only; the stronger network gate runs shell probes.
    } else {
        if {[run_cmd external_command {/bin/echo PANTHERA_BOOT_VERIFY_EXEC; rc=$?; /usr/bin/printf '__PANTHERA_STATUS_external_command:%d__\n' $rc}] != 0} { set failed 1 }
        if {[run_cmd id {/bin/id; rc=$?; /usr/bin/printf '__PANTHERA_STATUS_id:%d__\n' $rc}] != 0} { set failed 1 }
        if {[run_cmd ifconfig {/sbin/ifconfig en0; rc=$?; /usr/bin/printf '__PANTHERA_STATUS_ifconfig:%d__\n' $rc}] != 0} { set failed 1 }
        if {[run_cmd gateway_ping {/sbin/ping -c 1 10.0.2.2; rc=$?; /usr/bin/printf '__PANTHERA_STATUS_gateway_ping:%d__\n' $rc}] != 0} { set failed 1 }
        if {[run_cmd dns_tcp {/sbin/dnsprobe example.com 10.0.2.3; rc=$?; /usr/bin/printf '__PANTHERA_STATUS_dns_tcp:%d__\n' $rc}] != 0} { set failed 1 }
        if {[run_cmd bootstrap {if [ -x /usr/bin/test_bootstrap ]; then /usr/bin/test_bootstrap; rc=$?; else rc=99; fi; /usr/bin/printf '__PANTHERA_STATUS_bootstrap:%d__\n' $rc}] != 0} { set failed 1 }
    }
    if {$strict_dispatch_timer eq "1" && $dispatch_timer_boot_staged ne "1"} {
        if {[run_dispatch_timer_check] != 0} { set failed 1 }
    }
    if {$strict_mdns eq "1"} {
        if {[run_mdns_check] != 0} { set failed 1 }
    }
}

if {$verify_root_shell ne "/bin/mini_sh"} {
    send -- "exit\r"
    after 1000
}
quit_qemu
expect eof
exit $failed
EXPECT

if [[ "${expect_result}" -eq 0 ]]; then
  echo "interactive-checks-complete" > "${STATUS_PATH}"
else
  echo "interactive-checks-failed:${expect_result}" > "${STATUS_PATH}"
fi

bash userland/libsystem/verify_exports.sh > "${VERIFY_EXPORTS_LOG}" 2>&1 || true

parser_args=(
  --log "${LOG_PATH}"
  --summary "${SUMMARY_PATH}"
  --verify-exports-log "${VERIFY_EXPORTS_LOG}"
)
if [[ "${STRICT_DNS}" == "1" ]]; then
  parser_args+=(--strict-dns)
fi
if [[ "${STRICT_NETWORK}" == "1" ]]; then
  parser_args+=(--strict-network)
fi
if [[ "${STRICT_CONFIGD}" == "1" ]]; then
  parser_args+=(--strict-configd)
fi
if [[ "${STRICT_IPCONFIGURATION}" == "1" ]]; then
  parser_args+=(--strict-ipconfiguration)
fi
if [[ "${STRICT_IPCONFIGURATION_PRIME}" == "1" ]]; then
  parser_args+=(--strict-ipconfiguration-prime)
fi
if [[ "${STRICT_IPCONFIGURATION_DHCP}" == "1" ]]; then
  parser_args+=(--strict-ipconfiguration-dhcp)
fi
if [[ "${STRICT_IPCONFIGURATION_DHCP_PACKET}" == "1" ]]; then
  parser_args+=(--strict-ipconfiguration-dhcp-packet)
fi
if [[ "${STRICT_IPCONFIGURATION_BOOT}" == "1" ]]; then
  parser_args+=(--strict-ipconfiguration-boot)
fi
if [[ "${STRICT_IPCONFIGURATION_NETWORK}" == "1" ]]; then
  parser_args+=(--strict-ipconfiguration-network)
fi
if [[ "${STRICT_DISPATCH_TIMER}" == "1" ]]; then
  parser_args+=(--strict-dispatch-timer)
fi
if [[ "${STRICT_MDNS}" == "1" ]]; then
  parser_args+=(--strict-mdns)
fi

parse_result=0
python3 tools/verify_boot_log.py "${parser_args[@]}" || parse_result=$?

cp -f "${LOG_PATH}" "${LATEST_LOG}"
cp -f "${SUMMARY_PATH}" "${LATEST_SUMMARY}"
cp -f "${STATUS_PATH}" "${LATEST_STATUS}"
cp -f "${VERIFY_EXPORTS_LOG}" "${LATEST_VERIFY_EXPORTS_LOG}"

echo "Boot verification artifact: ${SUMMARY_PATH}"
cat "${SUMMARY_PATH}"

if [[ "${expect_result}" -ne 0 || "${parse_result}" -ne 0 ]]; then
  exit 1
fi
