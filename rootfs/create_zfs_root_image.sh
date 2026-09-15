#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ARTIFACT_DIR="${PANTHERA_ROOT}/artifacts/boot"
ZFS_INSTALLER_BUILDER="${SCRIPT_DIR}/create_zfs_installer_image.sh"
ZFS_HOST_BUILDER="${SCRIPT_DIR}/create_zfs_root_image_host.sh"

INSTALLER_DISK="${PANTHERA_ZFS_ROOT_INSTALLER_DISK:-${PANTHERA_ZFS_ROOT_CONTROL_DISK:-${PANTHERA_ZFS_INSTALLER_IMAGE:-${PANTHERA_ROOT}/images/qemu/panthera-zfs-installer.img}}}"
ROOT_DISK="${PANTHERA_ZFS_ROOT_DISK:-${PANTHERA_ROOT}/images/qemu/panthera-zfs-root.img}"
SOURCE_ROOT_DISK="${PANTHERA_ZFS_ROOT_SOURCE_DISK:-${PANTHERA_ROOT}/images/qemu/panthera-zfs-root.img}"
ROOT_SIZE="${PANTHERA_ZFS_ROOT_SIZE:-4g}"
DATASET="${PANTHERA_ZFS_ROOT_DATASET:-tank/ROOT/panthera}"
SOURCE_DATASET="${PANTHERA_ZFS_ROOT_SOURCE_DATASET:-tank/ROOT/panthera}"
CONTROL_DATASET="${PANTHERA_ZFS_ROOT_CONTROL_DATASET:-installer/ROOT/panthera}"
SOURCE_ROOTDEV="${PANTHERA_ZFS_ROOT_SOURCE_ROOTDEV:-uuid}"
CONTROL_ROOTDEV="${PANTHERA_ZFS_ROOT_CONTROL_ROOTDEV:-uuid}"
TAG="${PANTHERA_ZFS_ROOT_TAG:-zfs-root-image-$(date +%Y%m%d-%H%M%S)}"
PAYLOAD_TAR="${PANTHERA_ZFS_ROOT_PAYLOAD_TAR:-}"
PORT="${PANTHERA_ZFS_ROOT_PORT:-2225}"
TIMEOUT="${PANTHERA_ZFS_ROOT_TIMEOUT:-420}"
REMOTE_TIMEOUT="${PANTHERA_ZFS_ROOT_REMOTE_TIMEOUT:-900}"
BUILDER_MODE="${PANTHERA_ZFS_ROOT_BUILDER_MODE:-zfs-control}"
REBUILD_INSTALLER=1
CREATE_ROOT=1
FORCE=0
DEFAULT_INSTALLER_MANIFESTS=(
  manifests/base.system
  manifests/networking.system
  manifests/ssh.system
  manifests/experimental-configd.system
  manifests/zfs.system
)
INSTALLER_MANIFESTS=()
EXPLICIT_INSTALLER_MANIFESTS=()

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Creates a Panthera ZFS root image. The default path is zfs-control, which uses
a booted Panthera ZFS root as the builder and never depends on HFS or host ZFS
administrative mounts. Host-direct and HFS-installer paths are explicit modes.

