#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ARTIFACT_DIR="${PANTHERA_ROOT}/artifacts/pkgsrc"
ROOT_KIND="${PANTHERA_PKGSRC_ROOT_KIND:-${PANTHERA_ROOT_KIND:-zfs}}"
if [[ "${ROOT_KIND}" == "zfs" ]]; then
  ROOT_DISK="${PANTHERA_ZFS_ROOT_DISK:-${PANTHERA_ROOT}/images/qemu/panthera-zfs-root.img}"
else
  ROOT_DISK="${PANTHERA_ROOT}/images/qemu/panthera-root.img"
fi
ROOT_DISK_SET=0
TAG="${PANTHERA_PKGSRC_BOOTSTRAP_TAG:-pkgsrc-bootstrap-$(date +%Y%m%d-%H%M%S)}"
TARBALL="${ARTIFACT_DIR}/pkgsrc-current-20260518.tar.gz"
TARBALL_URL="${PANTHERA_PKGSRC_TARBALL_URL:-https://cdn.netbsd.org/pub/pkgsrc/current/pkgsrc.tar.gz}"
TIMEOUT="${PANTHERA_PKGSRC_BOOTSTRAP_TIMEOUT:-4800}"
REMOTE_TIMEOUT="${PANTHERA_PKGSRC_BOOTSTRAP_REMOTE_TIMEOUT:-4500}"
PROGRESS_INTERVAL="${PANTHERA_PKGSRC_BOOTSTRAP_PROGRESS_INTERVAL:-60}"
PORT="${PANTHERA_OPENSSH_SMOKE_PORT:-2222}"
DEFAULT_TAR_EXTRACT_FLAGS="--no-xattrs --no-acls --no-mac-metadata --no-same-owner --numeric-owner"
if [[ "${PANTHERA_PKGSRC_BOOTSTRAP_TAR_FLAGS+x}" == "x" ]]; then
  TAR_EXTRACT_FLAGS="${PANTHERA_PKGSRC_BOOTSTRAP_TAR_FLAGS}"
elif [[ "${PANTHERA_PKGSRC_EXTRACT_TAR_FLAGS+x}" == "x" ]]; then
  TAR_EXTRACT_FLAGS="${PANTHERA_PKGSRC_EXTRACT_TAR_FLAGS}"
else
  TAR_EXTRACT_FLAGS="${DEFAULT_TAR_EXTRACT_FLAGS}"
fi
MARKER="PANTHERA_PKGSRC_BOOTSTRAP_OK"
REBUILD_ROOTFS=0

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --tag NAME          Artifact tag for tools/smoke_openssh.sh
  --root-disk PATH    Root disk image to boot
  --root-kind KIND    Root filesystem kind for this smoke: zfs or hfs
                      (default: ${ROOT_KIND})
  --tarball PATH      pkgsrc tarball to upload into /var/tmp/pkgsrc.tar.gz
  --timeout SECONDS   Total boot timeout (default: ${TIMEOUT})
  --remote-timeout S  Remote extraction/bootstrap timeout (default: ${REMOTE_TIMEOUT})
  --port PORT         Host TCP port forwarded to guest port 22 (default: ${PORT})
  --progress-interval S
                      Print progress every S seconds (default: ${PROGRESS_INTERVAL})
  --tar-extract-flags FLAGS
                      Simple flags passed to tar during extraction.
                      Defaults to pkgsrc-safe owner/metadata skip flags:
                      "${DEFAULT_TAR_EXTRACT_FLAGS}"
                      Pass an empty string to preserve archive metadata.
  --rebuild-rootfs    Rebuild the rootfs before staging the pkgsrc tarball
  --help              Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --tag)
      TAG="$2"
      shift 2
      ;;
    --root-disk)
      ROOT_DISK="$2"
      ROOT_DISK_SET=1
      shift 2
      ;;
    --root-kind)
      ROOT_KIND="$2"
      shift 2
      ;;
    --tarball)
      TARBALL="$2"
      shift 2
      ;;
    --timeout)
      TIMEOUT="$2"
      shift 2
      ;;
    --remote-timeout)
      REMOTE_TIMEOUT="$2"
      shift 2
      ;;
    --port)
      PORT="$2"
      shift 2
      ;;
    --progress-interval)
      PROGRESS_INTERVAL="$2"
      shift 2
      ;;
    --tar-extract-flags)
      TAR_EXTRACT_FLAGS="$2"
      shift 2
      ;;
    --rebuild-rootfs)
      REBUILD_ROOTFS=1
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
  zfs|hfs)
    ;;
  *)
    echo "--root-kind must be zfs or hfs" >&2
    exit 2
    ;;
esac

if [[ -n "${TAR_EXTRACT_FLAGS}" ]]; then
  for flag in ${TAR_EXTRACT_FLAGS}; do
    if ! [[ "${flag}" =~ ^[-A-Za-z0-9_./=,:+]+$ ]]; then
      echo "tar extract flags must be simple shell words: ${flag}" >&2
      exit 2
    fi
  done
fi

if [[ "${ROOT_DISK_SET}" == "0" ]]; then
  if [[ "${ROOT_KIND}" == "zfs" ]]; then
    ROOT_DISK="${PANTHERA_ZFS_ROOT_DISK:-${PANTHERA_ROOT}/images/qemu/panthera-zfs-root.img}"
  else
    ROOT_DISK="${PANTHERA_ROOT}/images/qemu/panthera-root.img"
  fi
