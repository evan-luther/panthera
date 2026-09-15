#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TAG="reboot-$(date +%Y%m%d-%H%M%S)"
LAUNCH_ARGS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --tag) TAG="$2"; shift 2 ;;
    --help)
      echo "Usage: $0 [--tag NAME] [run_qemu_ssh.sh options]"
      echo "Uses a disposable overlay to verify normal reboot, scheduled reboot, and scheduled halt."
      exit 0 ;;
    *) LAUNCH_ARGS+=("$1"); shift ;;
  esac
done
TMP_DIR="$(mktemp -d /tmp/panthera-reboot.XXXXXX)"
MONITOR="${TMP_DIR}/monitor.sock"
cleanup() {
  if [[ -S "${MONITOR}" ]]; then
    printf 'quit\n' | nc -w 1 -U "${MONITOR}" >/dev/null 2>&1 || true
  fi
  rm -rf "${TMP_DIR}"
}
trap cleanup EXIT
mkdir -p "${ROOT}/artifacts/boot"
export PANTHERA_REBOOT_ROOT="${ROOT}" PANTHERA_REBOOT_MONITOR="${MONITOR}"
export PANTHERA_REBOOT_LOG="${ROOT}/artifacts/boot/${TAG}.log"
export PANTHERA_ROOT_DISK_SNAPSHOT=on
expect -f - -- "${LAUNCH_ARGS[@]}" <<'EXPECT'
set timeout 900
log_file -noappend $env(PANTHERA_REBOOT_LOG)
proc await_stage {pattern stage} {
    global timeout spawn_id
    expect {
        -re {panic\(cpu |i8042 restart did not complete} { puts stderr "PANTHERA_REBOOT_FAIL: guest fault at $stage"; exit 1 }
        -re $pattern { return }
        timeout { puts stderr "PANTHERA_REBOOT_FAIL: timeout at $stage"; exit 1 }
        eof { puts stderr "PANTHERA_REBOOT_FAIL: EOF at $stage"; exit 1 }
    }
}
spawn -noecho bash $env(PANTHERA_REBOOT_ROOT)/tools/run_qemu_ssh.sh {*}$argv --monitor $env(PANTHERA_REBOOT_MONITOR) --reboot
foreach {command outcome} {
    "/sbin/reboot" restart
    "/sbin/shutdown -r -q +2s" restart
    "/sbin/shutdown -h -q +2s" halt
} {
    await_stage {login: } login
    send -- "root\r"
    await_stage {Password:} password
    send -- "\r"
    await_stage {root@panthera[^\r\n]*# } shell
    send -- "$command\r"
    if {$outcome eq "halt"} {
        await_stage {CPU halted} halt
    } else {
        await_stage {MACH Reboot} kernel_reboot
        await_stage {Panthera BOOTX64 scaffold loaded} firmware_restart
    }
}
puts "PANTHERA_REBOOT_OK"
send_log "PANTHERA_REBOOT_OK\n"
EXPECT
