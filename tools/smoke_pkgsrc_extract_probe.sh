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
ZFS_DATASET="${PANTHERA_ZFS_BOOT:-${PANTHERA_ZFS_ROOT_DATASET:-tank/ROOT/panthera}}"
ZFS_POOL="${ZFS_DATASET%%/*}"
ROOT_DISK_SET=0
TAG="${PANTHERA_PKGSRC_EXTRACT_TAG:-pkgsrc-extract-probe-$(date +%Y%m%d-%H%M%S)}"
TARBALL="${ARTIFACT_DIR}/pkgsrc-current-20260518.tar.gz"
TARBALL_URL="${PANTHERA_PKGSRC_TARBALL_URL:-https://cdn.netbsd.org/pub/pkgsrc/current/pkgsrc.tar.gz}"
PORT="${PANTHERA_OPENSSH_SMOKE_PORT:-2222}"
TIMEOUT="${PANTHERA_PKGSRC_EXTRACT_TIMEOUT:-3000}"
REMOTE_TIMEOUT="${PANTHERA_PKGSRC_EXTRACT_REMOTE_TIMEOUT:-2700}"
PROGRESS_STEP="${PANTHERA_PKGSRC_EXTRACT_PROGRESS_STEP:-1000}"
PROGRESS_INTERVAL="${PANTHERA_PKGSRC_EXTRACT_PROGRESS_INTERVAL:-30}"
ARCHIVE_MEMBER="${PANTHERA_PKGSRC_EXTRACT_MEMBER:-}"
ACCOUNT_MODE="${PANTHERA_PKGSRC_EXTRACT_ACCOUNT_MODE:-tree-probe}"
ACCOUNT_HELPER="${PANTHERA_PKGSRC_EXTRACT_ACCOUNT_HELPER:-${PANTHERA_ROOT}/tools/pkgsrc_tree_account_probe}"
DEFAULT_TAR_EXTRACT_FLAGS="--no-xattrs --no-acls --no-mac-metadata --no-same-owner --numeric-owner"
if [[ "${PANTHERA_PKGSRC_EXTRACT_TAR_FLAGS+x}" == "x" ]]; then
  TAR_EXTRACT_FLAGS="${PANTHERA_PKGSRC_EXTRACT_TAR_FLAGS}"
else
  TAR_EXTRACT_FLAGS="${DEFAULT_TAR_EXTRACT_FLAGS}"
fi
TAR_LIST_FLAGS="${PANTHERA_PKGSRC_LIST_TAR_FLAGS:-}"
TAR_COMPRESSION="${PANTHERA_PKGSRC_EXTRACT_TAR_COMPRESSION:-gz}"
COUNT_ARCHIVE=1
CLEANUP=0
REBUILD_ROOTFS=0
MARKER="PANTHERA_PKGSRC_EXTRACT_OK"

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
  --remote-timeout S  Remote extraction timeout (default: ${REMOTE_TIMEOUT})
  --port PORT         Host TCP port forwarded to guest port 22 (default: ${PORT})
  --progress-step N   Reserved for archive-entry probes (default: ${PROGRESS_STEP})
  --progress-interval S
                      Print filesystem progress every S seconds (default: ${PROGRESS_INTERVAL})
  --member PATH       Extract only one archive path, for example pkgsrc/devel
  --account-mode MODE Account extracted files with tree-probe or find
                      (default: ${ACCOUNT_MODE})
  --tar-extract-flags FLAGS
                      Simple flags passed to tar during extraction.
                      Defaults to pkgsrc-safe owner/metadata skip flags:
                      "${DEFAULT_TAR_EXTRACT_FLAGS}"
                      Pass an empty string to preserve archive metadata.
  --tar-list-flags FLAGS
                      Extra simple flags passed to tar during archive count
  --tar-compression MODE
                      Archive compression mode: gz or none (default: ${TAR_COMPRESSION})
  --no-count          Skip the pre-extraction tar listing count
  --cleanup           Remove the extracted tree and uploaded tarball before success
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
    --progress-step)
      PROGRESS_STEP="$2"
      shift 2
      ;;
    --progress-interval)
      PROGRESS_INTERVAL="$2"
      shift 2
      ;;
    --member)
      ARCHIVE_MEMBER="$2"
      shift 2
      ;;
    --account-mode)
      ACCOUNT_MODE="$2"
      shift 2
      ;;
    --tar-extract-flags)
      TAR_EXTRACT_FLAGS="$2"
      shift 2
      ;;
    --tar-list-flags)
      TAR_LIST_FLAGS="$2"
      shift 2
      ;;
    --tar-compression)
      TAR_COMPRESSION="$2"
      shift 2
      ;;
    --no-count)
      COUNT_ARCHIVE=0
      shift
      ;;
    --cleanup)
      CLEANUP=1
      shift
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