fi

mkdir -p "${ARTIFACT_DIR}"

if [[ ! -f "${TARBALL}" ]]; then
  curl -L --fail --retry 3 -o "${TARBALL}" "${TARBALL_URL}"
fi

read -r -d '' REMOTE_COMMAND <<REMOTE || true
pkgroot=/tmp/${TAG}-tree
workdir=/tmp/${TAG}-work
log=/tmp/${TAG}.log
progress_interval=${PROGRESS_INTERVAL}
tar_extract_flags='${TAR_EXTRACT_FLAGS}'
: >"\${log}" &&
/bin/test -s /var/tmp/pkgsrc.tar.gz &&
/bin/echo PANTHERA_PKGSRC_STAGE_CLEANUP &&
/bin/rm -rf "\${pkgroot}" "\${workdir}" /usr/pkg &&
/bin/echo PANTHERA_PKGSRC_STAGE_EXTRACT &&
/bin/mkdir -p "\${pkgroot}" "\${workdir}" /usr/pkg &&
/bin/echo PANTHERA_PKGSRC_EXTRACT_START &&
/bin/echo PANTHERA_PKGSRC_EXTRACT_TAR_FLAGS:\${tar_extract_flags:-none} &&
extract_start=\$SECONDS
/usr/bin/tar ${TAR_EXTRACT_FLAGS} -xzf /var/tmp/pkgsrc.tar.gz -C "\${pkgroot}" &
extract_pid=\$!
extract_sample=0
while kill -0 "\${extract_pid}" 2>/dev/null; do
  /bin/sleep "\${progress_interval}"
  extract_sample=\$((extract_sample + 1))
  /bin/echo PANTHERA_PKGSRC_EXTRACT_HEARTBEAT:\${extract_sample}:\$((SECONDS - extract_start))
done
wait "\${extract_pid}"
extract_rc=\$?
/bin/echo PANTHERA_PKGSRC_EXTRACT_RC:\${extract_rc}
/bin/echo PANTHERA_PKGSRC_EXTRACT_ELAPSED:\$((SECONDS - extract_start))
if [ "\${extract_rc}" -ne 0 ]; then
  exit "\${extract_rc}"
fi
/bin/echo PANTHERA_PKGSRC_STAGE_BOOTSTRAP &&
cd "\${pkgroot}/pkgsrc/bootstrap" &&
bootstrap_start=\$SECONDS
PATH=/usr/bin:/bin:/usr/sbin:/sbin /usr/bin/env \
  SH=/bin/sh \
  CC=/usr/bin/cc \
  CPPFLAGS=-I/usr/include \
  LDFLAGS=-L/usr/lib \
  ./bootstrap \
    --prefix=/usr/pkg \
    --pkgdbdir=/usr/pkg/pkgdb \
    --varbase=/usr/pkg/var \
    --sysconfdir=/usr/pkg/etc \
    --workdir="\${workdir}" \
    --ignore-user-check \
    --preserve-path \
    >"\${log}" 2>&1 &
bootstrap_pid=\$!
bootstrap_sample=0
while kill -0 "\${bootstrap_pid}" 2>/dev/null; do
  /bin/sleep "\${progress_interval}"
  bootstrap_sample=\$((bootstrap_sample + 1))
  /bin/echo PANTHERA_PKGSRC_BOOTSTRAP_HEARTBEAT:\${bootstrap_sample}:\$((SECONDS - bootstrap_start))
done
wait "\${bootstrap_pid}"
rc=\$?
/bin/echo PANTHERA_PKGSRC_BOOTSTRAP_RC:\${rc}
/bin/echo PANTHERA_PKGSRC_BOOTSTRAP_ELAPSED:\$((SECONDS - bootstrap_start))
/bin/cat "\${log}"
if [ "\${rc}" -ne 0 ]; then
  exit "\${rc}"
fi
/bin/echo ${MARKER}
REMOTE

SMOKE_ARGS=(
  --root-disk "${ROOT_DISK}"
  --root-kind "${ROOT_KIND}"
  --timeout "${TIMEOUT}"
  --tag "${TAG}"
  --upload "${TARBALL}:/var/tmp/pkgsrc.tar.gz"
  --port "${PORT}"
  --command "${REMOTE_COMMAND}"
  --marker "${MARKER}"
)
if [[ "${REBUILD_ROOTFS}" == "1" ]]; then
  SMOKE_ARGS+=(--rebuild-rootfs)
fi

PANTHERA_OPENSSH_REMOTE_TIMEOUT="${REMOTE_TIMEOUT}" \
PANTHERA_ROOT_SHELL=/bin/zsh \
PANTHERA_DEFAULT_NETWORK_OWNER=ipconfiguration \
  bash "${PANTHERA_ROOT}/tools/smoke_openssh.sh" "${SMOKE_ARGS[@]}"

cat <<EOF
pkgsrc bootstrap smoke passed:
  serial log: ${PANTHERA_ROOT}/artifacts/boot/${TAG}.log
  ssh log:    ${PANTHERA_ROOT}/artifacts/boot/${TAG}.ssh.log
EOF
