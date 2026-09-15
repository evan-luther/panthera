#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ARTIFACT_DIR="${PANTHERA_ROOT}/artifacts/sdk"
TAG="${PANTHERA_SDK_SMOKE_TAG:-panthera-sdk-smoke-$(date +%Y%m%d-%H%M%S)}"
ROOT_KIND="${PANTHERA_SDK_SMOKE_ROOT_KIND:-${PANTHERA_ROOT_KIND:-zfs}}"
if [[ "${ROOT_KIND}" == "zfs" ]]; then
  ROOT_DISK="${PANTHERA_SDK_SMOKE_ROOT_DISK:-${PANTHERA_ZFS_ROOT_DISK:-${PANTHERA_ROOT}/images/qemu/panthera-zfs-root.img}}"
else
  ROOT_DISK="${PANTHERA_SDK_SMOKE_ROOT_DISK:-${PANTHERA_ROOT}/images/qemu/panthera-root.img}"
fi
DATA_DISK="${PANTHERA_SDK_SMOKE_DATA_DISK:-}"
PORT="${PANTHERA_OPENSSH_SMOKE_PORT:-2222}"
TIMEOUT="${PANTHERA_SDK_SMOKE_TIMEOUT:-360}"
HELLO_SRC="${PANTHERA_ROOT}/tools/panthera_sdk_hello.c"
HEADER_PROBE_SRC="${PANTHERA_ROOT}/tools/panthera_sdk_header_probe.c"
HELLO_BIN="${PANTHERA_ROOT}/tools/panthera_sdk_hello"
MARKER="PANTHERA_SDK_HELLO"
REBUILD_ROOTFS=1

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --tag NAME             Artifact tag for tools/smoke_openssh.sh
  --root-disk PATH       Root disk image to boot
  --root-kind KIND       Root filesystem kind: zfs or hfs (default: ${ROOT_KIND})
  --data-disk PATH       Secondary data disk image to attach
  --port PORT            Host TCP port forwarded to guest sshd
  --timeout SECONDS      Total boot timeout
  --skip-rebuild-rootfs  Use the existing root image
  --help                 Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --tag) TAG="$2"; shift 2 ;;
    --root-disk) ROOT_DISK="$2"; shift 2 ;;
    --root-kind) ROOT_KIND="$2"; shift 2 ;;
    --data-disk) DATA_DISK="$2"; shift 2 ;;
    --port) PORT="$2"; shift 2 ;;
    --timeout) TIMEOUT="$2"; shift 2 ;;
    --skip-rebuild-rootfs) REBUILD_ROOTFS=0; shift ;;
    --help) usage; exit 0 ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

case "${ROOT_KIND}" in
  zfs|hfs) ;;
  *)
    echo "--root-kind must be zfs or hfs" >&2
    exit 2
    ;;
esac

mkdir -p "${ARTIFACT_DIR}"

bash "${PANTHERA_ROOT}/userland/panthera_sdk/build_panthera_sdk.sh"
"${PANTHERA_ROOT}/userland/panthera_sdk/bin/panthera-cc" \
  -fsyntax-only \
  "${HEADER_PROBE_SRC}"
"${PANTHERA_ROOT}/userland/panthera_sdk/bin/panthera-cc" \
  -O2 \
  "${HELLO_SRC}" \
  -o "${HELLO_BIN}"

cp "${HELLO_BIN}" "${ARTIFACT_DIR}/${TAG}-hello"
file "${ARTIFACT_DIR}/${TAG}-hello" > "${ARTIFACT_DIR}/${TAG}-hello.file.txt"
otool -L "${ARTIFACT_DIR}/${TAG}-hello" > "${ARTIFACT_DIR}/${TAG}-hello.otool.txt"

SMOKE_ARGS=(
  --root-disk "${ROOT_DISK}"
  --root-kind "${ROOT_KIND}"
  --timeout "${TIMEOUT}"
  --port "${PORT}"
  --tag "${TAG}"
  --command "/usr/bin/panthera_sdk_hello"
  --marker "${MARKER}"
)

if [[ -n "${DATA_DISK}" ]]; then
  SMOKE_ARGS+=(--data-disk "${DATA_DISK}")
fi

if [[ "${REBUILD_ROOTFS}" == "1" ]]; then
  SMOKE_ARGS+=(--rebuild-rootfs)
fi

PANTHERA_STAGE_SDK_SMOKE=1 \
PANTHERA_ROOT_SHELL=/bin/zsh \
PANTHERA_DEFAULT_NETWORK_OWNER=ipconfiguration \
PANTHERA_OPENSSH_CONNECT_TIMEOUT="${PANTHERA_OPENSSH_CONNECT_TIMEOUT:-45}" \
  bash "${PANTHERA_ROOT}/tools/smoke_openssh.sh" "${SMOKE_ARGS[@]}"

cat <<EOF
Panthera SDK smoke passed:
  binary: ${ARTIFACT_DIR}/${TAG}-hello
  file:   ${ARTIFACT_DIR}/${TAG}-hello.file.txt
  otool:  ${ARTIFACT_DIR}/${TAG}-hello.otool.txt
EOF