case "${ACCOUNT_MODE}" in
  tree-probe|find)
    ;;
  *)
    echo "--account-mode must be tree-probe or find" >&2
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
if [[ -n "${TAR_LIST_FLAGS}" ]]; then
  for flag in ${TAR_LIST_FLAGS}; do
    if ! [[ "${flag}" =~ ^[-A-Za-z0-9_./=,:+]+$ ]]; then
      echo "tar list flags must be simple shell words: ${flag}" >&2
      exit 2
    fi
  done
fi
case "${TAR_COMPRESSION}" in
  gz)
    TAR_LIST_ARGS="-tzf"
    TAR_EXTRACT_ARGS="-xzf"
    ;;
  none)
    TAR_LIST_ARGS="-tf"
    TAR_EXTRACT_ARGS="-xf"
    ;;
  *)
    echo "--tar-compression must be gz or none" >&2
    exit 2
    ;;
esac

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

if [[ "${ACCOUNT_MODE}" == "tree-probe" ]]; then
  if [[ ! -x "${ACCOUNT_HELPER}" ]]; then
    bash "${PANTHERA_ROOT}/tools/build_pkgsrc_tree_account_probe.sh"
  fi
  if [[ ! -x "${ACCOUNT_HELPER}" ]]; then
    echo "account helper not executable after build: ${ACCOUNT_HELPER}" >&2
    exit 1
  fi
fi

read -r -d '' REMOTE_COMMAND <<REMOTE || true
pkgroot=/tmp/${TAG}-tree
log=/tmp/${TAG}.extract.log
err=/tmp/${TAG}.extract.err
count_file=/tmp/${TAG}.count.out
files_file=/tmp/${TAG}.files.out
dirs_file=/tmp/${TAG}.dirs.out
du_file=/tmp/${TAG}.du.out
step=${PROGRESS_STEP}
interval=${PROGRESS_INTERVAL}
count_archive=${COUNT_ARCHIVE}
archive_member='${ARCHIVE_MEMBER}'
cleanup=${CLEANUP}
account_mode='${ACCOUNT_MODE}'
tar_extract_flags='${TAR_EXTRACT_FLAGS}'
tar_list_flags='${TAR_LIST_FLAGS}'
tar_list_args='${TAR_LIST_ARGS}'
tar_extract_args='${TAR_EXTRACT_ARGS}'
zfs_pool='${ZFS_POOL}'
zpool_sync_cleanup=$([[ "${ROOT_KIND}" == "zfs" ]] && echo 1 || echo 0)
/bin/rm -rf "\${pkgroot}" "\${log}" "\${err}" "\${count_file}" "\${files_file}" "\${dirs_file}" "\${du_file}" || exit 1
/bin/test -s /var/tmp/pkgsrc.tar.gz || exit 1
/bin/mkdir -p "\${pkgroot}" || exit 1
/bin/echo PANTHERA_PKGSRC_EXTRACT_STAGE:START || exit 1
/bin/echo PANTHERA_PKGSRC_EXTRACT_TAR_FLAGS:\${tar_extract_flags:-none} || exit 1
if [ "\${count_archive}" = "1" ]; then
  /bin/echo PANTHERA_PKGSRC_EXTRACT_STAGE:COUNT || exit 1
  count_start=\$SECONDS
  list_file=/tmp/${TAG}.list.out
  /bin/rm -f "\${list_file}" || exit 1
  (if [ -n "\${archive_member}" ]; then
    /usr/bin/tar ${TAR_LIST_FLAGS} ${TAR_LIST_ARGS} /var/tmp/pkgsrc.tar.gz "\${archive_member}" >"\${list_file}"
  else
    /usr/bin/tar ${TAR_LIST_FLAGS} ${TAR_LIST_ARGS} /var/tmp/pkgsrc.tar.gz >"\${list_file}"
  fi) &
  count_pid=\$!
  count_sample=0
  while kill -0 "\${count_pid}" 2>/dev/null; do
    /bin/sleep "\${interval}"
    count_sample=\$((count_sample + 1))
    /bin/echo PANTHERA_PKGSRC_EXTRACT_COUNT_HEARTBEAT:\${count_sample}:\$((SECONDS - count_start))
  done
  wait "\${count_pid}" || exit 1
  /usr/bin/awk 'END { print NR }' "\${list_file}" >"\${count_file}" || exit 1
  expected=\$(/bin/cat "\${count_file}") || exit 1
  /bin/echo PANTHERA_PKGSRC_EXTRACT_EXPECTED:\${expected} || exit 1
  /bin/echo PANTHERA_PKGSRC_EXTRACT_COUNT_ELAPSED:\$((SECONDS - count_start)) || exit 1