Options:
  --tag NAME             Artifact tag prefix (default: ${TAG})
  --artifacts DIR        Artifact directory (default: ${ARTIFACT_DIR})
  --installer-disk PATH  Temporary ZFS installer image used only to create
                         and populate the target root pool
  --control-disk PATH    Compatibility alias for --installer-disk
  --root-disk PATH       ZFS root image to create/populate
  --root-size SIZE       Size for a newly-created ZFS root disk (default: ${ROOT_SIZE})
  --dataset DATASET      ZFS boot dataset (default: ${DATASET})
  --payload-tar PATH     Host-built rootfs payload archive to extract into ZFS
  --source-root-disk P   Existing ZFS root image used to create a ZFS control
                         image in zfs-control mode
  --source-dataset D     Existing source boot dataset (default: ${SOURCE_DATASET})
  --source-rootdev NAME  XNU rootdev used while booting source with a data disk
                         (default: ${SOURCE_ROOTDEV})
  --control-dataset D    ZFS control boot dataset (default: ${CONTROL_DATASET})
  --control-rootdev NAME XNU rootdev used while booting control with a data disk
                         (default: ${CONTROL_ROOTDEV})
  --builder-mode MODE    zfs-control, direct, or installer (default: ${BUILDER_MODE})
                         zfs-control boots an existing ZFS root to create a
                         ZFS-only control image, then boots that control image
                         to populate the target root.
                         direct uses host OpenZFS and never boots HFS.
                         installer is explicit compatibility mode.
  --installer-manifest P Rootfs manifest for the temporary installer
                         image; may be repeated. Defaults to an explicit
                         ZFS installer set, not the normal HFS image defaults.
  --control-manifest P   Compatibility alias for --installer-manifest
  --port PORT            Host TCP port forwarded to installer guest sshd
  --timeout SECONDS      Installer boot timeout (default: ${TIMEOUT})
  --remote-timeout S     Installer SSH command timeout (default: ${REMOTE_TIMEOUT})
  --reuse-installer      Do not rebuild the installer image
  --reuse-control        Compatibility alias for --reuse-installer
  --reuse-root-disk      Do not recreate the ZFS root disk
  --force                Replace existing generated images/payload
  --help                 Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --tag) TAG="$2"; shift 2 ;;
    --artifacts) ARTIFACT_DIR="$2"; shift 2 ;;
    --installer-disk|--control-disk) INSTALLER_DISK="$2"; shift 2 ;;
    --root-disk) ROOT_DISK="$2"; shift 2 ;;
    --source-root-disk) SOURCE_ROOT_DISK="$2"; shift 2 ;;
    --root-size) ROOT_SIZE="$2"; shift 2 ;;
    --dataset) DATASET="$2"; shift 2 ;;
    --source-dataset) SOURCE_DATASET="$2"; shift 2 ;;
    --source-rootdev) SOURCE_ROOTDEV="$2"; shift 2 ;;
    --control-dataset) CONTROL_DATASET="$2"; shift 2 ;;
    --control-rootdev) CONTROL_ROOTDEV="$2"; shift 2 ;;
    --payload-tar) PAYLOAD_TAR="$2"; shift 2 ;;
    --builder-mode) BUILDER_MODE="$2"; shift 2 ;;
    --installer-manifest|--control-manifest) EXPLICIT_INSTALLER_MANIFESTS+=("$2"); shift 2 ;;
    --port) PORT="$2"; shift 2 ;;
    --timeout) TIMEOUT="$2"; shift 2 ;;
    --remote-timeout) REMOTE_TIMEOUT="$2"; shift 2 ;;
    --reuse-installer|--reuse-control) REBUILD_INSTALLER=0; shift ;;
    --reuse-root-disk) CREATE_ROOT=0; shift ;;
    --force) FORCE=1; shift ;;
    --help) usage; exit 0 ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ "${DATASET}" != */* ]]; then
  echo "--dataset must name a pool and filesystem, for example tank/ROOT/panthera" >&2
  exit 2
fi
POOL="${DATASET%%/*}"

case "${BUILDER_MODE}" in
  zfs-control|direct|installer)
    ;;
  *)
    echo "--builder-mode must be zfs-control, direct, or installer" >&2
    exit 2
    ;;
esac

if [[ "${CONTROL_DATASET}" != */* ]]; then
  echo "--control-dataset must name a pool and filesystem, for example installer/ROOT/panthera" >&2
  exit 2
