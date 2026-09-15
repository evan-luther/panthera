#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ARTIFACT_DIR="${PANTHERA_ROOT}/artifacts/boot"
ROOT_KIND="${PANTHERA_OPENSSH_ROOT_KIND:-${PANTHERA_ROOT_KIND:-zfs}}"
if [[ "${ROOT_KIND}" == "zfs" ]]; then
  ROOT_DISK="${PANTHERA_ZFS_ROOT_DISK:-${PANTHERA_ROOT}/images/qemu/panthera-zfs-root.img}"
else
  ROOT_DISK="${PANTHERA_ROOT}/images/qemu/panthera-root.img"
fi
ROOT_DISK_SET=0
ZFS_DATASET="${PANTHERA_ZFS_BOOT:-${PANTHERA_ZFS_ROOT_DATASET:-tank/ROOT/panthera}}"
ROOTDEV="${PANTHERA_ROOTDEV:-uuid}"
DATA_DISK="${PANTHERA_DATA_DISK:-}"
TAG="openssh-smoke-$(date +%Y%m%d-%H%M%S)"
PORT="${PANTHERA_OPENSSH_SMOKE_PORT:-2222}"
TIMEOUT_SECONDS="${PANTHERA_OPENSSH_SMOKE_TIMEOUT:-240}"
CONNECT_TIMEOUT_SECONDS="${PANTHERA_OPENSSH_CONNECT_TIMEOUT:-60}"
REMOTE_EXEC_TIMEOUT_SECONDS="${PANTHERA_OPENSSH_REMOTE_TIMEOUT:-45}"
KEYSCAN_TIMEOUT_SECONDS="${PANTHERA_OPENSSH_KEYSCAN_TIMEOUT:-5}"
KEYSCAN_WATCHDOG_SECONDS="${PANTHERA_OPENSSH_KEYSCAN_WATCHDOG:-8}"
KEYSCAN_TYPES="${PANTHERA_OPENSSH_KEYSCAN_TYPES:-ed25519}"
POST_KEYSCAN_DELAY_SECONDS="${PANTHERA_OPENSSH_POST_KEYSCAN_DELAY:-1}"
SFTP_EXTRA_ARGS="${PANTHERA_OPENSSH_SFTP_ARGS:-}"
REMOTE_COMMAND="${PANTHERA_OPENSSH_SMOKE_COMMAND:-/bin/echo PANTHERA_OPENSSH_REMOTE_EXEC}"
REMOTE_MARKER="${PANTHERA_OPENSSH_SMOKE_MARKER:-PANTHERA_OPENSSH_REMOTE_EXEC}"
REPEAT="${PANTHERA_OPENSSH_REPEAT:-1}"
REBUILD_ROOTFS=0
UPLOADS=()

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --root-disk PATH    Root disk image to boot (default: ${ROOT_DISK})
  --root-kind KIND    Root filesystem kind: zfs or hfs (default: ${ROOT_KIND})
  --zfs-root DATASET  ZFS boot dataset (default: ${ZFS_DATASET})
  --rootdev NAME      XNU root device for ZFS root (default: ${ROOTDEV})
  --data-disk PATH    Secondary data disk image to attach during boot
  --port PORT         Host TCP port forwarded to guest port 22 (default: ${PORT})
  --timeout SECONDS   Total startup timeout (default: ${TIMEOUT_SECONDS})
  --tag NAME          Artifact tag (default: ${TAG})
  --command COMMAND   Remote command for exec smoke (default: ${REMOTE_COMMAND})
  --marker TEXT       Marker expected from remote command (default: ${REMOTE_MARKER})
  --repeat COUNT      Run SSH exec and SFTP checks COUNT times after one clean boot (default: ${REPEAT})
  --upload LOCAL:REMOTE
                      Upload a local file into the guest with SFTP before the
                      remote command. May be repeated; remote parent must exist.
  --sftp-args ARGS    Extra simple arguments passed to host sftp, e.g.
                      "-B 32768 -R 64" (default: ${SFTP_EXTRA_ARGS:-none})
  --rebuild-rootfs    Rebuild the default rootfs before boot
  --help              Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --root-disk)
      ROOT_DISK="$2"
      ROOT_DISK_SET=1
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
    --data-disk)
      DATA_DISK="$2"
      shift 2
      ;;
    --port)
      PORT="$2"
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
    --command)
      REMOTE_COMMAND="$2"
      shift 2
      ;;
    --marker)
      REMOTE_MARKER="$2"
      shift 2
      ;;
    --repeat)
      REPEAT="$2"
      shift 2
      ;;
    --upload)
      UPLOADS+=("$2")
      shift 2
      ;;
    --sftp-args)
      SFTP_EXTRA_ARGS="$2"
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

