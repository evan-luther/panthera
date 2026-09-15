#!/usr/bin/env bash
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ARTIFACT_DIR="${PANTHERA_ROOT}/artifacts/release"
TAG="zfs-release-$(date +%Y%m%d-%H%M%S)"
TIMEOUT_SECONDS="${PANTHERA_ZFS_RELEASE_TIMEOUT:-360}"
REMOTE_TIMEOUT_SECONDS="${PANTHERA_ZFS_RELEASE_REMOTE_TIMEOUT:-220}"
PORT="${PANTHERA_ZFS_RELEASE_SSH_PORT:-2225}"
RUN_DATA_POOL=1
RUN_HYBRID=1
RUN_MDNS=1
RUN_SDK=1
RUN_TOOLCHAIN=1
RUN_ROOT=1
RUN_ROOT_UPLOAD=1
FORCE=1
HYBRID_ROOT_DISK="${PANTHERA_ZFS_HYBRID_ROOT_DISK:-${PANTHERA_ROOT}/images/qemu/panthera-zfs-hybrid-root.img}"
HYBRID_DATA_DISK="${PANTHERA_ZFS_HYBRID_DATA_DISK:-${PANTHERA_ROOT}/images/qemu/panthera-zfs-hybrid-data.img}"
SOURCE_ROOT_DISK="${PANTHERA_ZFS_ROOT_SOURCE_DISK:-${PANTHERA_ROOT}/images/qemu/panthera-zfs-root.img}"
ROOT_INSTALLER_DISK="${PANTHERA_ZFS_ROOT_INSTALLER_DISK:-${PANTHERA_ZFS_ROOT_CONTROL_DISK:-${PANTHERA_ZFS_INSTALLER_IMAGE:-${PANTHERA_ROOT}/images/qemu/panthera-zfs-installer.img}}}"
ROOT_DISK="${PANTHERA_ZFS_ROOT_DISK:-${PANTHERA_ROOT}/images/qemu/panthera-zfs-root.img}"
ROOT_DATASET="${PANTHERA_ZFS_ROOT_DATASET:-tank/ROOT/panthera}"
ROOTDEV="${PANTHERA_ZFS_ROOTDEV:-uuid}"
ROOT_REMOTE_TIMEOUT_SECONDS="${PANTHERA_ZFS_ROOT_REMOTE_TIMEOUT:-900}"
ROOT_BUILDER_MODE="${PANTHERA_ZFS_ROOT_BUILDER_MODE:-zfs-control}"
ROOT_UPLOAD_PROBE_32K="${PANTHERA_ZFS_ROOT_UPLOAD_PROBE_32K:-${PANTHERA_ROOT}/artifacts/pkgsrc/upload-probes/32k.bin}"

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Runs the current opt-in Panthera ZFS storage gate:
  1. verify diagnostic/ZFS staging hygiene
  2. verify third-party ZFS provenance checksums
  3. rebuild mount tools and shared cache
  4. run the Z4 secondary data-pool persistence smoke
  5. run the Z5 hybrid writable-state layout smoke
  6. run mDNS, SDK, and guest toolchain smokes against the Z5 hybrid layout
  7. build/populate/boot a ZFS root image
  8. run statfs, mDNS, SDK, and guest toolchain smokes against ZFS root