fi
/bin/echo PANTHERA_PKGSRC_EXTRACT_STAGE:EXTRACT || exit 1
(if [ -n "\${archive_member}" ]; then
  /usr/bin/tar ${TAR_EXTRACT_FLAGS} ${TAR_EXTRACT_ARGS} /var/tmp/pkgsrc.tar.gz -C "\${pkgroot}" "\${archive_member}"
else
  /usr/bin/tar ${TAR_EXTRACT_FLAGS} ${TAR_EXTRACT_ARGS} /var/tmp/pkgsrc.tar.gz -C "\${pkgroot}"
fi) >"\${log}" 2>"\${err}" &
tar_pid=\$!
sample=0
extract_start=\$SECONDS
while kill -0 "\${tar_pid}" 2>/dev/null; do
  /bin/sleep "\${interval}"
  sample=\$((sample + 1))
  elapsed=\$((SECONDS - extract_start))
  /bin/echo PANTHERA_PKGSRC_EXTRACT_HEARTBEAT:\${sample}:\${elapsed}
done
wait "\${tar_pid}"
tar_rc=\$?
/bin/echo PANTHERA_PKGSRC_EXTRACT_TAR_RC:\${tar_rc}
/bin/echo PANTHERA_PKGSRC_EXTRACT_TAR_ELAPSED:\$((SECONDS - extract_start))
/bin/cat "\${err}"
if [ "\${tar_rc}" -ne 0 ]; then
  exit "\${tar_rc}"
fi
/bin/echo PANTHERA_PKGSRC_EXTRACT_STAGE:SYNC
sync_start=\$SECONDS
/sbin/sync
/bin/echo PANTHERA_PKGSRC_EXTRACT_SYNC_ELAPSED:\$((SECONDS - sync_start))
/bin/echo PANTHERA_PKGSRC_EXTRACT_STAGE:FINAL_COUNT
account_root="\${pkgroot}/pkgsrc"
if [ -n "\${archive_member}" ]; then
  account_root="\${pkgroot}/\${archive_member%/}"
fi
/bin/echo PANTHERA_PKGSRC_EXTRACT_ACCOUNT_ROOT:\${account_root}
if [ "\${account_mode}" = "tree-probe" ]; then
  /bin/chmod 755 /var/tmp/pkgsrc_tree_account_probe || exit 1
  /var/tmp/pkgsrc_tree_account_probe "\${account_root}" keep 10000 || exit 1