if [[ "${ROOT_DISK_SET}" == "0" ]]; then
  if [[ "${ROOT_KIND}" == "zfs" ]]; then
    ROOT_DISK="${PANTHERA_ZFS_ROOT_DISK:-${PANTHERA_ROOT}/images/qemu/panthera-zfs-root.img}"
  else
    ROOT_DISK="${PANTHERA_ROOT}/images/qemu/panthera-root.img"
  fi
fi

if ! [[ "${REPEAT}" =~ ^[1-9][0-9]*$ ]]; then
  echo "--repeat must be a positive integer" >&2
  exit 2
fi
if [[ -n "${SFTP_EXTRA_ARGS}" ]]; then
  for arg in ${SFTP_EXTRA_ARGS}; do
    if ! [[ "${arg}" =~ ^[-A-Za-z0-9_./=,:+]+$ ]]; then
      echo "sftp args must be simple shell words: ${arg}" >&2
      exit 2
    fi
  done
fi

if ! command -v expect >/dev/null 2>&1; then
  echo "expect is required for the OpenSSH password-auth smoke" >&2
  exit 1
fi
if ! command -v ssh >/dev/null 2>&1 || ! command -v sftp >/dev/null 2>&1; then
  echo "host ssh and sftp clients are required for the OpenSSH smoke" >&2
  exit 1
fi
if nc -z 127.0.0.1 "${PORT}" >/dev/null 2>&1; then
  echo "port ${PORT} is already in use" >&2
  exit 1
fi

mkdir -p "${ARTIFACT_DIR}"
LOG_PATH="${ARTIFACT_DIR}/${TAG}.log"
SSH_LOG="${ARTIFACT_DIR}/${TAG}.ssh.log"
SFTP_LOG="${ARTIFACT_DIR}/${TAG}.sftp.log"
KEYSCAN_LOG="${ARTIFACT_DIR}/${TAG}.keyscan.log"
UPLOAD_LOG="${ARTIFACT_DIR}/${TAG}.upload.log"
MONITOR_PATH="${PANTHERA_OPENSSH_MONITOR_PATH:-$(mktemp -u "/tmp/panthera-ssh-${TAG:0:24}.sock.XXXXXX")}"
KNOWN_HOSTS="${ARTIFACT_DIR}/${TAG}.known_hosts"
KNOWN_HOSTS_TMP="${KNOWN_HOSTS}.tmp"

for upload in "${UPLOADS[@]}"; do
  if [[ "${upload}" != *:* ]]; then
    echo "--upload must be LOCAL:REMOTE: ${upload}" >&2
    exit 2
  fi
  upload_local="${upload%%:*}"
  upload_remote="${upload#*:}"
  if [[ -z "${upload_local}" || -z "${upload_remote}" ]]; then
    echo "--upload must be LOCAL:REMOTE: ${upload}" >&2
    exit 2
  fi
  if [[ ! -f "${upload_local}" ]]; then
    echo "upload source not found: ${upload_local}" >&2
    exit 1
  fi
done

qemu_args=(--no-reboot --ssh-port "${PORT}")
if [[ -n "${DATA_DISK}" ]]; then
  qemu_args+=(--data-disk "${DATA_DISK}")
fi

if [[ "${REBUILD_ROOTFS}" == "1" ]]; then
  if [[ "${ROOT_KIND}" == "zfs" ]]; then
    PATH="/usr/local/zfs/bin:${PATH}" \
    PANTHERA_ROOT_SHELL=/bin/zsh \
    PANTHERA_DEFAULT_NETWORK_OWNER=ipconfiguration \
      bash "${PANTHERA_ROOT}/rootfs/create_zfs_root_image.sh" \
        --root-disk "${ROOT_DISK}" \
        --dataset "${ZFS_DATASET}" \
        --force
  else
    PANTHERA_ROOT_SHELL=/bin/zsh \
    PANTHERA_DEFAULT_NETWORK_OWNER=ipconfiguration \
      bash "${PANTHERA_ROOT}/rootfs/create_hfs_root_image.sh" \
        --image "${ROOT_DISK}" \
        --force \
        --no-build-components
  fi
fi