Options:
  --tag NAME             Artifact tag (default: ${TAG})
  --artifacts DIR        Release artifact directory (default: ${ARTIFACT_DIR})
  --source-root-disk PATH Existing immutable ZFS root image used as builder
                         source in zfs-control mode (default: ${SOURCE_ROOT_DISK})
  --installer-disk PATH  Temporary ZFS control/installer image used to create
                         and populate the target root (default: ${ROOT_INSTALLER_DISK})
  --control-disk PATH    Compatibility alias for --installer-disk
  --root-disk PATH       Target output ZFS root image to create, populate, and boot
                         (default: ${ROOT_DISK})
  --timeout SECONDS      Per-boot timeout (default: ${TIMEOUT_SECONDS})
  --remote-timeout SEC   Per-SSH-command timeout (default: ${REMOTE_TIMEOUT_SECONDS})
  --port PORT            Host SSH forward port for smokes (default: ${PORT})
  --skip-data-pool       Skip the Z4 data-pool persistence smoke
  --skip-hybrid          Skip the Z5 hybrid writable-state smoke
  --skip-mdns            Skip mDNS-over-SSH smokes
  --skip-sdk             Skip Panthera SDK smokes
  --skip-toolchain       Skip guest toolchain smokes
  --skip-root            Skip the Z6/Z7 ZFS-root bootstrap/runtime smoke
  --skip-root-upload     Skip the ZFS-root default 32K OpenSSH/SFTP upload smoke
  --only-root            Run only the ZFS-root bootstrap/runtime smoke after
                         hygiene/provenance/build checks
  --reuse-images         Reuse existing smoke images instead of recreating them
  --help                 Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --tag) TAG="$2"; shift 2 ;;
    --artifacts) ARTIFACT_DIR="$2"; shift 2 ;;
    --source-root-disk)
      if [[ $# -lt 2 || -z "${2:-}" ]]; then
        echo "--source-root-disk requires a path argument" >&2
        exit 2
      fi
      SOURCE_ROOT_DISK="$2"
      shift 2
      ;;
    --installer-disk|--control-disk)
      if [[ $# -lt 2 || -z "${2:-}" ]]; then
        echo "$1 requires a path argument" >&2
        exit 2
      fi
      ROOT_INSTALLER_DISK="$2"
      shift 2
      ;;
    --root-disk)
      if [[ $# -lt 2 || -z "${2:-}" ]]; then
        echo "--root-disk requires a path argument" >&2
        exit 2
      fi
      ROOT_DISK="$2"
      shift 2
      ;;
    --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
    --remote-timeout)
      if [[ $# -lt 2 || -z "${2:-}" ]]; then
        echo "--remote-timeout requires a seconds argument" >&2
        exit 2
      fi
      REMOTE_TIMEOUT_SECONDS="$2"
      ROOT_REMOTE_TIMEOUT_SECONDS="$2"
      shift 2
      ;;
    --port) PORT="$2"; shift 2 ;;
    --skip-data-pool) RUN_DATA_POOL=0; shift ;;
    --skip-hybrid) RUN_HYBRID=0; shift ;;
    --skip-mdns) RUN_MDNS=0; shift ;;
    --skip-sdk) RUN_SDK=0; shift ;;
    --skip-toolchain) RUN_TOOLCHAIN=0; shift ;;
    --skip-root) RUN_ROOT=0; shift ;;
    --skip-root-upload) RUN_ROOT_UPLOAD=0; shift ;;
    --only-root)
      RUN_DATA_POOL=0
      RUN_HYBRID=0
      RUN_ROOT=1
      shift
      ;;
    --reuse-images) FORCE=0; shift ;;
    --help) usage; exit 0 ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done
export PANTHERA_ROOT_DISK_SNAPSHOT=on

mkdir -p "${ARTIFACT_DIR}"
cd "${PANTHERA_ROOT}"

if [[ "${RUN_ROOT}" == "1" && "${ROOT_BUILDER_MODE}" == "zfs-control" ]]; then
  if [[ ! -f "${SOURCE_ROOT_DISK}" ]]; then
    echo "Source ZFS root image not found: ${SOURCE_ROOT_DISK}" >&2
    exit 1
  fi
fi

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
  {
    echo "# Panthera ZFS Storage Gate"
    echo
    echo "Result: ${RESULT}"
    echo
    echo "Scope: Z4 secondary data-pool persistence, Z5 hybrid writable-state layout, and Z6/Z7 ZFS-root bootstrap/runtime verification when enabled."
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
  } >"${SUMMARY_PATH}"
  cp -f "${SUMMARY_PATH}" "${ARTIFACT_DIR}/latest-zfs.summary.md"
}

rm -f "${STEPS_PATH}"

run_step diagnostics_audit \
  "${ARTIFACT_DIR}/${TAG}.diagnostics_audit.log" \
  bash tools/audit_diagnostics.sh || {
    write_summary
    cat "${SUMMARY_PATH}"
    exit 1
  }

