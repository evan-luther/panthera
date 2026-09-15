#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
HFS_BUILDER="${SCRIPT_DIR}/create_hfs_root_image.sh"

IMAGE_PATH="${PANTHERA_ZFS_INSTALLER_IMAGE:-${PANTHERA_ROOT}/images/qemu/panthera-zfs-installer.img}"
IMAGE_SIZE="${PANTHERA_ZFS_INSTALLER_IMAGE_SIZE:-2g}"
VOLUME_NAME="${PANTHERA_ZFS_INSTALLER_VOLUME_NAME:-PantheraZFSInstaller}"
BUILD_COMPONENTS="${PANTHERA_ROOTFS_BUILD_COMPONENTS:-0}"
FORCE=0
INSTALLER_MANIFESTS=()
EXPLICIT_INSTALLER_MANIFESTS=()
DEFAULT_INSTALLER_MANIFESTS=(
  manifests/base.system
  manifests/networking.system
  manifests/ssh.system
  manifests/experimental-configd.system
  manifests/zfs.system
)

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Creates the temporary Panthera ZFS installer image used to create and populate
a ZFS root pool. The output is HFS-formatted for current boot compatibility,
but it is intentionally not the normal Panthera HFS runtime root image.

Options:
  --image PATH       Output installer image path (default: ${IMAGE_PATH})
  --size SIZE        Installer disk size (default: ${IMAGE_SIZE})
  --volume NAME      HFS volume name (default: ${VOLUME_NAME})
  --manifest PATH    Installer rootfs manifest; may be repeated. Defaults to
                    the explicit ZFS installer manifest set.
  --build-components
                    Build component artifacts before assembling the image
  --no-build-components
                    Do not build component artifacts before assembly (default)
  --force           Recreate the image if it already exists
  --help            Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --image)
      IMAGE_PATH="$2"
      shift 2
      ;;
    --size)
      IMAGE_SIZE="$2"
      shift 2
      ;;
    --volume)
      VOLUME_NAME="$2"
      shift 2
      ;;
    --manifest)
      EXPLICIT_INSTALLER_MANIFESTS+=("$2")
      shift 2
      ;;
    --build-components)
      BUILD_COMPONENTS=1
      shift
      ;;
    --no-build-components)
      BUILD_COMPONENTS=0
      shift
      ;;
    --force)
      FORCE=1
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

if [[ -z "${IMAGE_PATH}" ]]; then
  echo "Installer image path must not be empty" >&2
  exit 2
fi
if [[ "${IMAGE_PATH}" != /* ]]; then
  IMAGE_PATH="${PWD}/${IMAGE_PATH}"
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
elif [[ -n "${PANTHERA_ZFS_INSTALLER_MANIFESTS:-}" ]]; then
  IFS=',' read -r -a manifest_entries <<< "${PANTHERA_ZFS_INSTALLER_MANIFESTS}"
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

hfs_args=(
  --image "${IMAGE_PATH}"
  --size "${IMAGE_SIZE}"
  --volume "${VOLUME_NAME}"
)
if [[ "${BUILD_COMPONENTS}" == "1" ]]; then
  hfs_args+=(--build-components)
else
  hfs_args+=(--no-build-components)
fi
if [[ "${FORCE}" == "1" ]]; then
  hfs_args+=(--force)
fi
for manifest in "${INSTALLER_MANIFESTS[@]}"; do
  hfs_args+=(--manifest "${manifest}")
done

PANTHERA_ROOT_SHELL=/bin/zsh \
PANTHERA_DEFAULT_NETWORK_OWNER=ipconfiguration \
PANTHERA_STAGE_ZFS=1 \
  bash "${HFS_BUILDER}" "${hfs_args[@]}"

cat <<EOF
Created Panthera ZFS installer image:
  image:     ${IMAGE_PATH}
  volume:    ${VOLUME_NAME}
  manifests:
$(printf '    %s\n' "${INSTALLER_MANIFESTS[@]}")
EOF
