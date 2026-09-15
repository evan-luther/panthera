#!/usr/bin/env bash
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ARTIFACT_DIR="${PANTHERA_ROOT}/artifacts/release"
TAG="alpha-release-$(date +%Y%m%d-%H%M%S)"
TIMEOUT_SECONDS="${PANTHERA_ALPHA_RELEASE_TIMEOUT:-360}"
PORT="${PANTHERA_ALPHA_RELEASE_SSH_PORT:-2222}"
ROOT_KIND="${PANTHERA_ALPHA_ROOT_KIND:-zfs}"
if [[ "${ROOT_KIND}" == "zfs" ]]; then
  ROOT_DISK="${PANTHERA_ZFS_ROOT_DISK:-${PANTHERA_ROOT}/images/qemu/panthera-zfs-root.img}"
else
  ROOT_DISK="${PANTHERA_ROOT}/images/qemu/panthera-root.img"
fi
ZFS_DATASET="${PANTHERA_ZFS_BOOT:-${PANTHERA_ZFS_ROOT_DATASET:-tank/ROOT/panthera}}"
ROOTDEV="${PANTHERA_ROOTDEV:-uuid}"
REBUILD_ROOTFS=1
RUN_BOOT=1
RUN_OPENSSH=1
RUN_MDNS=1
RUN_SDK=1
RUN_TOOLCHAIN=1
OPENSSH_REPEAT="${PANTHERA_ALPHA_RELEASE_OPENSSH_REPEAT:-3}"

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Runs the Panthera alpha release gate:
  1. build release smoke probes
  2. build runtime libraries needed by the default root image
  3. rebuild the dyld shared cache
  4. rebuild the default configd-owned IPConfiguration root image
  5. run strict boot/network/mDNS/export verification
  6. run OpenSSH remote exec, signal, pselect, mDNS, SFTP, SDK, and toolchain smokes

Options:
  --tag NAME             Artifact tag (default: ${TAG})
  --artifacts DIR        Release artifact directory (default: ${ARTIFACT_DIR})
  --timeout SECONDS      Boot/smoke timeout (default: ${TIMEOUT_SECONDS})
  --port PORT            Host SSH forward port for smokes (default: ${PORT})
  --root-kind KIND       Root filesystem kind for alpha smokes: zfs or hfs
                         (default: ${ROOT_KIND})
  --skip-rebuild-rootfs  Use the existing root image
  --skip-boot            Skip boot_verify.sh
  --skip-openssh         Skip OpenSSH exec/signal/pselect smokes
  --skip-mdns            Skip mDNS-over-SSH smoke
  --skip-sdk             Skip Panthera SDK host/guest smoke
  --skip-toolchain       Skip guest SDK/toolchain smoke
  --openssh-repeat N     SSH exec/SFTP attempts per clean OpenSSH boot (default: ${OPENSSH_REPEAT})
  --help                 Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --tag)
      TAG="$2"
      shift 2
      ;;
    --artifacts)
      ARTIFACT_DIR="$2"
      shift 2
      ;;
    --timeout)
      TIMEOUT_SECONDS="$2"
      shift 2
      ;;
    --port)
      PORT="$2"
      shift 2
      ;;
    --root-kind)
      ROOT_KIND="$2"
      shift 2
      ;;
    --skip-rebuild-rootfs)
      REBUILD_ROOTFS=0
      shift
      ;;
    --skip-boot)
      RUN_BOOT=0
      shift
      ;;
    --skip-openssh)
      RUN_OPENSSH=0
      shift
      ;;
    --skip-mdns)
      RUN_MDNS=0
      shift
      ;;
    --skip-sdk)
      RUN_SDK=0
      shift
      ;;
    --skip-toolchain)
      RUN_TOOLCHAIN=0
      shift
      ;;
    --openssh-repeat)
      OPENSSH_REPEAT="$2"
      shift 2
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
  hfs)
    ROOT_DISK="${PANTHERA_ROOT}/images/qemu/panthera-root.img"
    ;;
  zfs)
    ROOT_DISK="${PANTHERA_ZFS_ROOT_DISK:-${PANTHERA_ROOT}/images/qemu/panthera-zfs-root.img}"
    ;;
  *)
    echo "--root-kind must be hfs or zfs" >&2
    exit 2
    ;;
esac
export PANTHERA_ROOT_DISK_SNAPSHOT=on

if ! [[ "${OPENSSH_REPEAT}" =~ ^[1-9][0-9]*$ ]]; then
  echo "--openssh-repeat must be a positive integer" >&2
  exit 2
fi

mkdir -p "${ARTIFACT_DIR}"
cd "${PANTHERA_ROOT}"