run_step zfs_staging_audit \
  "${ARTIFACT_DIR}/${TAG}.zfs_staging_audit.log" \
  bash tools/audit_zfs_staging.sh || {
    write_summary
    cat "${SUMMARY_PATH}"
    exit 1
  }

if [[ "${RUN_ROOT}" == "1" && "${ROOT_BUILDER_MODE}" == "direct" ]]; then
  run_step host_openzfs_audit \
    "${ARTIFACT_DIR}/${TAG}.host_openzfs_audit.log" \
    bash tools/audit_host_openzfs.sh || {
      write_summary
      cat "${SUMMARY_PATH}"
      exit 1
    }
else
  ROWS+=("| host_openzfs_audit | SKIPPED | root disabled or builder mode is not direct |")
fi

run_step verify_third_party_checksums \
  "${ARTIFACT_DIR}/${TAG}.verify_third_party_checksums.log" \
  bash tools/verify_third_party_checksums.sh || {
    write_summary
    cat "${SUMMARY_PATH}"
    exit 1
  }

run_step build_mount_tools \
  "${ARTIFACT_DIR}/${TAG}.build_mount_tools.log" \
  bash userland/mount_tools/build_mount_tools.sh || {
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

run_step build_zfs_root_statfs_probe \
  "${ARTIFACT_DIR}/${TAG}.build_zfs_root_statfs_probe.log" \
  userland/panthera_sdk/bin/panthera-cc -O2 tools/zfs_root_statfs_probe.c -o tools/zfs_root_statfs_probe || {
    write_summary
    cat "${SUMMARY_PATH}"
    exit 1
  }

if [[ "${RUN_SDK}" == "1" ]]; then
  run_step build_panthera_sdk_smoke_binary \
    "${ARTIFACT_DIR}/${TAG}.build_panthera_sdk_smoke_binary.log" \
    bash -c '
      set -euo pipefail
      bash userland/panthera_sdk/build_panthera_sdk.sh
      userland/panthera_sdk/bin/panthera-cc -fsyntax-only tools/panthera_sdk_header_probe.c
      userland/panthera_sdk/bin/panthera-cc -O2 tools/panthera_sdk_hello.c -o tools/panthera_sdk_hello
      file tools/panthera_sdk_hello
      otool -L tools/panthera_sdk_hello
    ' || {
      write_summary
      cat "${SUMMARY_PATH}"
      exit 1
    }
else
  ROWS+=("| build_panthera_sdk_smoke_binary | SKIPPED | disabled by option |")
fi

if [[ "${RUN_ROOT}" == "1" && "${RUN_ROOT_UPLOAD}" == "1" ]]; then
  run_step prepare_zfs_root_upload_probe_32k \
    "${ARTIFACT_DIR}/${TAG}.prepare_zfs_root_upload_probe_32k.log" \
    bash -c '
      set -euo pipefail
      probe="$1"
      mkdir -p "$(dirname "${probe}")"
      if [[ ! -f "${probe}" || "$(wc -c < "${probe}" | tr -d " ")" != "32768" ]]; then
        dd if=/dev/zero of="${probe}" bs=32768 count=1
      fi
      test "$(wc -c < "${probe}" | tr -d " ")" = "32768"
    ' bash "${ROOT_UPLOAD_PROBE_32K}" || {
      write_summary
      cat "${SUMMARY_PATH}"
      exit 1
    }
else
  ROWS+=("| prepare_zfs_root_upload_probe_32k | SKIPPED | root or upload check disabled |")
fi

if [[ "${RUN_DATA_POOL}" == "1" ]]; then
  data_args=(
    --tag "${TAG}-data"
    --timeout "${TIMEOUT_SECONDS}"
    --remote-timeout "${REMOTE_TIMEOUT_SECONDS}"
    --port "${PORT}"
  )
  if [[ "${FORCE}" == "1" ]]; then
    data_args+=(--force)
  else
    data_args+=(--reuse-rootfs --reuse-data-disk)
  fi
  run_step zfs_data_pool_smoke \
    "${ARTIFACT_DIR}/${TAG}.zfs_data_pool_smoke.log" \
    bash tools/smoke_zfs_data_pool.sh "${data_args[@]}" || {
      write_summary
      cat "${SUMMARY_PATH}"
      exit 1
    }