fi
if [[ "${SOURCE_DATASET}" != */* ]]; then
  echo "--source-dataset must name a pool and filesystem, for example tank/ROOT/panthera" >&2
  exit 2
fi

if [[ "${ARTIFACT_DIR}" != /* ]]; then
  ARTIFACT_DIR="${PWD}/${ARTIFACT_DIR}"
fi
if [[ "${INSTALLER_DISK}" != /* ]]; then
  INSTALLER_DISK="${PWD}/${INSTALLER_DISK}"
fi
if [[ "${ROOT_DISK}" != /* ]]; then
  ROOT_DISK="${PWD}/${ROOT_DISK}"
fi
if [[ "${SOURCE_ROOT_DISK}" != /* ]]; then
  SOURCE_ROOT_DISK="${PWD}/${SOURCE_ROOT_DISK}"
fi
if [[ -z "${PAYLOAD_TAR}" ]]; then
  PAYLOAD_TAR="${ARTIFACT_DIR}/${TAG}-rootfs-payload.tar.gz"
elif [[ "${PAYLOAD_TAR}" != /* ]]; then
  PAYLOAD_TAR="${PWD}/${PAYLOAD_TAR}"
fi

add_installer_manifest() {
  local manifest="$1"

  if [[ "${manifest}" = /* ]]; then
    INSTALLER_MANIFESTS+=("${manifest}")
  else
    INSTALLER_MANIFESTS+=("${PANTHERA_ROOT}/${manifest}")
  fi
}

if [[ "${#EXPLICIT_INSTALLER_MANIFESTS[@]}" -gt 0 ]]; then
  for manifest_entry in "${EXPLICIT_INSTALLER_MANIFESTS[@]}"; do
    add_installer_manifest "${manifest_entry}"
  done
elif [[ -n "${PANTHERA_ZFS_INSTALLER_MANIFESTS:-${PANTHERA_ZFS_CONTROL_MANIFESTS:-}}" ]]; then
  IFS=',' read -r -a manifest_entries <<< "${PANTHERA_ZFS_INSTALLER_MANIFESTS:-${PANTHERA_ZFS_CONTROL_MANIFESTS:-}}"
  for manifest_entry in "${manifest_entries[@]}"; do
    manifest_entry="${manifest_entry## }"
    manifest_entry="${manifest_entry%% }"
    [[ -z "${manifest_entry}" ]] && continue
    add_installer_manifest "${manifest_entry}"
  done
else
  for manifest_entry in "${DEFAULT_INSTALLER_MANIFESTS[@]}"; do
    add_installer_manifest "${manifest_entry}"
  done
fi

if ! command -v ruby >/dev/null 2>&1; then
  echo "ruby is required to create the ZFS GPT root disk image" >&2
  exit 1
fi
if pgrep -fl 'qemu-system-x86_64|run_phase2_serial.sh' >/dev/null 2>&1; then
  echo "A Panthera/QEMU process is already running; stop it before creating a ZFS root image." >&2
  pgrep -fl 'qemu-system-x86_64|run_phase2_serial.sh' >&2 || true
  exit 1
fi

mkdir -p "${ARTIFACT_DIR}" "$(dirname "${INSTALLER_DISK}")" "$(dirname "${ROOT_DISK}")" "$(dirname "${PAYLOAD_TAR}")"

if [[ "${BUILDER_MODE}" == "direct" ]]; then
  host_args=(
    --tag "${TAG}"
    --artifacts "${ARTIFACT_DIR}"
    --root-disk "${ROOT_DISK}"
    --root-size "${ROOT_SIZE}"
    --dataset "${DATASET}"
    --payload-tar "${PAYLOAD_TAR}"
  )
  if [[ "${CREATE_ROOT}" != "1" ]]; then
    host_args+=(--reuse-root-disk)
  fi
  if [[ "${FORCE}" == "1" ]]; then
    host_args+=(--force)
  fi
  exec bash "${ZFS_HOST_BUILDER}" "${host_args[@]}"
fi

if [[ ! -f "${PAYLOAD_TAR}" || "${FORCE}" == "1" ]]; then
  env \
    PANTHERA_ROOT_SHELL="${PANTHERA_ROOT_SHELL:-/bin/zsh}" \
    PANTHERA_DEFAULT_NETWORK_OWNER="${PANTHERA_DEFAULT_NETWORK_OWNER:-ipconfiguration}" \
    PANTHERA_STAGE_ZFS=1 \
    bash "${PANTHERA_ROOT}/rootfs/create_rootfs_payload_tar.sh" \
      --output "${PAYLOAD_TAR}" \
      --force \
      --no-build-components
fi
if [[ ! -f "${PAYLOAD_TAR}" ]]; then
  echo "Payload archive not found: ${PAYLOAD_TAR}" >&2
  exit 1
fi

if [[ "${BUILDER_MODE}" == "zfs-control" ]]; then
  CONTROL_POOL="${CONTROL_DATASET%%/*}"
  SOURCE_POOL="${SOURCE_DATASET%%/*}"

  if [[ ! -f "${SOURCE_ROOT_DISK}" ]]; then
    echo "Source ZFS root image not found: ${SOURCE_ROOT_DISK}" >&2
    exit 1
  fi

  if [[ "${REBUILD_INSTALLER}" == "1" ]]; then
    if [[ -f "${INSTALLER_DISK}" && "${FORCE}" != "1" ]]; then
      echo "ZFS control image already exists: ${INSTALLER_DISK}" >&2
      echo "Use --force or --reuse-control." >&2
      exit 1
    fi
    ruby "${PANTHERA_ROOT}/tools/create_zfs_gpt_image.rb" \
      --image "${INSTALLER_DISK}" \
      --size "${ROOT_SIZE}" \
      --label PantheraZFSControl >/dev/null

    read -r -d '' CREATE_CONTROL_COMMAND <<REMOTE || true
set -e
/bin/echo PANTHERA_ZFS_CONTROL_CREATE_BEGIN
# Native ZFS can import from a different disk than XNU's boot hint.
rootdev=\$(/sbin/zpool status -P '${SOURCE_POOL}' | while read -r name rest; do
  case "\$name" in /dev/disk[01]s1) printf '%s\n' "\${name#/dev/}" ;; esac
done)
root_path="/dev/\$rootdev"
case "\$rootdev" in
  disk0s1) target=/dev/disk1s1 ;;
  disk1s1) target=/dev/disk0s1 ;;
  *) target= ;;
esac
if [ -n "\$target" ]; then candidate_count=1; else candidate_count=0; fi
/bin/echo PANTHERA_ZFS_CONTROL_ROOTDEV:\$rootdev
/bin/echo PANTHERA_ZFS_CONTROL_CANDIDATE_COUNT:\$candidate_count
/bin/echo PANTHERA_ZFS_CONTROL_TARGET:\$target
if [ "\$candidate_count" -ne 1 ] || [ -z "\$target" ] || [ "\$target" = "\$root_path" ]; then
  /bin/echo PANTHERA_ZFS_CONTROL_DEVICES_BEGIN
  /bin/ls -l /dev/disk* 2>/dev/null || true
  /bin/echo PANTHERA_ZFS_CONTROL_DEVICES_END
  exit 42
fi
/bin/rm -rf /zcontrol
/bin/mkdir -p /zcontrol
/sbin/zpool create -f -o cachefile=none -R /zcontrol -m none '${CONTROL_POOL}' "\$target"
/sbin/zfs create -p -o mountpoint=/ -o atime=off '${CONTROL_DATASET}'
/sbin/zpool set bootfs='${CONTROL_DATASET}' '${CONTROL_POOL}'
/sbin/zpool get bootfs '${CONTROL_POOL}'
/bin/test -d /zcontrol
/bin/test -x /usr/bin/bsdtar
/bin/test -f /tmp/panthera-control-payload.tar.gz
/bin/echo PANTHERA_ZFS_CONTROL_PAYLOAD_EXTRACT_ENTER
/usr/bin/bsdtar -C /zcontrol --no-xattrs --no-acls --no-mac-metadata -xpf /tmp/panthera-control-payload.tar.gz
/bin/mkdir -p /zcontrol/dev /zcontrol/tmp /zcontrol/private/tmp /zcontrol/private/var/run /zcontrol/var/run /zcontrol/var/tmp
/bin/chmod 1777 /zcontrol/tmp /zcontrol/private/tmp /zcontrol/var/tmp
/bin/test -x /zcontrol/sbin/launchd
/bin/test -x /zcontrol/bin/zsh
/bin/test -x /zcontrol/sbin/zpool
/sbin/sync
/sbin/zpool sync '${CONTROL_POOL}'
/sbin/sync
/sbin/zpool export '${CONTROL_POOL}'
/bin/echo PANTHERA_ZFS_CONTROL_CREATE_OK
REMOTE

    # The boot seed is the secondary disk here; the primary disk is output.
    PANTHERA_ROOT_DISK_SNAPSHOT=off \
    PANTHERA_DATA_DISK_SNAPSHOT=on \
    PANTHERA_DATA_DISK_POSITION=secondary-master \
    PANTHERA_OPENSSH_REMOTE_TIMEOUT="${REMOTE_TIMEOUT}" \
      bash "${PANTHERA_ROOT}/tools/smoke_openssh.sh" \
        --root-disk "${INSTALLER_DISK}" \
        --root-kind zfs \
        --zfs-root "${SOURCE_DATASET}" \
        --rootdev "${SOURCE_ROOTDEV}" \
        --data-disk "${SOURCE_ROOT_DISK}" \
        --timeout "${TIMEOUT}" \
        --port "${PORT}" \
        --tag "${TAG}-control-create" \
        --upload "${PAYLOAD_TAR}:/tmp/panthera-control-payload.tar.gz" \
        --command "${CREATE_CONTROL_COMMAND}" \
        --marker "PANTHERA_ZFS_CONTROL_CREATE_OK"
  elif [[ ! -f "${INSTALLER_DISK}" ]]; then
    echo "ZFS control image not found: ${INSTALLER_DISK}" >&2
    exit 1
  fi

  if [[ "${CREATE_ROOT}" == "1" ]]; then
    if [[ -f "${ROOT_DISK}" && "${FORCE}" != "1" ]]; then
      echo "ZFS root image already exists: ${ROOT_DISK}" >&2
      echo "Use --force or --reuse-root-disk." >&2
      exit 1
    fi
    ruby "${PANTHERA_ROOT}/tools/create_zfs_gpt_image.rb" \
      --image "${ROOT_DISK}" \
      --size "${ROOT_SIZE}" \
      --label PantheraZFSRoot >/dev/null
  elif [[ ! -f "${ROOT_DISK}" ]]; then
    echo "ZFS root image not found: ${ROOT_DISK}" >&2
    exit 1
  fi

  read -r -d '' ZFS_CONTROL_TARGET_SNIPPET <<'REMOTE' || true
rootdev=$(/sbin/zpool status -P "$boot_pool" | while read -r name rest; do
  case "$name" in /dev/disk[01]s1) printf '%s\n' "${name#/dev/}" ;; esac
done)
root_path="/dev/$rootdev"
case "$rootdev" in
  disk0s1) target=/dev/disk1s1 ;;
  disk1s1) target=/dev/disk0s1 ;;
  *) target= ;;
esac
if [ -n "$target" ]; then candidate_count=1; else candidate_count=0; fi
/bin/echo PANTHERA_ZFS_ROOT_BUILDER_ROOTDEV:$rootdev
/bin/echo PANTHERA_ZFS_ROOT_CANDIDATE_COUNT:$candidate_count
/bin/echo PANTHERA_ZFS_ROOT_TARGET:$target
if [ "$candidate_count" -ne 1 ] || [ -z "$target" ] || [ "$target" = "$root_path" ]; then
  /bin/echo PANTHERA_ZFS_ROOT_DEVICES_BEGIN
  /bin/ls -l /dev/disk* 2>/dev/null || true
  /bin/echo PANTHERA_ZFS_ROOT_DEVICES_END
  exit 42
fi
REMOTE

  read -r -d '' ZFS_CONTROL_POPULATE_COMMAND <<REMOTE || true
set -e
pool='${POOL}'
dataset='${DATASET}'
/bin/echo PANTHERA_ZFS_ROOT_POPULATE_BEGIN
boot_pool='${CONTROL_POOL}'
${ZFS_CONTROL_TARGET_SNIPPET}
/bin/rm -rf /zroot
/bin/mkdir -p /zroot
/bin/echo PANTHERA_ZFS_ROOT_ZPOOL_CREATE_ENTER
/sbin/zpool create -f -o cachefile=none -R /zroot -m none "\$pool" "\$target"
/sbin/zfs create -p -o mountpoint=/ -o atime=off "\$dataset"
/sbin/zpool set bootfs="\$dataset" "\$pool"
/sbin/zpool get bootfs "\$pool"
/sbin/zfs list -H -o name,mountpoint,mounted "\$dataset"
/bin/test -d /zroot
/bin/test -x /usr/bin/bsdtar
/bin/test -f /tmp/panthera-rootfs-payload.tar.gz
/bin/echo PANTHERA_ZFS_ROOT_PAYLOAD_EXTRACT_ENTER
/usr/bin/bsdtar -C /zroot --no-xattrs --no-acls --no-mac-metadata -xpf /tmp/panthera-rootfs-payload.tar.gz
/bin/mkdir -p /zroot/dev /zroot/tmp /zroot/private/tmp /zroot/private/var/run /zroot/var/run /zroot/var/tmp
/bin/chmod 1777 /zroot/tmp /zroot/private/tmp /zroot/var/tmp
/bin/test -x /zroot/sbin/launchd
/bin/test -x /zroot/bin/zsh
/bin/test -d /zroot/System/Library/LaunchDaemons
/sbin/sync
/bin/echo PANTHERA_ZFS_ROOT_EXPORT_ENTER
/sbin/zpool export "\$pool"
/bin/echo PANTHERA_ZFS_ROOT_POPULATE_OK
REMOTE

  PANTHERA_DATA_DISK_POSITION=secondary-master \
  PANTHERA_OPENSSH_REMOTE_TIMEOUT="${REMOTE_TIMEOUT}" \
    bash "${PANTHERA_ROOT}/tools/smoke_openssh.sh" \
      --root-disk "${INSTALLER_DISK}" \
      --root-kind zfs \
      --zfs-root "${CONTROL_DATASET}" \
      --rootdev "${CONTROL_ROOTDEV}" \
      --data-disk "${ROOT_DISK}" \
      --timeout "${TIMEOUT}" \
      --port "${PORT}" \
      --tag "${TAG}-populate" \
      --upload "${PAYLOAD_TAR}:/tmp/panthera-rootfs-payload.tar.gz" \
      --command "${ZFS_CONTROL_POPULATE_COMMAND}" \
      --marker "PANTHERA_ZFS_ROOT_POPULATE_OK"

  cat <<EOF
Created Panthera ZFS root image:
  builder mode:  zfs-control
  source image:  ${SOURCE_ROOT_DISK}
  control image: ${INSTALLER_DISK}
  root image:    ${ROOT_DISK}
  payload tar:   ${PAYLOAD_TAR}
  dataset:       ${DATASET}
  control dataset: ${CONTROL_DATASET}
  populate log:  ${ARTIFACT_DIR}/${TAG}-populate.log
EOF
  exit 0
fi

if ! command -v hdiutil >/dev/null 2>&1 || ! command -v diskutil >/dev/null 2>&1; then
  echo "hdiutil and diskutil are required to stage the rootfs payload into the installer image" >&2
  exit 1
fi

if [[ "${REBUILD_INSTALLER}" == "1" ]]; then
  if [[ -f "${INSTALLER_DISK}" && "${FORCE}" != "1" ]]; then
    echo "Installer image already exists: ${INSTALLER_DISK}" >&2
    echo "Use --force or --reuse-installer." >&2
    exit 1
  fi
  installer_args=(
    --image "${INSTALLER_DISK}"
    --volume PantheraZFSInstaller
    --force
    --no-build-components
  )
  for manifest in "${INSTALLER_MANIFESTS[@]}"; do
    installer_args+=(--manifest "${manifest}")
  done
  bash "${ZFS_INSTALLER_BUILDER}" "${installer_args[@]}"
elif [[ ! -f "${INSTALLER_DISK}" ]]; then
  echo "Installer image not found: ${INSTALLER_DISK}" >&2
  exit 1
fi

stage_payload_into_control_image() {
  local image_path="$1"
  local payload_path="$2"
  local raw_device=""
  local partition_device=""
  local mounted_volume=""
  local attach_output=""
  local candidate=""

  detach_payload_image() {
    trap - RETURN
    if [[ -n "${mounted_volume}" ]]; then
      diskutil unmount force "${mounted_volume}" >/dev/null 2>&1 || true
    fi
    if [[ -n "${raw_device}" ]]; then
      hdiutil detach "${raw_device}" -force >/dev/null 2>&1 || true
    fi
  }

  attach_output="$(hdiutil attach -nomount -imagekey diskimage-class=CRawDiskImage "${image_path}" 2>&1)"
  raw_device="$(printf '%s\n' "${attach_output}" | awk '/^\/dev\// {print $1; exit}')"
  if [[ -z "${raw_device}" ]]; then
    echo "Failed to attach installer image for payload staging: ${image_path}" >&2
    echo "${attach_output}" >&2
    return 1
  fi
  trap detach_payload_image RETURN

  while IFS= read -r candidate; do
    [[ "${candidate}" == "${raw_device}" ]] && continue
    if diskutil info "${candidate}" 2>/dev/null | grep -Eq 'Type \(Bundle\):.*hfs|File System Personality:.*HFS|Volume Name:.*PantheraRoot'; then
      partition_device="${candidate}"
      break
    fi
  done < <(printf '%s\n' "${attach_output}" | awk '/^\/dev\// {print $1}')
  if [[ -z "${partition_device}" ]]; then
    echo "Failed to find HFS installer partition in image: ${image_path}" >&2
    echo "${attach_output}" >&2
    return 1
  fi

  diskutil mount "${partition_device}" >/dev/null
  mounted_volume="$(diskutil info "${partition_device}" | awk -F': *' '/Mount Point/ {print $2; exit}')"
  if [[ -z "${mounted_volume}" || ! -d "${mounted_volume}" ]]; then
    echo "Failed to locate mounted control volume for ${partition_device}" >&2
    return 1
  fi

  mkdir -p "${mounted_volume}/tmp"
  cp "${payload_path}" "${mounted_volume}/tmp/panthera-rootfs-payload.tar.gz"
  sync
  echo "Staged rootfs payload into installer image: ${image_path}"
}

stage_payload_into_control_image "${INSTALLER_DISK}" "${PAYLOAD_TAR}"

if [[ "${CREATE_ROOT}" == "1" ]]; then
  if [[ -f "${ROOT_DISK}" && "${FORCE}" != "1" ]]; then
    echo "ZFS root image already exists: ${ROOT_DISK}" >&2
    echo "Use --force or --reuse-root-disk." >&2
    exit 1
  fi
  ruby "${PANTHERA_ROOT}/tools/create_zfs_gpt_image.rb" \
    --image "${ROOT_DISK}" \
    --size "${ROOT_SIZE}" \
    --label PantheraZFSRoot >/dev/null
elif [[ ! -f "${ROOT_DISK}" ]]; then
  echo "ZFS root image not found: ${ROOT_DISK}" >&2
  exit 1
fi

bash "${PANTHERA_ROOT}/boot/efi/stage_phase2_efi.sh" --enable-zfs \
  > "${ARTIFACT_DIR}/${TAG}.builder.stage_phase2.log" 2>&1

read -r -d '' CREATE_TARGET_SNIPPET <<'REMOTE' || true
rootdev='__PANTHERA_BSD_ROOTDEV__'
root_path="/dev/$rootdev"
case "$rootdev" in
  disk0s1) target=/dev/disk1s1 ;;
  disk1s1) target=/dev/disk0s1 ;;
  *) target= ;;
esac
if [ -n "$target" ]; then candidate_count=1; else candidate_count=0; fi
/bin/echo PANTHERA_ZFS_ROOT_BUILDER_ROOTDEV:$rootdev
/bin/echo PANTHERA_ZFS_ROOT_CANDIDATE_COUNT:$candidate_count
/bin/echo PANTHERA_ZFS_ROOT_TARGET:$target
if [ "$candidate_count" -ne 1 ] || [ -z "$target" ] || [ "$target" = "$root_path" ]; then
  /bin/echo PANTHERA_ZFS_ROOT_DEVICES_BEGIN
  /bin/ls -l /dev/disk* 2>/dev/null || true
  /bin/echo PANTHERA_ZFS_ROOT_DEVICES_END
  exit 42
fi
REMOTE

read -r -d '' POPULATE_COMMAND <<REMOTE || true
set -e
pool='${POOL}'
dataset='${DATASET}'
/bin/echo PANTHERA_ZFS_ROOT_POPULATE_BEGIN
${CREATE_TARGET_SNIPPET}
/bin/rm -rf /zroot
/bin/mkdir -p /zroot
/bin/echo PANTHERA_ZFS_ROOT_ZPOOL_CREATE_ENTER
/sbin/zpool create -f -o cachefile=none -R /zroot -m none "\$pool" "\$target"
/sbin/zfs create -p -o mountpoint=/ -o atime=off "\$dataset"
/sbin/zpool set bootfs="\$dataset" "\$pool"
/sbin/zpool get bootfs "\$pool"
/sbin/zfs list -H -o name,mountpoint,mounted "\$dataset"
/bin/test -d /zroot
/bin/test -x /usr/bin/bsdtar
/bin/test -f /tmp/panthera-rootfs-payload.tar.gz
/bin/echo PANTHERA_ZFS_ROOT_PAYLOAD_EXTRACT_ENTER
/usr/bin/bsdtar -C /zroot --no-xattrs --no-acls --no-mac-metadata -xpf /tmp/panthera-rootfs-payload.tar.gz
/bin/mkdir -p /zroot/dev /zroot/tmp /zroot/var/run /zroot/var/tmp
/bin/chmod 1777 /zroot/tmp /zroot/var/tmp
/bin/test -x /zroot/sbin/launchd
/bin/test -x /zroot/bin/zsh
/bin/test -d /zroot/System/Library/LaunchDaemons
/sbin/sync
/bin/echo PANTHERA_ZFS_ROOT_EXPORT_ENTER
/sbin/zpool export "\$pool"
/bin/echo PANTHERA_ZFS_ROOT_POPULATE_OK
REMOTE

PANTHERA_OPENSSH_REMOTE_TIMEOUT="${REMOTE_TIMEOUT}" \
PANTHERA_ROOT_SHELL=/bin/zsh \
PANTHERA_DEFAULT_NETWORK_OWNER=ipconfiguration \
  bash "${PANTHERA_ROOT}/tools/smoke_openssh.sh" \
    --root-disk "${INSTALLER_DISK}" \
    --root-kind hfs \
    --data-disk "${ROOT_DISK}" \
    --timeout "${TIMEOUT}" \
    --port "${PORT}" \
    --tag "${TAG}-populate" \
    --command "${POPULATE_COMMAND}" \
    --marker "PANTHERA_ZFS_ROOT_POPULATE_OK"

cat <<EOF
Created Panthera ZFS root image:
  installer image: ${INSTALLER_DISK}
  root image:    ${ROOT_DISK}
  payload tar:   ${PAYLOAD_TAR}
  dataset:       ${DATASET}
  installer manifests:
$(printf '    %s\n' "${INSTALLER_MANIFESTS[@]}")
  populate log:  ${ARTIFACT_DIR}/${TAG}-populate.log
EOF