SUMMARY_PATH="${ARTIFACT_DIR}/${TAG}.summary.md"
STEPS_PATH="${ARTIFACT_DIR}/${TAG}.steps.tsv"
RESULT="PASS"
ROWS=()
ARTIFACTS=()

run_step() {
  local name="$1"
  local log_path="$2"
  shift 2

  echo ">>> ${name}"
  echo "    log: ${log_path}"
  if "$@" >"${log_path}" 2>&1; then
    ROWS+=("| ${name} | PASS | \`${log_path}\` |")
    ARTIFACTS+=("- ${name}: \`${log_path}\`")
    printf '%s\tPASS\t%s\n' "${name}" "${log_path}" >>"${STEPS_PATH}"
    return 0
  fi

  RESULT="FAIL"
  ROWS+=("| ${name} | FAIL | \`${log_path}\` |")
  ARTIFACTS+=("- ${name}: \`${log_path}\`")
  printf '%s\tFAIL\t%s\n' "${name}" "${log_path}" >>"${STEPS_PATH}"
  return 1
}

write_summary() {
  local boot_summary="artifacts/boot/${TAG}-boot.summary.md"
  local openssh_return_log="artifacts/boot/${TAG}-openssh-return.ssh.log"
  local openssh_pselect_log="artifacts/boot/${TAG}-openssh-pselect.ssh.log"
  local openssh_signal_log="artifacts/boot/${TAG}-openssh-signal.ssh.log"
  local mdns_log="artifacts/boot/${TAG}-mdns.ssh.log"
  local mdns_sftp_log="artifacts/boot/${TAG}-mdns.sftp.log"
  local sdk_log="artifacts/boot/${TAG}-sdk.ssh.log"
  local toolchain_log="artifacts/boot/${TAG}-toolchain.ssh.log"

  {
    echo "# Panthera Alpha Release Gate"
    echo
    echo "Result: ${RESULT}"
    echo
    echo "## Checks"
    echo
    echo "| Check | Result | Artifact |"
    echo "|---|---|---|"
    for row in "${ROWS[@]}"; do
      echo "${row}"
    done
    echo
    echo "## Artifacts"
    echo
    if [[ ${#ARTIFACTS[@]} -eq 0 ]]; then
      echo "- None."
    else
      for artifact in "${ARTIFACTS[@]}"; do
        echo "${artifact}"
      done
    fi
    if [[ -f "${PANTHERA_ROOT}/${boot_summary}" ]]; then
      echo "- boot summary: \`${boot_summary}\`"
    fi
    if [[ -f "${PANTHERA_ROOT}/${openssh_return_log}" ]]; then
      echo "- OpenSSH return smoke: \`${openssh_return_log}\`"
    fi
    if [[ -f "${PANTHERA_ROOT}/${openssh_pselect_log}" ]]; then
      echo "- OpenSSH pselect smoke: \`${openssh_pselect_log}\`"
    fi
    if [[ -f "${PANTHERA_ROOT}/${openssh_signal_log}" ]]; then
      echo "- OpenSSH signal smoke: \`${openssh_signal_log}\`"
    fi
    if [[ -f "${PANTHERA_ROOT}/${mdns_log}" ]]; then
      echo "- mDNS/OpenSSH smoke: \`${mdns_log}\`"
    fi
    if [[ -f "${PANTHERA_ROOT}/${mdns_sftp_log}" ]]; then
      echo "- mDNS/OpenSSH SFTP: \`${mdns_sftp_log}\`"
    fi
    if [[ -f "${PANTHERA_ROOT}/${sdk_log}" ]]; then
      echo "- Panthera SDK smoke: \`${sdk_log}\`"
    fi
    if [[ -f "${PANTHERA_ROOT}/${toolchain_log}" ]]; then
      echo "- guest toolchain smoke: \`${toolchain_log}\`"
    fi
  } >"${SUMMARY_PATH}"
  cp -f "${SUMMARY_PATH}" "${ARTIFACT_DIR}/latest.summary.md"
}

rm -f "${STEPS_PATH}"

run_step build_ssh_exit_probes \
  "${ARTIFACT_DIR}/${TAG}.build_ssh_exit_probes.log" \
  bash tools/build_ssh_exit_probes.sh || {
    write_summary
    cat "${SUMMARY_PATH}"
    exit 1
  }

run_step build_mdns_dns_sd_probe \
  "${ARTIFACT_DIR}/${TAG}.build_mdns_dns_sd_probe.log" \
  bash tools/build_mdns_dns_sd_probe.sh || {
    write_summary
    cat "${SUMMARY_PATH}"
    exit 1
  }

run_step build_boot_verify_guest \
  "${ARTIFACT_DIR}/${TAG}.build_boot_verify_guest.log" \
  bash tools/build_boot_verify_guest.sh || {
    write_summary
    cat "${SUMMARY_PATH}"
    exit 1
  }

run_step build_coreutils \
  "${ARTIFACT_DIR}/${TAG}.build_coreutils.log" \
  bash userland/coreutils/build_coreutils.sh || {
    write_summary
    cat "${SUMMARY_PATH}"
    exit 1
  }
run_step build_new_coreutils \
  "${ARTIFACT_DIR}/${TAG}.build_new_coreutils.log" \
  bash userland/coreutils/build_new_coreutils.sh || {
    write_summary
    cat "${SUMMARY_PATH}"
    exit 1
  }


run_step build_libiconv \
  "${ARTIFACT_DIR}/${TAG}.build_libiconv.log" \
  bash tools/build_libiconv.sh || {
    write_summary
    cat "${SUMMARY_PATH}"
    exit 1
  }

run_step build_apple_ncurses_target \
  "${ARTIFACT_DIR}/${TAG}.build_apple_ncurses_target.log" \
  bash tools/build_apple_ncurses.sh --target-lib || {
    write_summary
    cat "${SUMMARY_PATH}"
    exit 1
  }

run_step build_openssh \
  "${ARTIFACT_DIR}/${TAG}.build_openssh.log" \
  env PANTHERA_FORCE_REBUILD=1 PANTHERA_OPENSSH_NATIVE_POLL=0 bash userland/openssh/build_openssh.sh || {
    write_summary
    cat "${SUMMARY_PATH}"
    exit 1
  }

run_step diagnostics_audit \
  "${ARTIFACT_DIR}/${TAG}.diagnostics_audit.log" \
  bash tools/audit_diagnostics.sh || {
    write_summary
    cat "${SUMMARY_PATH}"
    exit 1
  }

run_step build_shared_cache \
  "${ARTIFACT_DIR}/${TAG}.build_shared_cache.log" \
  bash tools/build_shared_cache.sh || {
    write_summary
    cat "${SUMMARY_PATH}"
    exit 1
  }
if [[ "${RUN_TOOLCHAIN}" == "1" ]]; then
  run_step build_cctools \
    "${ARTIFACT_DIR}/${TAG}.build_cctools.log" \
    bash userland/cctools/build_cctools.sh || {
      write_summary
      cat "${SUMMARY_PATH}"
      exit 1
    }

  run_step build_ld64 \
    "${ARTIFACT_DIR}/${TAG}.build_ld64.log" \
    bash userland/ld64/build_ld64.sh || {
      write_summary
      cat "${SUMMARY_PATH}"
      exit 1
    }

  run_step build_panthera_sdk \
    "${ARTIFACT_DIR}/${TAG}.build_panthera_sdk.log" \
    bash -c '
      set -euo pipefail
      bash userland/panthera_sdk/build_panthera_sdk.sh
      if [[ "${RUN_SDK:-1}" == "1" ]]; then
        userland/panthera_sdk/bin/panthera-cc -fsyntax-only tools/panthera_sdk_header_probe.c
        userland/panthera_sdk/bin/panthera-cc -O2 tools/panthera_sdk_hello.c -o tools/panthera_sdk_hello
      fi
    ' || {
      write_summary
      cat "${SUMMARY_PATH}"
      exit 1
    }

  run_step build_clang \
    "${ARTIFACT_DIR}/${TAG}.build_clang.log" \
    bash userland/clang/build_clang.sh || {
      write_summary
      cat "${SUMMARY_PATH}"
      exit 1
    }
fi


if [[ "${REBUILD_ROOTFS}" == "1" ]]; then
  if [[ "${ROOT_KIND}" == "zfs" ]]; then
    run_step rebuild_rootfs \
      "${ARTIFACT_DIR}/${TAG}.rebuild_rootfs.log" \
      env PATH="/usr/local/zfs/bin:${PATH}" \
          PANTHERA_ROOT_SHELL=/bin/zsh \
          PANTHERA_DEFAULT_NETWORK_OWNER=ipconfiguration \
          PANTHERA_BOOT_VERIFY_RUN_DAEMON_NETWORK=1 \
          PANTHERA_STAGE_SDK_SMOKE="${RUN_SDK}" \
          PANTHERA_CONFIGD_TRACE=1 \
          PANTHERA_CONFIGD_ROUTE_TRACE=1 \
          PANTHERA_CONFIGD_KERNEL_EVENT_MONITOR_TRACE=1 \
          PANTHERA_SYSTEMCONFIGURATION_TRACE=1 \
          PANTHERA_IOKIT_TRACE=1 \
          PANTHERA_IPCONFIGURATION_TRACE=1 \
        bash rootfs/create_zfs_root_image.sh \
          --tag "${TAG}-root" \
          --timeout "${TIMEOUT_SECONDS}" \
          --root-disk "${ROOT_DISK}" \
          --dataset "${ZFS_DATASET}" \
          --port "${PORT}" \
          --force || {
          write_summary
          cat "${SUMMARY_PATH}"
          exit 1
        }
  else
    run_step rebuild_rootfs \
      "${ARTIFACT_DIR}/${TAG}.rebuild_rootfs.log" \
      env PANTHERA_ROOT_SHELL=/bin/zsh \
          PANTHERA_DEFAULT_NETWORK_OWNER=ipconfiguration \
          PANTHERA_BOOT_VERIFY_RUN_DAEMON_NETWORK=1 \
          PANTHERA_STAGE_SDK_SMOKE="${RUN_SDK}" \
          PANTHERA_CONFIGD_TRACE=1 \
          PANTHERA_CONFIGD_ROUTE_TRACE=1 \
          PANTHERA_CONFIGD_KERNEL_EVENT_MONITOR_TRACE=1 \
          PANTHERA_SYSTEMCONFIGURATION_TRACE=1 \
          PANTHERA_IOKIT_TRACE=1 \
          PANTHERA_IPCONFIGURATION_TRACE=1 \
        bash rootfs/create_hfs_root_image.sh --force --no-build-components || {
          write_summary
          cat "${SUMMARY_PATH}"
          exit 1
        }
  fi
else
  ROWS+=("| rebuild_rootfs | SKIPPED | existing root image |")
fi

run_step fallback_audit \
  "${ARTIFACT_DIR}/${TAG}.fallback_audit.log" \
  python3 tools/audit_ipconfiguration_fallbacks.py || {
    write_summary
    cat "${SUMMARY_PATH}"
    exit 1
  }

if [[ "${ROOT_KIND}" == "hfs" ]]; then
  # Recovery must not inherit a preceding ZFS run's dataset/UUID selection.
  run_step stage_hfs_efi \
    "${ARTIFACT_DIR}/${TAG}.stage_hfs_efi.log" \
    env PANTHERA_ZFS_BOOT= PANTHERA_ROOTDEV= \
      bash boot/efi/stage_phase2_efi.sh || {
      write_summary
      cat "${SUMMARY_PATH}"
      exit 1
    }
fi

if [[ "${RUN_BOOT}" == "1" ]]; then
  boot_verify_args=(
    --timeout "${TIMEOUT_SECONDS}"
    --tag "${TAG}-boot"
    --root-disk "${ROOT_DISK}"
    --root-kind "${ROOT_KIND}"
    --default-ipconfiguration-network
  )
  if [[ "${ROOT_KIND}" == "zfs" ]]; then
    boot_verify_args+=(--zfs-root "${ZFS_DATASET}" --rootdev "${ROOTDEV}")
  fi
  run_step boot_verify \
    "${ARTIFACT_DIR}/${TAG}.boot_verify.log" \
    tools/boot_verify.sh "${boot_verify_args[@]}" || {
      write_summary
      cat "${SUMMARY_PATH}"
      exit 1
    }
else
  ROWS+=("| boot_verify | SKIPPED | disabled by option |")
fi

if [[ "${RUN_OPENSSH}" == "1" ]]; then
  run_step openssh_return_exec_sftp \
    "${ARTIFACT_DIR}/${TAG}.openssh_return.log" \
    env PANTHERA_OPENSSH_CONNECT_TIMEOUT=45 \
      bash tools/smoke_openssh.sh \
        --timeout "${TIMEOUT_SECONDS}" \
        --port "${PORT}" \
        --root-disk "${ROOT_DISK}" \
        --root-kind "${ROOT_KIND}" \
        --tag "${TAG}-openssh-return" \
        --command '/usr/bin/ssh_return_exit_probe && /usr/bin/libsystem_primitives_probe' \
        --marker PANTHERA_LIBSYSTEM_PRIMITIVES_OK \
        --repeat "${OPENSSH_REPEAT}" || {
          write_summary
          cat "${SUMMARY_PATH}"
          exit 1
        }

  run_step openssh_pselect_sigchld_sftp \
    "${ARTIFACT_DIR}/${TAG}.openssh_pselect.log" \
    env PANTHERA_OPENSSH_CONNECT_TIMEOUT=45 \
      bash tools/smoke_openssh.sh \
        --timeout "${TIMEOUT_SECONDS}" \
        --port "${PORT}" \
        --root-disk "${ROOT_DISK}" \
        --root-kind "${ROOT_KIND}" \
        --tag "${TAG}-openssh-pselect" \
        --command /usr/bin/pselect_pipe_eof_probe \
        --marker PSELECT_PIPE_DONE || {
          write_summary
          cat "${SUMMARY_PATH}"
          exit 1
        }

  run_step openssh_signal_trampoline_sftp \
    "${ARTIFACT_DIR}/${TAG}.openssh_signal.log" \
    env PANTHERA_OPENSSH_CONNECT_TIMEOUT=45 \
      bash tools/smoke_openssh.sh \
        --timeout "${TIMEOUT_SECONDS}" \
        --port "${PORT}" \
        --root-disk "${ROOT_DISK}" \
        --root-kind "${ROOT_KIND}" \
        --tag "${TAG}-openssh-signal" \
        --command /usr/bin/signal_unblock_probe \
        --marker SIGNAL_UNBLOCK_DONE || {
          write_summary
          cat "${SUMMARY_PATH}"
          exit 1
        }
else
  ROWS+=("| openssh_smokes | SKIPPED | disabled by option |")
fi

if [[ "${RUN_MDNS}" == "1" ]]; then
  run_step mdns_dns_sd_over_openssh_sftp \
    "${ARTIFACT_DIR}/${TAG}.mdns.log" \
    env PANTHERA_OPENSSH_CONNECT_TIMEOUT=45 \
        PANTHERA_OPENSSH_REMOTE_TIMEOUT=180 \
      bash tools/smoke_openssh.sh \
        --timeout "${TIMEOUT_SECONDS}" \
        --port "${PORT}" \
        --root-disk "${ROOT_DISK}" \
        --root-kind "${ROOT_KIND}" \
        --tag "${TAG}-mdns" \
        --command /usr/bin/mdns_dns_sd_probe \
        --marker PANTHERA_MDNS_DNSSD_PROBE:PASS || {
          write_summary
          cat "${SUMMARY_PATH}"
          exit 1
        }
else
  ROWS+=("| mdns_smoke | SKIPPED | disabled by option |")
fi

if [[ "${RUN_SDK}" == "1" ]]; then
  run_step panthera_sdk_smoke \
    "${ARTIFACT_DIR}/${TAG}.panthera_sdk.log" \
    env PANTHERA_OPENSSH_SMOKE_PORT="${PORT}" \
        PANTHERA_SDK_SMOKE_TAG="${TAG}-sdk" \
        PANTHERA_SDK_SMOKE_TIMEOUT="${TIMEOUT_SECONDS}" \
        PANTHERA_SDK_SMOKE_ROOT_DISK="${ROOT_DISK}" \
        PANTHERA_SDK_SMOKE_ROOT_KIND="${ROOT_KIND}" \
      bash tools/smoke_panthera_sdk.sh \
        --root-disk "${ROOT_DISK}" \
        --root-kind "${ROOT_KIND}" \
        --port "${PORT}" \
        --timeout "${TIMEOUT_SECONDS}" \
        --tag "${TAG}-sdk" \
        --skip-rebuild-rootfs || {
        write_summary
        cat "${SUMMARY_PATH}"
        exit 1
      }
else
  ROWS+=("| panthera_sdk_smoke | SKIPPED | disabled by option |")
fi

if [[ "${RUN_TOOLCHAIN}" == "1" ]]; then
  run_step guest_toolchain_smoke \
    "${ARTIFACT_DIR}/${TAG}.guest_toolchain.log" \
    env PANTHERA_OPENSSH_SMOKE_PORT="${PORT}" \
        PANTHERA_GUEST_TOOLCHAIN_TIMEOUT="${TIMEOUT_SECONDS}" \
        PANTHERA_GUEST_TOOLCHAIN_ROOT_DISK="${ROOT_DISK}" \
        PANTHERA_GUEST_TOOLCHAIN_ROOT_KIND="${ROOT_KIND}" \
      bash tools/smoke_guest_toolchain.sh \
        --root-disk "${ROOT_DISK}" \
        --root-kind "${ROOT_KIND}" \
        --port "${PORT}" \
        --timeout "${TIMEOUT_SECONDS}" \
        --tag "${TAG}-toolchain" || {
        write_summary
        cat "${SUMMARY_PATH}"
        exit 1
      }
else
  ROWS+=("| guest_toolchain_smoke | SKIPPED | disabled by option |")
fi

write_summary
cat "${SUMMARY_PATH}"
