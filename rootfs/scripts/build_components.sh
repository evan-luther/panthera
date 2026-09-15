#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

run_if_present() {
  local script="$1"
  shift || true

  if [[ -f "${script}" ]]; then
    bash "${script}" "$@"
  fi
}

echo ">>> Building rootfs component artifacts"

run_if_present "${PANTHERA_ROOT}/tools/build_libiconv.sh"
run_if_present "${PANTHERA_ROOT}/tools/build_apple_ncurses.sh" --host-tools --target-lib
run_if_present "${PANTHERA_ROOT}/tools/build_guest_helpers.sh"
run_if_present "${PANTHERA_ROOT}/userland/shell/build_shells.sh"
run_if_present "${PANTHERA_ROOT}/userland/zlib/build_zlib.sh"
run_if_present "${PANTHERA_ROOT}/userland/libcxxabi/build_libcxxabi.sh"
run_if_present "${PANTHERA_ROOT}/userland/libcxx/build_libcxx.sh"
run_if_present "${PANTHERA_ROOT}/userland/icu/build_icu.sh"
run_if_present "${PANTHERA_ROOT}/userland/libxml2/build_libxml2.sh"
run_if_present "${PANTHERA_ROOT}/userland/corefoundation/build_cf.sh"
run_if_present "${PANTHERA_ROOT}/userland/iokituser/build_iokituser.sh"

if [[ "${PANTHERA_STAGE_CONFIGD:-0}" == "1" ]]; then
  run_if_present "${PANTHERA_ROOT}/userland/configd/build_systemconfiguration.sh"
  run_if_present "${PANTHERA_ROOT}/userland/configd/build_configd.sh"
fi

if [[ "${PANTHERA_STAGE_IPCONFIGURATION:-0}" == "1" ]]; then
  run_if_present "${PANTHERA_ROOT}/userland/bootp/build_ipconfiguration.sh"
fi

run_if_present "${PANTHERA_ROOT}/userland/network_tools/build_network_tools.sh"
run_if_present "${PANTHERA_ROOT}/userland/login/build_login.sh"
run_if_present "${PANTHERA_ROOT}/userland/coreutils/build_coreutils.sh"
run_if_present "${PANTHERA_ROOT}/userland/coreutils/build_more_coreutils.sh"
run_if_present "${PANTHERA_ROOT}/userland/coreutils/build_new_coreutils.sh"
run_if_present "${PANTHERA_ROOT}/userland/nano/build_nano.sh"
run_if_present "${PANTHERA_ROOT}/userland/openssl/build_openssl.sh"
run_if_present "${PANTHERA_ROOT}/userland/curl/build_curl.sh"
run_if_present "${PANTHERA_ROOT}/userland/awk/build_awk.sh"
run_if_present "${PANTHERA_ROOT}/userland/less/build_less.sh"
run_if_present "${PANTHERA_ROOT}/userland/bash/build_bash.sh"
run_if_present "${PANTHERA_ROOT}/userland/vim/build_vim.sh"
run_if_present "${PANTHERA_ROOT}/userland/bzip2/build_bzip2.sh"
run_if_present "${PANTHERA_ROOT}/userland/system_cmds/build_system_cmds.sh"
run_if_present "${PANTHERA_ROOT}/userland/mount_tools/build_mount_tools.sh"
run_if_present "${PANTHERA_ROOT}/userland/dropbear/build_dropbear.sh"
run_if_present "${PANTHERA_ROOT}/userland/libedit/build_libedit.sh"
run_if_present "${PANTHERA_ROOT}/userland/openssh/build_openssh.sh"
run_if_present "${PANTHERA_ROOT}/userland/mdnsresponder/build_mdnsresponder.sh"
run_if_present "${PANTHERA_ROOT}/userland/syslog/build_syslogd.sh"
run_if_present "${PANTHERA_ROOT}/userland/notifyd/build_notifyd.sh"
run_if_present "${PANTHERA_ROOT}/userland/launchd/build_launchctl.sh"

if [[ "${PANTHERA_STAGE_ZFS:-0}" == "1" ]]; then
  run_if_present "${PANTHERA_ROOT}/userland/diskarbitration/build_diskarbitration.sh"
  run_if_present "${PANTHERA_ROOT}/userland/zfs/build_zfs_userland.sh"
fi

for t2_script in \
  "${PANTHERA_ROOT}/userland/expat/build_expat.sh" \
  "${PANTHERA_ROOT}/userland/pcre/build_pcre.sh" \
  "${PANTHERA_ROOT}/userland/libffi/build_libffi.sh" \
  "${PANTHERA_ROOT}/userland/libarchive/build_libarchive.sh" \
  "${PANTHERA_ROOT}/userland/patch_cmds/build_patch_cmds.sh"; do
  run_if_present "${t2_script}"
done

echo ">>> Rootfs component artifact build complete"
