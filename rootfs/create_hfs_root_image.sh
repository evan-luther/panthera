#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
TREE_BUILDER="${SCRIPT_DIR}/create_rootfs_tree.sh"
ROOTFS_FIX_OWNERSHIP_SCRIPT="${SCRIPT_DIR}/scripts/fix_ownership.sh"
IMAGE_OWNERSHIP_FIXER="${PANTHERA_ROOT}/tools/fix_cache_ownership.py"

IMAGE_PATH="${PANTHERA_ROOT}/images/qemu/panthera-root.img"
IMAGE_SIZE="${PANTHERA_ROOT_IMAGE_SIZE:-2g}"
VOLUME_NAME="${PANTHERA_ROOT_VOLUME_NAME:-PantheraRoot}"
BUILD_COMPONENTS="${PANTHERA_ROOTFS_BUILD_COMPONENTS:-0}"
FORCE=0
EXPLICIT_ROOTFS_MANIFESTS=()

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --image PATH     Output raw disk image path (default: ${IMAGE_PATH})
  --size SIZE      Disk size for qemu-img create (default: ${IMAGE_SIZE})
  --volume NAME    HFS volume name (default: ${VOLUME_NAME})
  --manifest PATH  Rootfs input manifest to verify before image creation
                  (may be repeated; defaults to Panthera base manifests)
  PANTHERA_DEFAULT_NETWORK_OWNER=netbringup|ipconfiguration
                  Select the default network owner. The default is
                  ipconfiguration; netbringup remains available as a
                  recovery/debug override.
  --build-components
                  Build component artifacts before assembling the image
  --no-build-components
                  Do not build component artifacts before assembly (default)
  --force          Recreate the image if it already exists
  --help           Show this help
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
      EXPLICIT_ROOTFS_MANIFESTS+=("$2")
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
      exit 1
      ;;
  esac
done

if ! command -v qemu-img >/dev/null 2>&1; then
  echo "qemu-img not found in PATH" >&2
  exit 1
fi

if ! command -v hdiutil >/dev/null 2>&1; then
  echo "hdiutil not found in PATH" >&2
  exit 1
fi

if ! command -v diskutil >/dev/null 2>&1; then
  echo "diskutil not found in PATH" >&2
  exit 1
fi

mkdir -p "$(dirname "${IMAGE_PATH}")"

if [[ -f "${IMAGE_PATH}" ]]; then
  if [[ "${FORCE}" != "1" ]]; then
    echo "Image already exists: ${IMAGE_PATH}" >&2
    echo "Use --force to recreate it." >&2
    exit 1
  fi
  rm -f "${IMAGE_PATH}"
fi

qemu-img create -f raw "${IMAGE_PATH}" "${IMAGE_SIZE}" >/dev/null

raw_device=""
mounted_volume="/Volumes/${VOLUME_NAME}"
SHARED_CACHE_STAGED=0

detach_image() {
  if [[ -n "${mounted_volume}" && -d "${mounted_volume}" ]]; then
    diskutil unmount force "${mounted_volume}" >/dev/null 2>&1 || true
  fi
  if [[ -n "${raw_device}" ]]; then
    hdiutil detach "${raw_device}" -force >/dev/null 2>&1 || true
  fi
  raw_device=""
}

cleanup() {
  detach_image
}
trap cleanup EXIT

raw_device=""
for attach_attempt in {1..30}; do
  attach_output="$(hdiutil attach -nomount -imagekey diskimage-class=CRawDiskImage "${IMAGE_PATH}" 2>&1 || true)"
  raw_device="$(printf '%s\n' "${attach_output}" | awk '/^\/dev\// {print $1; exit}')"
  if [[ -n "${raw_device}" ]]; then
    break
  fi
  echo "hdiutil attach attempt ${attach_attempt} failed: ${attach_output}" >&2
  sleep 1
done
if [[ -z "${raw_device}" ]]; then
  echo "Failed to attach raw disk image: ${IMAGE_PATH}" >&2
  exit 1
fi

diskutil partitionDisk "${raw_device}" GPT JHFS+ "${VOLUME_NAME}" R >/dev/null
sleep 1

tree_args=(
  --output "${mounted_volume}"
  --populate-existing
)
if [[ "${BUILD_COMPONENTS}" == "1" ]]; then
  tree_args+=(--build-components)
else
  tree_args+=(--no-build-components)
fi
for manifest in "${EXPLICIT_ROOTFS_MANIFESTS[@]}"; do
  tree_args+=(--manifest "${manifest}")
done

bash "${TREE_BUILDER}" "${tree_args[@]}"

if [[ -f "${mounted_volume}/System/Library/dyld/dyld_shared_cache_x86_64" ]]; then
  SHARED_CACHE_STAGED=1
fi

detach_image

"${ROOTFS_FIX_OWNERSHIP_SCRIPT}" "${IMAGE_PATH}" "${SHARED_CACHE_STAGED}" "${IMAGE_OWNERSHIP_FIXER}"

echo "Created HFS root image: ${IMAGE_PATH}"
echo "Volume name: ${VOLUME_NAME}"