else
  ROWS+=("| zfs_data_pool_smoke | SKIPPED | disabled by option |")
fi

if [[ "${RUN_HYBRID}" == "1" ]]; then
  hybrid_args=(
    --tag "${TAG}-hybrid"
    --root-disk "${HYBRID_ROOT_DISK}"
    --data-disk "${HYBRID_DATA_DISK}"
    --timeout "${TIMEOUT_SECONDS}"
    --remote-timeout "${REMOTE_TIMEOUT_SECONDS}"
    --port "${PORT}"
  )
  if [[ "${FORCE}" == "1" ]]; then
    hybrid_args+=(--force)
  else
    hybrid_args+=(--reuse-rootfs --reuse-data-disk)
  fi
  run_step zfs_hybrid_layout_smoke \
    "${ARTIFACT_DIR}/${TAG}.zfs_hybrid_layout_smoke.log" \
    env PANTHERA_STAGE_SDK_SMOKE="${RUN_SDK}" \
      bash tools/smoke_zfs_hybrid_layout.sh "${hybrid_args[@]}" || {
      write_summary
      cat "${SUMMARY_PATH}"
      exit 1
    }
else
  ROWS+=("| zfs_hybrid_layout_smoke | SKIPPED | disabled by option |")
fi

if [[ "${RUN_HYBRID}" == "1" && "${RUN_MDNS}" == "1" ]]; then
  run_step zfs_hybrid_mdns_smoke \
    "${ARTIFACT_DIR}/${TAG}.zfs_hybrid_mdns.log" \
    env PANTHERA_OPENSSH_REMOTE_TIMEOUT=180 \
      bash tools/smoke_openssh.sh \
        --root-disk "${HYBRID_ROOT_DISK}" \
        --root-kind hfs \
        --data-disk "${HYBRID_DATA_DISK}" \
        --timeout "${TIMEOUT_SECONDS}" \
        --port "${PORT}" \
        --tag "${TAG}-hybrid-mdns" \
        --command /usr/bin/mdns_dns_sd_probe \
        --marker PANTHERA_MDNS_DNSSD_PROBE:PASS || {
          write_summary
          cat "${SUMMARY_PATH}"
          exit 1
        }
elif [[ "${RUN_HYBRID}" != "1" ]]; then
  ROWS+=("| zfs_hybrid_mdns_smoke | SKIPPED | hybrid disabled |")
else
  ROWS+=("| zfs_hybrid_mdns_smoke | SKIPPED | disabled by option |")
fi

if [[ "${RUN_HYBRID}" == "1" && "${RUN_SDK}" == "1" ]]; then
  run_step zfs_hybrid_sdk_smoke \
    "${ARTIFACT_DIR}/${TAG}.zfs_hybrid_sdk.log" \
    env PANTHERA_OPENSSH_SMOKE_PORT="${PORT}" \
      bash tools/smoke_panthera_sdk.sh \
        --root-disk "${HYBRID_ROOT_DISK}" \
        --root-kind hfs \
        --data-disk "${HYBRID_DATA_DISK}" \
        --port "${PORT}" \
        --timeout "${TIMEOUT_SECONDS}" \
        --tag "${TAG}-hybrid-sdk" \
        --skip-rebuild-rootfs || {
          write_summary
          cat "${SUMMARY_PATH}"
          exit 1
        }
elif [[ "${RUN_HYBRID}" != "1" ]]; then
  ROWS+=("| zfs_hybrid_sdk_smoke | SKIPPED | hybrid disabled |")
else
  ROWS+=("| zfs_hybrid_sdk_smoke | SKIPPED | disabled by option |")
fi

