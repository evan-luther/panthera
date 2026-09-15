#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Usage: $(basename "$0") MOUNTED_VOLUME [MANIFEST ...]" >&2
  exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
ROOTFS_DIR="${PANTHERA_ROOT}/rootfs"
mounted_volume="$1"
shift
manifests=("$@")

# shellcheck source=/dev/null
source "${SCRIPT_DIR}/manifest_lib.sh"

selected() {
  local source_path="$1"
  manifest_source_selected "${source_path}" "${manifests[@]}"
}

NETWORK_TOOLS_BIN_DIR="${PANTHERA_ROOT}/userland/network_tools/bin"
LOGIN_BIN_DIR="${PANTHERA_ROOT}/userland/login/bin"
NANO_BIN_DIR="${PANTHERA_ROOT}/userland/nano/bin"
NANO_ETC_DIR="${PANTHERA_ROOT}/userland/nano/etc"
NANO_SHARE_DIR="${PANTHERA_ROOT}/userland/nano/share/nano"
COREUTILS_BIN_DIR="${PANTHERA_ROOT}/userland/coreutils/bin"
OPENSSL_BIN_DIR="${PANTHERA_ROOT}/userland/openssl/bin"
OPENSSL_LIB_DIR="${PANTHERA_ROOT}/userland/openssl/lib"
OPENSSL_ETC_SSL_DIR="${PANTHERA_ROOT}/userland/openssl/etc/ssl"
CURL_BIN_DIR="${PANTHERA_ROOT}/userland/curl/bin"
CURL_LIB_DIR="${PANTHERA_ROOT}/userland/curl/lib"
AWK_BIN_DIR="${PANTHERA_ROOT}/userland/awk/bin"
LESS_BIN_DIR="${PANTHERA_ROOT}/userland/less/bin"
BASH_BIN_DIR="${PANTHERA_ROOT}/userland/bash/bin"
VIM_BIN_DIR="${PANTHERA_ROOT}/userland/vim/bin"
BZIP2_BIN_DIR="${PANTHERA_ROOT}/userland/bzip2/bin"
BZIP2_LIB_DIR="${PANTHERA_ROOT}/userland/bzip2/lib"
SYSTEM_CMDS_BIN_DIR="${PANTHERA_ROOT}/userland/system_cmds/bin"
ZLIB_SYSROOT_LIB="${PANTHERA_ROOT}/userland/libsystem/build/sysroot/usr/lib/libz.1.3.1.dylib"
CACHE_HELPER_BIN="${PANTHERA_ROOT}/tools/chown_cache_owner_guest"
NCURSES_PROBE_BIN="${PANTHERA_ROOT}/tools/ncurses_probe"
LAUNCHD_BIN="${PANTHERA_ROOT}/userland/launchd/launchd"
LAUNCHCTL_BIN="${PANTHERA_ROOT}/userland/launchd/launchctl"
SHELL_BIN="${PANTHERA_ROOT}/userland/shell/zsh"
MINI_SHELL_BIN="${PANTHERA_ROOT}/userland/shell/mini_sh"
DYLD_BIN="${PANTHERA_ROOT}/userland/dyld/dyld"
LIBBSM_BIN="${PANTHERA_ROOT}/userland/libbsm/libbsm.0.dylib"
SYSROOT_LIB_DIR="${PANTHERA_ROOT}/userland/libsystem/build/sysroot/usr/lib"

