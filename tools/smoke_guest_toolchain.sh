#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
TAG="${PANTHERA_GUEST_TOOLCHAIN_TAG:-guest-toolchain-$(date +%Y%m%d-%H%M%S)}"
TIMEOUT="${PANTHERA_GUEST_TOOLCHAIN_TIMEOUT:-420}"
ROOT_KIND="${PANTHERA_GUEST_TOOLCHAIN_ROOT_KIND:-${PANTHERA_ROOT_KIND:-zfs}}"
if [[ "${ROOT_KIND}" == "zfs" ]]; then
  ROOT_DISK="${PANTHERA_GUEST_TOOLCHAIN_ROOT_DISK:-${PANTHERA_ZFS_ROOT_DISK:-${PANTHERA_ROOT}/images/qemu/panthera-zfs-root.img}}"
else
  ROOT_DISK="${PANTHERA_GUEST_TOOLCHAIN_ROOT_DISK:-${PANTHERA_ROOT}/images/qemu/panthera-root.img}"
fi
DATA_DISK="${PANTHERA_GUEST_TOOLCHAIN_DATA_DISK:-}"
PORT="${PANTHERA_OPENSSH_SMOKE_PORT:-2222}"
MARKER="PANTHERA_GUEST_TOOLCHAIN_OK"
REBUILD_ROOTFS=0

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --tag NAME         Artifact tag for tools/smoke_openssh.sh
  --timeout SECONDS  Total boot timeout (default: ${TIMEOUT})
  --root-disk PATH   Root disk image to boot
  --root-kind KIND   Root filesystem kind: zfs or hfs (default: ${ROOT_KIND})
  --data-disk PATH   Secondary data disk image to attach
  --port PORT        Host TCP port forwarded to guest sshd
  --rebuild-rootfs   Rebuild the default rootfs before booting
  --help             Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --tag)
      TAG="$2"
      shift 2
      ;;
    --timeout)
      TIMEOUT="$2"
      shift 2
      ;;
    --root-disk)
      ROOT_DISK="$2"
      shift 2
      ;;
    --root-kind)
      ROOT_KIND="$2"
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
  zfs|hfs) ;;
  *)
    echo "--root-kind must be zfs or hfs" >&2
    exit 2
    ;;
esac

read -r -d '' REMOTE_COMMAND <<'REMOTE' || true
: >/tmp/toolchain_missing.out &&
/bin/echo PANTHERA_TOOLCHAIN_STAGE_PRINTF &&
/bin/printf "abc\n" >/tmp/toolchain_printf.txt &&
/bin/cat /tmp/toolchain_printf.txt &&
/bin/echo PANTHERA_TOOLCHAIN_STAGE_CLANG_COMPILE &&
/bin/printf "int main(void){return 0;}\n" >/tmp/toolchain_compile.c &&
/usr/bin/clang -target x86_64-apple-darwin23.0 -mmacosx-version-min=14.0 -isysroot / -resource-dir /usr/lib/clang/17 -Wno-incompatible-sysroot -c /tmp/toolchain_compile.c -o /tmp/toolchain_compile.o &&
/bin/echo PANTHERA_TOOLCHAIN_STAGE_CC_RUN &&
/bin/printf "extern int puts(const char *); int main(void){ puts(\"PANTHERA_TOOLCHAIN_CC_RUN\"); return 0; }\n" >/tmp/toolchain_run.c &&
/usr/bin/cc /tmp/toolchain_run.c -o /tmp/toolchain_run &&
/tmp/toolchain_run &&
/bin/echo PANTHERA_TOOLCHAIN_STAGE_LD_RUN &&
/usr/bin/clang -target x86_64-apple-darwin23.0 -mmacosx-version-min=14.0 -isysroot / -resource-dir /usr/lib/clang/17 -Wno-incompatible-sysroot -c /tmp/toolchain_run.c -o /tmp/toolchain_run.o &&
/usr/bin/ld -demangle -dynamic -arch x86_64 -platform_version macos 14.0.0 14.0.0 -syslibroot / -o /tmp/toolchain_ld /tmp/toolchain_run.o -lSystem &&
/tmp/toolchain_ld &&
/bin/echo PANTHERA_TOOLCHAIN_STAGE_ARCHIVE &&
/usr/bin/ar -rcs /tmp/libtoolchain.a /tmp/toolchain_run.o &&
/usr/bin/ranlib /tmp/libtoolchain.a &&
/bin/echo PANTHERA_TOOLCHAIN_STAGE_LD_ERROR &&
/usr/bin/ld -demangle -dynamic -arch x86_64 -platform_version macos 14.0.0 14.0.0 -syslibroot / -o /tmp/toolchain_missing -lDefinitelyMissing >/tmp/toolchain_missing.out 2>&1
ld_rc=$?
if [ "$ld_rc" -eq 0 ]; then
  /bin/echo "ld unexpectedly linked a missing library" >&2
  exit 1
fi
if [ ! -s /tmp/toolchain_missing.out ]; then
  /bin/echo "ld missing-library output was not captured" >&2
  exit 1
fi
/bin/cat /tmp/toolchain_missing.out

/bin/echo PANTHERA_GUEST_TOOLCHAIN_OK
REMOTE

SMOKE_ARGS=(
  --root-disk "${ROOT_DISK}"
  --root-kind "${ROOT_KIND}"
  --timeout "${TIMEOUT}"
  --port "${PORT}"
  --tag "${TAG}"
  --command "${REMOTE_COMMAND}"
  --marker "${MARKER}"
)

if [[ -n "${DATA_DISK}" ]]; then
  SMOKE_ARGS+=(--data-disk "${DATA_DISK}")
fi

if [[ "${REBUILD_ROOTFS}" == "1" ]]; then
  SMOKE_ARGS+=(--rebuild-rootfs)
fi

PANTHERA_OPENSSH_REMOTE_TIMEOUT="${PANTHERA_OPENSSH_REMOTE_TIMEOUT:-420}" \
PANTHERA_ROOT_SHELL=/bin/zsh \
PANTHERA_DEFAULT_NETWORK_OWNER=ipconfiguration \
  bash "${PANTHERA_ROOT}/tools/smoke_openssh.sh" "${SMOKE_ARGS[@]}"

cat <<EOF
Guest toolchain smoke passed:
  serial log: ${PANTHERA_ROOT}/artifacts/boot/${TAG}.log
  ssh log:    ${PANTHERA_ROOT}/artifacts/boot/${TAG}.ssh.log
EOF