if [[ "${RUN_HYBRID}" == "1" && "${RUN_TOOLCHAIN}" == "1" ]]; then
  run_step zfs_hybrid_guest_toolchain_smoke \
    "${ARTIFACT_DIR}/${TAG}.zfs_hybrid_guest_toolchain.log" \
    env PANTHERA_OPENSSH_REMOTE_TIMEOUT=420 \
      bash tools/smoke_guest_toolchain.sh \
        --root-disk "${HYBRID_ROOT_DISK}" \
        --root-kind hfs \
        --data-disk "${HYBRID_DATA_DISK}" \
        --port "${PORT}" \
        --timeout "${TIMEOUT_SECONDS}" \
        --tag "${TAG}-hybrid-toolchain" || {
          write_summary
          cat "${SUMMARY_PATH}"
          exit 1
        }
elif [[ "${RUN_HYBRID}" != "1" ]]; then
  ROWS+=("| zfs_hybrid_guest_toolchain_smoke | SKIPPED | hybrid disabled |")
else
  ROWS+=("| zfs_hybrid_guest_toolchain_smoke | SKIPPED | disabled by option |")
fi

if [[ "${RUN_ROOT}" == "1" ]]; then
  root_args=(
    --tag "${TAG}-root"
    --source-root-disk "${SOURCE_ROOT_DISK}"
    --installer-disk "${ROOT_INSTALLER_DISK}"
    --root-disk "${ROOT_DISK}"
    --dataset "${ROOT_DATASET}"
    --rootdev "${ROOTDEV}"
    --builder-mode "${ROOT_BUILDER_MODE}"
    --timeout "${TIMEOUT_SECONDS}"
    --remote-timeout "${ROOT_REMOTE_TIMEOUT_SECONDS}"
    --port "${PORT}"
  )
  if [[ "${FORCE}" == "1" ]]; then
    root_args+=(--force)
  else
    root_args+=(--reuse-installer --reuse-root-disk)
  fi
  run_step zfs_root_bootstrap_smoke \
    "${ARTIFACT_DIR}/${TAG}.zfs_root_bootstrap_smoke.log" \
    env PANTHERA_STAGE_SDK_SMOKE="${RUN_SDK}" \
      bash tools/smoke_zfs_root_bootstrap.sh "${root_args[@]}" || {
      write_summary
      cat "${SUMMARY_PATH}"
      exit 1
    }

  run_step zfs_root_statfs_ssh_smoke \
    "${ARTIFACT_DIR}/${TAG}.zfs_root_statfs_ssh.log" \
    env PANTHERA_FIX_CACHE_OWNERSHIP=0 \
        PANTHERA_OPENSSH_REMOTE_TIMEOUT=180 \
      bash tools/smoke_openssh.sh \
        --root-disk "${ROOT_DISK}" \
        --timeout "${TIMEOUT_SECONDS}" \
        --port "${PORT}" \
        --tag "${TAG}-root-statfs" \
        --command "/bin/sh -c 'set -e; /usr/bin/zfs_root_statfs_probe; /sbin/zpool get -H -o value bootfs ${ROOT_DATASET%%/*}; /sbin/zfs list -H -o name,mountpoint,mounted ${ROOT_DATASET}; /bin/echo PANTHERA_ZFS_ROOT_RUNTIME_OK'" \
        --marker PANTHERA_ZFS_ROOT_RUNTIME_OK || {
          write_summary
          cat "${SUMMARY_PATH}"
          exit 1
        }

  if [[ "${RUN_ROOT_UPLOAD}" == "1" ]]; then
    run_step zfs_root_default_32k_upload_smoke \
      "${ARTIFACT_DIR}/${TAG}.zfs_root_default_32k_upload.log" \
      env PANTHERA_FIX_CACHE_OWNERSHIP=0 \
          PANTHERA_OPENSSH_REMOTE_TIMEOUT=180 \
        bash tools/smoke_openssh.sh \
          --root-disk "${ROOT_DISK}" \
          --timeout "${TIMEOUT_SECONDS}" \
          --port "${PORT}" \
          --tag "${TAG}-root-upload-32k" \
          --upload "${ROOT_UPLOAD_PROBE_32K}:/var/tmp/panthera-zfs-root-upload-32k.bin" \
          --command "/bin/sh -c 'set -e; /bin/test -s /var/tmp/panthera-zfs-root-upload-32k.bin; /usr/bin/stat -f PANTHERA_ZFS_ROOT_UPLOAD_32K_SIZE:%z /var/tmp/panthera-zfs-root-upload-32k.bin; /bin/echo PANTHERA_ZFS_ROOT_UPLOAD_32K_OK'" \
          --marker PANTHERA_ZFS_ROOT_UPLOAD_32K_OK || {
            write_summary
            cat "${SUMMARY_PATH}"
            exit 1
          }
  else
    ROWS+=("| zfs_root_default_32k_upload_smoke | SKIPPED | disabled by option |")
  fi

  if [[ "${RUN_MDNS}" == "1" ]]; then
    run_step zfs_root_mdns_smoke \
      "${ARTIFACT_DIR}/${TAG}.zfs_root_mdns.log" \
      env PANTHERA_FIX_CACHE_OWNERSHIP=0 \
          PANTHERA_OPENSSH_REMOTE_TIMEOUT=180 \
        bash tools/smoke_openssh.sh \
          --root-disk "${ROOT_DISK}" \
          --timeout "${TIMEOUT_SECONDS}" \
          --port "${PORT}" \
          --tag "${TAG}-root-mdns" \
          --command /usr/bin/mdns_dns_sd_probe \
          --marker PANTHERA_MDNS_DNSSD_PROBE:PASS || {
            write_summary
            cat "${SUMMARY_PATH}"
            exit 1
          }
  else
    ROWS+=("| zfs_root_mdns_smoke | SKIPPED | disabled by option |")
  fi

  if [[ "${RUN_SDK}" == "1" ]]; then
    run_step zfs_root_sdk_smoke \
      "${ARTIFACT_DIR}/${TAG}.zfs_root_sdk.log" \
      env PANTHERA_FIX_CACHE_OWNERSHIP=0 \
          PANTHERA_OPENSSH_SMOKE_PORT="${PORT}" \
        bash tools/smoke_panthera_sdk.sh \
          --root-disk "${ROOT_DISK}" \
          --port "${PORT}" \
          --timeout "${TIMEOUT_SECONDS}" \
          --tag "${TAG}-root-sdk" \
          --skip-rebuild-rootfs || {
            write_summary
            cat "${SUMMARY_PATH}"
            exit 1
          }
  else
    ROWS+=("| zfs_root_sdk_smoke | SKIPPED | disabled by option |")
  fi

  if [[ "${RUN_TOOLCHAIN}" == "1" ]]; then
    run_step zfs_root_guest_toolchain_smoke \
      "${ARTIFACT_DIR}/${TAG}.zfs_root_guest_toolchain.log" \
      env PANTHERA_FIX_CACHE_OWNERSHIP=0 \
          PANTHERA_OPENSSH_REMOTE_TIMEOUT=420 \
        bash tools/smoke_guest_toolchain.sh \
          --root-disk "${ROOT_DISK}" \
          --port "${PORT}" \
          --timeout "${TIMEOUT_SECONDS}" \
          --tag "${TAG}-root-toolchain" || {
            write_summary
            cat "${SUMMARY_PATH}"
            exit 1
          }
  else
    ROWS+=("| zfs_root_guest_toolchain_smoke | SKIPPED | disabled by option |")
  fi
else
  ROWS+=("| zfs_root_bootstrap_smoke | SKIPPED | disabled by option |")
  ROWS+=("| zfs_root_statfs_ssh_smoke | SKIPPED | disabled by option |")
  ROWS+=("| zfs_root_default_32k_upload_smoke | SKIPPED | root disabled |")
  ROWS+=("| zfs_root_mdns_smoke | SKIPPED | root disabled |")
  ROWS+=("| zfs_root_sdk_smoke | SKIPPED | root disabled |")
  ROWS+=("| zfs_root_guest_toolchain_smoke | SKIPPED | root disabled |")
fi

write_summary
cat "${SUMMARY_PATH}"