else
  files_start=\$SECONDS
  /usr/bin/find "\${account_root}" -type f 2>/dev/null | /usr/bin/awk 'END { print NR }' >"\${files_file}" &
  files_pid=\$!
  files_sample=0
  while kill -0 "\${files_pid}" 2>/dev/null; do
    /bin/sleep "\${interval}"
    files_sample=\$((files_sample + 1))
    /bin/echo PANTHERA_PKGSRC_EXTRACT_FILES_HEARTBEAT:\${files_sample}:\$((SECONDS - files_start))
  done
  wait "\${files_pid}" || exit 1
  files=\$(/bin/cat "\${files_file}") || exit 1
  /bin/echo PANTHERA_PKGSRC_EXTRACT_FILES_ELAPSED:\$((SECONDS - files_start))
  dirs_start=\$SECONDS
  /usr/bin/find "\${account_root}" -type d 2>/dev/null | /usr/bin/awk 'END { print NR }' >"\${dirs_file}" &
  dirs_pid=\$!
  dirs_sample=0
  while kill -0 "\${dirs_pid}" 2>/dev/null; do
    /bin/sleep "\${interval}"
    dirs_sample=\$((dirs_sample + 1))
    /bin/echo PANTHERA_PKGSRC_EXTRACT_DIRS_HEARTBEAT:\${dirs_sample}:\$((SECONDS - dirs_start))
  done
  wait "\${dirs_pid}" || exit 1
  dirs=\$(/bin/cat "\${dirs_file}") || exit 1
  /bin/echo PANTHERA_PKGSRC_EXTRACT_DIRS_ELAPSED:\$((SECONDS - dirs_start))
  du_start=\$SECONDS
  /usr/bin/du -sk "\${account_root}" 2>/dev/null | /usr/bin/awk '{print \$1}' >"\${du_file}" &
  du_pid=\$!
  du_sample=0
  while kill -0 "\${du_pid}" 2>/dev/null; do
    /bin/sleep "\${interval}"
    du_sample=\$((du_sample + 1))
    /bin/echo PANTHERA_PKGSRC_EXTRACT_DU_HEARTBEAT:\${du_sample}:\$((SECONDS - du_start))
  done
  wait "\${du_pid}" || exit 1
  size_kb=\$(/bin/cat "\${du_file}") || exit 1
  /bin/echo PANTHERA_PKGSRC_EXTRACT_DU_ELAPSED:\$((SECONDS - du_start))
  /bin/echo PANTHERA_PKGSRC_EXTRACT_FILES:\${files}
  /bin/echo PANTHERA_PKGSRC_EXTRACT_DIRS:\${dirs}
  /bin/echo PANTHERA_PKGSRC_EXTRACT_SIZE_KB:\${size_kb}
fi
if [ "\${cleanup}" = "1" ]; then
  /bin/echo PANTHERA_PKGSRC_EXTRACT_STAGE:CLEANUP
  cleanup_start=\$SECONDS
  /bin/rm -rf "\${pkgroot}" /var/tmp/pkgsrc.tar.gz || exit 1
  /bin/echo PANTHERA_PKGSRC_EXTRACT_CLEANUP_ELAPSED:\$((SECONDS - cleanup_start))
  if [ -e "\${pkgroot}" ]; then
    /bin/echo PANTHERA_PKGSRC_EXTRACT_CLEANUP_LEFTOVER:\${pkgroot}
    exit 1
  fi
  if [ -e /var/tmp/pkgsrc.tar.gz ]; then
    /bin/echo PANTHERA_PKGSRC_EXTRACT_CLEANUP_LEFTOVER:/var/tmp/pkgsrc.tar.gz
    exit 1
  fi
  if [ "\${zpool_sync_cleanup}" = "1" ]; then
    /bin/echo PANTHERA_PKGSRC_EXTRACT_STAGE:ZPOOL_SYNC
    zpool_sync_start=\$SECONDS
    /sbin/sync || exit 1
    /sbin/zpool sync "\${zfs_pool}" || exit 1
    /sbin/sync || exit 1
    /bin/sleep 5
    /bin/echo PANTHERA_PKGSRC_EXTRACT_ZPOOL_SYNC_ELAPSED:\$((SECONDS - zpool_sync_start))
  fi
  /bin/echo PANTHERA_PKGSRC_EXTRACT_STAGE:CLEANUP_SYNC
  cleanup_sync_start=\$SECONDS
  /sbin/sync || exit 1
  /bin/echo PANTHERA_PKGSRC_EXTRACT_CLEANUP_SYNC_ELAPSED:\$((SECONDS - cleanup_sync_start))
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
if [[ "${ACCOUNT_MODE}" == "tree-probe" ]]; then
  SMOKE_ARGS+=(--upload "${ACCOUNT_HELPER}:/var/tmp/pkgsrc_tree_account_probe")
fi
if [[ "${REBUILD_ROOTFS}" == "1" ]]; then
  SMOKE_ARGS+=(--rebuild-rootfs)
fi

PANTHERA_OPENSSH_REMOTE_TIMEOUT="${REMOTE_TIMEOUT}" \
PANTHERA_ROOT_SHELL=/bin/zsh \
PANTHERA_DEFAULT_NETWORK_OWNER=ipconfiguration \
  bash "${PANTHERA_ROOT}/tools/smoke_openssh.sh" "${SMOKE_ARGS[@]}"

cat <<EOF
pkgsrc extraction probe passed:
  serial log: ${PANTHERA_ROOT}/artifacts/boot/${TAG}.log
  ssh log:    ${PANTHERA_ROOT}/artifacts/boot/${TAG}.ssh.log
EOF