if [[ "${ROOT_KIND}" == "zfs" ]]; then
  bash "${PANTHERA_ROOT}/boot/efi/stage_phase2_efi.sh" \
    --zfs-root "${ZFS_DATASET}" \
    --rootdev "${ROOTDEV}" \
    > "${ARTIFACT_DIR}/${TAG}.stage_phase2.log" 2>&1
fi

qemu_env=(
  "PANTHERA_QEMU_MONITOR_PATH=${MONITOR_PATH}"
  "PANTHERA_ROOT_DISK=${ROOT_DISK}"
  "PANTHERA_DATA_DISK=${DATA_DISK}"
  "PANTHERA_FIX_CACHE_OWNERSHIP=${PANTHERA_FIX_CACHE_OWNERSHIP:-$([[ "${ROOT_KIND}" == "zfs" ]] && echo 0 || echo 1)}"
)
if [[ "${ROOT_KIND}" == "zfs" ]]; then
  qemu_env+=(
    "PANTHERA_ZFS_BOOT=${ZFS_DATASET}"
    "PANTHERA_ROOTDEV=${ROOTDEV}"
  )
fi

qemu_pid=""
cleanup() {
  if [[ -S "${MONITOR_PATH}" ]]; then
    printf 'quit\n' | nc -U "${MONITOR_PATH}" >/dev/null 2>&1 || true
  fi
  if [[ -n "${qemu_pid}" ]] && kill -0 "${qemu_pid}" >/dev/null 2>&1; then
    kill "${qemu_pid}" >/dev/null 2>&1 || true
    wait "${qemu_pid}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

(
  cd "${PANTHERA_ROOT}"
  env "${qemu_env[@]}" boot/qemu/run_phase2_serial.sh "${qemu_args[@]}"
) > "${LOG_PATH}" 2>&1 &
qemu_pid=$!

ssh_ready() {
  local timeout_bin
  timeout_bin="$(command -v timeout || command -v gtimeout || true)"
  if [[ -n "${timeout_bin}" ]]; then
    "${timeout_bin}" "${KEYSCAN_WATCHDOG_SECONDS}" \
      ssh-keyscan -p "${PORT}" -T "${KEYSCAN_TIMEOUT_SECONDS}" \
        -t "${KEYSCAN_TYPES}" \
        127.0.0.1 > "${KNOWN_HOSTS_TMP}" 2> "${KEYSCAN_LOG}.tmp"
  else
    ssh-keyscan -p "${PORT}" -T "${KEYSCAN_TIMEOUT_SECONDS}" \
      -t "${KEYSCAN_TYPES}" \
      127.0.0.1 > "${KNOWN_HOSTS_TMP}" 2> "${KEYSCAN_LOG}.tmp"
  fi
}

network_ready() {
  grep -Eq 'PANTHERA:configd service-to-global Global IPv4 published|PANTHERA:configd service-to-global interface IPv4 published' \
    "${LOG_PATH}"
}

remote_network_ready() {
  PANTHERA_OPENSSH_READY_COMMAND='/usr/bin/awk '"'"'/PANTHERA:configd service-to-global Global IPv4 published|PANTHERA:configd service-to-global interface IPv4 published/ { found=1 } END { exit found ? 0 : 1 }'"'"' /var/log/configd.log 2>/dev/null' \
  expect <<EXPECT >/dev/null 2>&1
set timeout ${CONNECT_TIMEOUT_SECONDS}
set command \$env(PANTHERA_OPENSSH_READY_COMMAND)
spawn ssh \
  -p ${PORT} \
  -o ConnectTimeout=${CONNECT_TIMEOUT_SECONDS} \
  -o UserKnownHostsFile=${KNOWN_HOSTS} \
  -o StrictHostKeyChecking=yes \
  -o PreferredAuthentications=password \
  -o PubkeyAuthentication=no \
  -o NumberOfPasswordPrompts=1 \
  root@127.0.0.1 \$command
expect {
    -re {[Pp]assword:} {
        send -- "\r"
        exp_continue
    }
    eof {
        catch wait result
        set rc [lindex \$result 3]
        exit \$rc
    }
    timeout {
        exit 1
    }
}
EXPECT
}


deadline=$((SECONDS + TIMEOUT_SECONDS))
until ssh_ready; do
  cat "${KEYSCAN_LOG}.tmp" >> "${KEYSCAN_LOG}" 2>/dev/null || true
  if ! kill -0 "${qemu_pid}" >/dev/null 2>&1; then
    echo "QEMU exited before sshd became reachable on guest port 22; see ${LOG_PATH}" >&2
    exit 1
  fi
  if (( SECONDS >= deadline )); then
    echo "timed out waiting for sshd to listen on guest port 22; see ${LOG_PATH}" >&2
    exit 1
  fi
  sleep 1
done

cat "${KEYSCAN_LOG}.tmp" >> "${KEYSCAN_LOG}" 2>/dev/null || true
mv "${KNOWN_HOSTS_TMP}" "${KNOWN_HOSTS}"
sleep "${POST_KEYSCAN_DELAY_SECONDS}"

until network_ready || remote_network_ready; do
  if ! kill -0 "${qemu_pid}" >/dev/null 2>&1; then
    echo "QEMU exited before guest IPv4 was published; see ${LOG_PATH}" >&2
    exit 1
  fi
  if (( SECONDS >= deadline )); then
    echo "timed out waiting for guest IPv4 publish; see ${LOG_PATH} and /var/log/configd.log in the guest" >&2
    exit 1
  fi
  sleep 1
done

if [[ "${REMOTE_COMMAND}" == *"__PANTHERA_BSD_ROOTDEV__"* ]]; then
  boot_rootdev=""
  rootdev_pattern='^BSD root: (disk[0-9]+s[0-9]+),'
  while IFS= read -r serial_line; do
    if [[ "${serial_line}" =~ ${rootdev_pattern} ]]; then
      rootdev_candidate="${BASH_REMATCH[1]}"
      if [[ -n "${boot_rootdev}" && "${boot_rootdev}" != "${rootdev_candidate}" ]]; then
        echo "ambiguous BSD root devices in serial log: ${LOG_PATH}" >&2
        exit 1
      fi
      boot_rootdev="${rootdev_candidate}"
    fi
  done < "${LOG_PATH}"
  if [[ ! "${boot_rootdev}" =~ ^disk[0-9]+s[0-9]+$ ]]; then
    echo "failed to parse BSD root device from serial log: ${LOG_PATH}" >&2
    exit 1
  fi
  REMOTE_COMMAND="${REMOTE_COMMAND//__PANTHERA_BSD_ROOTDEV__/${boot_rootdev}}"
fi


for upload in "${UPLOADS[@]}"; do
  upload_local="${upload%%:*}"
  upload_remote="${upload#*:}"
  echo "PANTHERA_OPENSSH_UPLOAD:${upload_local}:${upload_remote}" | tee -a "${UPLOAD_LOG}"
  upload_start_seconds="${SECONDS}"
  PANTHERA_OPENSSH_UPLOAD_LOCAL="${upload_local}" \
  PANTHERA_OPENSSH_UPLOAD_REMOTE="${upload_remote}" \
  expect <<EXPECT | tee -a "${UPLOAD_LOG}"
set timeout ${REMOTE_EXEC_TIMEOUT_SECONDS}
set local_path \$env(PANTHERA_OPENSSH_UPLOAD_LOCAL)
set remote_path \$env(PANTHERA_OPENSSH_UPLOAD_REMOTE)
spawn sftp \
  ${SFTP_EXTRA_ARGS} \
  -P ${PORT} \
  -o UserKnownHostsFile=${KNOWN_HOSTS} \
  -o StrictHostKeyChecking=yes \
  -o PreferredAuthentications=password \
  -o PubkeyAuthentication=no \
  -o NumberOfPasswordPrompts=1 \
  root@127.0.0.1
expect {
    -re {[Pp]assword:} {
        send -- "\r"
        exp_continue
    }
    -re {sftp>} {
        send -- "put \$local_path \$remote_path\r"
    }
    timeout {
        puts "PANTHERA_OPENSSH_UPLOAD:TIMEOUT_LOGIN"
        exit 1
    }
    eof {
        puts "PANTHERA_OPENSSH_UPLOAD:EOF_LOGIN"
        exit 1
    }
}
expect {
    -re {sftp>} {
        puts "PANTHERA_OPENSSH_UPLOAD:PASS"
        send -- "quit\r"
    }
    timeout {
        puts "PANTHERA_OPENSSH_UPLOAD:TIMEOUT_PUT"
        exit 1
    }
    eof {
        puts "PANTHERA_OPENSSH_UPLOAD:EOF_PUT"
        exit 1
    }
}
expect eof
catch wait result
set rc [lindex \$result 3]
if {\$rc != 0} { exit \$rc }
EXPECT
  upload_elapsed_seconds=$((SECONDS - upload_start_seconds))
  echo "PANTHERA_OPENSSH_UPLOAD_SECONDS:${upload_local}:${upload_remote}:${upload_elapsed_seconds}" | tee -a "${UPLOAD_LOG}"
done

export PANTHERA_OPENSSH_SMOKE_COMMAND="${REMOTE_COMMAND}"
export PANTHERA_OPENSSH_SMOKE_MARKER="${REMOTE_MARKER}"

for attempt in $(seq 1 "${REPEAT}"); do
echo "PANTHERA_OPENSSH_REMOTE_ATTEMPT:${attempt}/${REPEAT}" | tee -a "${SSH_LOG}"
expect <<EXPECT | tee -a "${SSH_LOG}"
set timeout ${REMOTE_EXEC_TIMEOUT_SECONDS}
set saw_marker 0
set command \$env(PANTHERA_OPENSSH_SMOKE_COMMAND)
set marker \$env(PANTHERA_OPENSSH_SMOKE_MARKER)
spawn ssh \
  -p ${PORT} \
  -o ConnectTimeout=${CONNECT_TIMEOUT_SECONDS} \
  -o UserKnownHostsFile=${KNOWN_HOSTS} \
  -o StrictHostKeyChecking=accept-new \
  -o PreferredAuthentications=password \
  -o PubkeyAuthentication=no \
  -o NumberOfPasswordPrompts=1 \
  root@127.0.0.1 \$command
expect {
    -re {[Pp]assword:} {
        send -- "\r"
        exp_continue
    }
    -re "\$marker" {
        set saw_marker 1
        puts "\$marker:PASS"
    }
    timeout {
        puts "\$marker:TIMEOUT"
        exit 1
    }
    eof {
        catch wait result
        set rc [lindex \$result 3]
        if {\$rc != 0} {
            puts "\$marker:EXIT:\$rc"
            exit \$rc
        }
    }
}
if {!\$saw_marker} {
    puts "\$marker:MISSING"
    exit 1
}
expect {
    eof {}
    timeout {
        puts "\$marker:TIMEOUT_EOF"
        exit 1
    }
}
catch wait result
set rc [lindex \$result 3]
if {\$rc != 0} { exit \$rc }
EXPECT
done

for attempt in $(seq 1 "${REPEAT}"); do
echo "PANTHERA_OPENSSH_SFTP_ATTEMPT:${attempt}/${REPEAT}" | tee -a "${SFTP_LOG}"
expect <<EXPECT | tee -a "${SFTP_LOG}"
set timeout 45
spawn sftp \
  ${SFTP_EXTRA_ARGS} \
  -P ${PORT} \
  -o UserKnownHostsFile=${KNOWN_HOSTS} \
  -o StrictHostKeyChecking=yes \
  -o PreferredAuthentications=password \
  -o PubkeyAuthentication=no \
  -o NumberOfPasswordPrompts=1 \
  root@127.0.0.1
expect {
    -re {[Pp]assword:} {
        send -- "\r"
        exp_continue
    }
    -re {sftp>} {
        send -- "pwd\r"
    }
    timeout {
        puts "PANTHERA_OPENSSH_SFTP:TIMEOUT_LOGIN"
        exit 1
    }
    eof {
        puts "PANTHERA_OPENSSH_SFTP:EOF_LOGIN"
        exit 1
    }
}
expect {
    -re {Remote working directory:} {
        send -- "ls /\r"
    }
    timeout {
        puts "PANTHERA_OPENSSH_SFTP:TIMEOUT_PWD"
        exit 1
    }
}
expect {
    -re {(etc|usr|var)} {
        puts "PANTHERA_OPENSSH_SFTP:PASS"
        send -- "quit\r"
    }
    timeout {
        puts "PANTHERA_OPENSSH_SFTP:TIMEOUT_LS"
        exit 1
    }
}
expect eof
catch wait result
set rc [lindex \$result 3]
if {\$rc != 0} { exit \$rc }
EXPECT
done

cleanup
trap - EXIT

cat <<EOF
OpenSSH smoke passed:
  serial log: ${LOG_PATH}
  ssh log:    ${SSH_LOG}
  sftp log:   ${SFTP_LOG}
EOF
if [[ "${#UPLOADS[@]}" -gt 0 ]]; then
  echo "  upload log: ${UPLOAD_LOG}"
fi