if selected "userland/network_tools/bin/*" && [[ -d "${NETWORK_TOOLS_BIN_DIR}" ]]; then
  shopt -s nullglob
  for network_tool in "${NETWORK_TOOLS_BIN_DIR}"/*; do
    if [[ -f "${network_tool}" && -x "${network_tool}" ]]; then
      cp "${network_tool}" "${mounted_volume}/sbin/$(basename "${network_tool}")"
    fi
  done
  shopt -u nullglob
fi

if selected "userland/login/bin/*" && [[ -d "${LOGIN_BIN_DIR}" ]]; then
  shopt -s nullglob
  for login_tool in "${LOGIN_BIN_DIR}"/*; do
    if [[ -f "${login_tool}" && -x "${login_tool}" ]]; then
      cp "${login_tool}" "${mounted_volume}/usr/bin/$(basename "${login_tool}")"
    fi
  done
  shopt -u nullglob
fi

if selected "userland/nano/bin/*" && [[ -d "${NANO_BIN_DIR}" ]]; then
  shopt -s nullglob
  for nano_tool in "${NANO_BIN_DIR}"/*; do
    if [[ -f "${nano_tool}" && -x "${nano_tool}" ]]; then
      cp "${nano_tool}" "${mounted_volume}/bin/$(basename "${nano_tool}")"
      cp "${nano_tool}" "${mounted_volume}/usr/bin/$(basename "${nano_tool}")"
    fi
  done
  shopt -u nullglob
fi

if selected "userland/coreutils/bin/*" && [[ -d "${COREUTILS_BIN_DIR}" ]]; then
  shopt -s nullglob
  for utility in "${COREUTILS_BIN_DIR}"/*; do
    if [[ -f "${utility}" && -x "${utility}" ]]; then
      cp "${utility}" "${mounted_volume}/bin/$(basename "${utility}")"
      cp "${utility}" "${mounted_volume}/usr/bin/$(basename "${utility}")"
    fi
  done
  shopt -u nullglob
fi

if selected "userland/nano/etc/nanorc" && [[ -f "${NANO_ETC_DIR}/nanorc" ]]; then
  cp "${NANO_ETC_DIR}/nanorc" "${mounted_volume}/etc/nanorc"
fi

if selected "userland/nano/share/nano" && [[ -d "${NANO_SHARE_DIR}" ]]; then
  cp -R "${NANO_SHARE_DIR}/." "${mounted_volume}/usr/share/nano/"
fi

if selected "userland/openssl/bin/openssl" && [[ -d "${OPENSSL_BIN_DIR}" ]]; then
  shopt -s nullglob
  for openssl_tool in "${OPENSSL_BIN_DIR}"/*; do
    if [[ -f "${openssl_tool}" && -x "${openssl_tool}" ]]; then
      cp "${openssl_tool}" "${mounted_volume}/usr/bin/$(basename "${openssl_tool}")"
      cp "${openssl_tool}" "${mounted_volume}/bin/$(basename "${openssl_tool}")"
    fi
  done
  shopt -u nullglob
fi

for ssl_dylib in \
  "${OPENSSL_LIB_DIR}/libcrypto.3.dylib" \
  "${OPENSSL_LIB_DIR}/libssl.3.dylib"; do
  rel_ssl_dylib="${ssl_dylib#${PANTHERA_ROOT}/}"
  if selected "${rel_ssl_dylib}" && [[ -f "${ssl_dylib}" ]]; then
    cp "${ssl_dylib}" "${mounted_volume}/usr/lib/$(basename "${ssl_dylib}")"
  fi
done

if [[ -f "${mounted_volume}/usr/lib/libcrypto.3.dylib" ]]; then
  ln -sf libcrypto.3.dylib "${mounted_volume}/usr/lib/libcrypto.dylib"
fi
if [[ -f "${mounted_volume}/usr/lib/libssl.3.dylib" ]]; then
  ln -sf libssl.3.dylib "${mounted_volume}/usr/lib/libssl.dylib"
fi

if selected "userland/openssl/lib/ossl-modules" && [[ -d "${OPENSSL_LIB_DIR}/ossl-modules" ]]; then
  cp -R "${OPENSSL_LIB_DIR}/ossl-modules/." "${mounted_volume}/usr/lib/ossl-modules/"
fi

if selected "userland/curl/bin/curl" && [[ -d "${CURL_BIN_DIR}" ]]; then
  for curl_tool in "${CURL_BIN_DIR}"/*; do
    if [[ -f "${curl_tool}" && -x "${curl_tool}" ]]; then
      cp "${curl_tool}" "${mounted_volume}/usr/bin/$(basename "${curl_tool}")"
      cp "${curl_tool}" "${mounted_volume}/bin/$(basename "${curl_tool}")"
    fi
  done
fi

if selected "userland/curl/lib/libcurl.4.dylib" && [[ -f "${CURL_LIB_DIR}/libcurl.4.dylib" ]]; then
  cp "${CURL_LIB_DIR}/libcurl.4.dylib" "${mounted_volume}/usr/lib/libcurl.4.dylib"
  ln -sf libcurl.4.dylib "${mounted_volume}/usr/lib/libcurl.dylib"
fi

if selected "userland/awk/bin/awk" && [[ -f "${AWK_BIN_DIR}/awk" ]]; then
  cp "${AWK_BIN_DIR}/awk" "${mounted_volume}/usr/bin/awk"
fi

if selected "userland/less/bin/*" && [[ -d "${LESS_BIN_DIR}" ]]; then
  for less_tool in "${LESS_BIN_DIR}"/*; do
    if [[ -f "${less_tool}" && -x "${less_tool}" ]]; then
      cp "${less_tool}" "${mounted_volume}/usr/bin/$(basename "${less_tool}")"
    fi
  done
  ln -sf less "${mounted_volume}/usr/bin/more"
fi

if selected "userland/bash/bin/bash" && [[ -f "${BASH_BIN_DIR}/bash" ]]; then
  cp "${BASH_BIN_DIR}/bash" "${mounted_volume}/bin/bash"
  cp "${BASH_BIN_DIR}/bash" "${mounted_volume}/usr/bin/bash"
fi

if selected "userland/vim/bin/vim" && [[ -f "${VIM_BIN_DIR}/vim" ]]; then
  cp "${VIM_BIN_DIR}/vim" "${mounted_volume}/usr/bin/vim"
  ln -sf vim "${mounted_volume}/usr/bin/vi"
  ln -sf vim "${mounted_volume}/usr/bin/view"
fi

if selected "userland/bzip2/bin/bzip2" && [[ -f "${BZIP2_BIN_DIR}/bzip2" ]]; then
  cp "${BZIP2_BIN_DIR}/bzip2" "${mounted_volume}/usr/bin/bzip2"
  ln -sf bzip2 "${mounted_volume}/usr/bin/bunzip2"
  ln -sf bzip2 "${mounted_volume}/usr/bin/bzcat"
fi

if selected "userland/bzip2/lib/libbz2.1.0.dylib" && [[ -f "${BZIP2_LIB_DIR}/libbz2.1.0.dylib" ]]; then
  cp "${BZIP2_LIB_DIR}/libbz2.1.0.dylib" "${mounted_volume}/usr/lib/libbz2.1.0.dylib"
  ln -sf libbz2.1.0.dylib "${mounted_volume}/usr/lib/libbz2.dylib"
fi

if (selected "userland/system_cmds/bin/*" || selected "userland/system_cmds/bin/stty" || selected "userland/system_cmds/bin/shutdown" || selected "userland/system_cmds/bin/ps") && [[ -d "${SYSTEM_CMDS_BIN_DIR}" ]]; then
  for syscmd in "${SYSTEM_CMDS_BIN_DIR}"/*; do
    if [[ -f "${syscmd}" && -x "${syscmd}" ]]; then
      local_name="$(basename "${syscmd}")"
      case "${local_name}" in
        reboot|halt|sync)
          cp "${syscmd}" "${mounted_volume}/sbin/${local_name}" ;;
        shutdown)
          cp "${syscmd}" "${mounted_volume}/sbin/${local_name}"
          ln -sf "/sbin/${local_name}" "${mounted_volume}/usr/sbin/${local_name}" ;;
        stty)
          cp "${syscmd}" "${mounted_volume}/bin/${local_name}"
          ln -sf "/bin/${local_name}" "${mounted_volume}/usr/bin/${local_name}" ;;
        ps)
          cp "${syscmd}" "${mounted_volume}/bin/${local_name}"
          ln -sf "/bin/${local_name}" "${mounted_volume}/usr/bin/${local_name}" ;;
        sysctl)
          cp "${syscmd}" "${mounted_volume}/usr/sbin/${local_name}" ;;
        nologin)
          cp "${syscmd}" "${mounted_volume}/sbin/${local_name}" ;;
        dmesg)
          cp "${syscmd}" "${mounted_volume}/sbin/${local_name}" ;;
        *)
          cp "${syscmd}" "${mounted_volume}/usr/bin/${local_name}" ;;
      esac
    fi
  done
fi


if selected "userland/libsystem/build/sysroot/usr/lib/libz.1.3.1.dylib" && [[ -f "${ZLIB_SYSROOT_LIB}" ]]; then
  cp "${ZLIB_SYSROOT_LIB}" "${mounted_volume}/usr/lib/libz.1.3.1.dylib"
  ln -sf libz.1.3.1.dylib "${mounted_volume}/usr/lib/libz.1.dylib"
  ln -sf libz.1.dylib "${mounted_volume}/usr/lib/libz.dylib"
fi

mkdir -p "${mounted_volume}/etc/ssl/certs"
if selected "userland/openssl/etc/ssl/openssl.cnf" && [[ -f "${OPENSSL_ETC_SSL_DIR}/openssl.cnf" ]]; then
  cp "${OPENSSL_ETC_SSL_DIR}/openssl.cnf" "${mounted_volume}/etc/ssl/openssl.cnf"
fi

cacert_pem="${ROOTFS_DIR}/etc/ssl/cacert.pem"
if selected "rootfs/etc/ssl/cacert.pem" && [[ -f "${cacert_pem}" ]]; then
  cp "${cacert_pem}" "${mounted_volume}/etc/ssl/cert.pem"
  cp "${cacert_pem}" "${mounted_volume}/etc/ssl/certs/ca-certificates.crt"
fi

if selected "tools/chown_cache_owner_guest" && [[ -f "${CACHE_HELPER_BIN}" ]]; then
  cp "${CACHE_HELPER_BIN}" "${mounted_volume}/bin/chown_cache_owner_guest"
  cp "${CACHE_HELPER_BIN}" "${mounted_volume}/usr/bin/chown_cache_owner_guest"
fi

if selected "tools/ncurses_probe" && [[ -f "${NCURSES_PROBE_BIN}" ]]; then
  cp "${NCURSES_PROBE_BIN}" "${mounted_volume}/bin/ncurses_probe"
  cp "${NCURSES_PROBE_BIN}" "${mounted_volume}/usr/bin/ncurses_probe"
fi

setupterm_probe_bin="${PANTHERA_ROOT}/tools/setupterm_probe"
if selected "tools/setupterm_probe" && [[ -f "${setupterm_probe_bin}" ]]; then
  cp "${setupterm_probe_bin}" "${mounted_volume}/bin/setupterm_probe"
fi

ncurses_diag_script="${PANTHERA_ROOT}/tools/ncurses_diag.sh"
if selected "tools/ncurses_diag.sh" && [[ -f "${ncurses_diag_script}" ]]; then
  mkdir -p "${mounted_volume}/usr/libexec"
  cp "${ncurses_diag_script}" "${mounted_volume}/usr/libexec/ncurses_diag.sh"
  chmod 755 "${mounted_volume}/usr/libexec/ncurses_diag.sh"
fi

for test_script in test_curl.sh test_tcp_io.sh; do
  if selected "tools/${test_script}" && [[ -f "${PANTHERA_ROOT}/tools/${test_script}" ]]; then
    mkdir -p "${mounted_volume}/usr/libexec"
    cp "${PANTHERA_ROOT}/tools/${test_script}" "${mounted_volume}/usr/libexec/${test_script}"
    chmod 755 "${mounted_volume}/usr/libexec/${test_script}"
  fi
done

if selected "userland/launchd/launchd" && [[ -f "${LAUNCHD_BIN}" ]]; then
  cp "${LAUNCHD_BIN}" "${mounted_volume}/sbin/launchd"
fi

if selected "userland/launchd/launchctl" && [[ -f "${LAUNCHCTL_BIN}" ]]; then
  cp "${LAUNCHCTL_BIN}" "${mounted_volume}/bin/launchctl"
  cp "${LAUNCHCTL_BIN}" "${mounted_volume}/usr/bin/launchctl"
fi

test_bin_dir="${PANTHERA_ROOT}/tests"
for test_bin in boot_verify_guest test_mach_ipc test_bootstrap test_bootstrap_simple test_tcp_recv test_poll443 test_poll_tcp test_nbio_select test_tls_basic test_dispatch_timer mdns_dns_sd_probe ssh_raw_exit_probe ssh_return_exit_probe pselect_pipe_eof_probe signal_unblock_probe libsystem_primitives_probe zfs_root_statfs_probe poll_listen_probe test_openssl_connect test_recv_len test_recv_eagain test_xpc_service; do
  src="${test_bin_dir}/${test_bin}"
  if [[ ! -f "${src}" ]]; then
    src="${PANTHERA_ROOT}/tools/${test_bin}"
  fi
  rel_src="${src#${PANTHERA_ROOT}/}"
  if selected "${rel_src}" && [[ -f "${src}" ]]; then
    cp "${src}" "${mounted_volume}/usr/bin/${test_bin}"
  fi
done

if selected "tools/boot_verify_guest" && [[ -f "${PANTHERA_ROOT}/tools/boot_verify_guest" ]]; then
  ln -sf boot_verify_guest "${mounted_volume}/usr/bin/boot_verify_daemon_network"
  ln -sf boot_verify_guest "${mounted_volume}/usr/bin/boot_verify_ipconfig_control"
fi

if selected "userland/shell/zsh" && [[ -f "${SHELL_BIN}" ]]; then
  cp "${SHELL_BIN}" "${mounted_volume}/bin/zsh"
  ln -sf zsh "${mounted_volume}/bin/zsh5"
  ln -sf zsh "${mounted_volume}/bin/sh"
fi

if selected "userland/shell/mini_sh" && [[ -f "${MINI_SHELL_BIN}" ]]; then
  cp "${MINI_SHELL_BIN}" "${mounted_volume}/bin/mini_sh"
fi

if selected "userland/dyld/dyld" && [[ -f "${DYLD_BIN}" ]]; then
  cp "${DYLD_BIN}" "${mounted_volume}/usr/lib/dyld"
fi

for dylib in \
  "${SYSROOT_LIB_DIR}/libSystem.B.dylib" \
  "${SYSROOT_LIB_DIR}/libiconv.2.dylib" \
  "${SYSROOT_LIB_DIR}/libncurses.5.4.dylib" \
  "${LIBBSM_BIN}"; do
  rel_dylib="${dylib#${PANTHERA_ROOT}/}"
  if selected "${rel_dylib}" && [[ -f "${dylib}" ]]; then
    cp "${dylib}" "${mounted_volume}/usr/lib/$(basename "${dylib}")"
  fi
done
if selected "userland/libsystem/build/sysroot/usr/lib/libSystem.B.dylib"; then
  ln -sf libSystem.B.dylib "${mounted_volume}/usr/lib/libSystem.dylib"
fi

if selected "userland/libsystem/build/sysroot/usr/lib/system/*.dylib" && [[ -d "${SYSROOT_LIB_DIR}/system" ]]; then
  cp -R "${SYSROOT_LIB_DIR}/system/." "${mounted_volume}/usr/lib/system/"
fi
