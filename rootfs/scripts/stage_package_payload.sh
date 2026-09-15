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

stage_framework_dylib() {
  local name="$1"
  local dylib="$2"
  local framework_dir="${mounted_volume}/System/Library/Frameworks/${name}.framework"
  local version_dir="${framework_dir}/Versions/A"

  cp "${dylib}" "${mounted_volume}/usr/lib/$(basename "${dylib}")"
  mkdir -p "${version_dir}/Resources"
  ln -sf "/usr/lib/$(basename "${dylib}")" "${version_dir}/${name}"
  ln -sfn A "${framework_dir}/Versions/Current"
  ln -sf "Versions/Current/${name}" "${framework_dir}/${name}"
  ln -sfn "Versions/Current/Resources" "${framework_dir}/Resources"
}

EXPAT_LIB_DIR="${PANTHERA_ROOT}/userland/expat/lib"
LIBXML2_LIB_DIR="${PANTHERA_ROOT}/userland/libxml2/lib"
LIBXML2_BIN_DIR="${PANTHERA_ROOT}/userland/libxml2/bin"
PCRE_LIB_DIR="${PANTHERA_ROOT}/userland/pcre/lib"
LIBFFI_LIB_DIR="${PANTHERA_ROOT}/userland/libffi/lib"
LIBEDIT_LIB_DIR="${PANTHERA_ROOT}/userland/libedit/lib"
LIBCXXABI_DYLIB="${PANTHERA_ROOT}/userland/libcxxabi/libc++abi.dylib"
LIBCXX_DYLIB="${PANTHERA_ROOT}/userland/libcxx/libc++.1.dylib"
LIBICUCORE_DYLIB="${PANTHERA_ROOT}/userland/icu/libicucore.A.dylib"
ICU_DATA_FILE="${PANTHERA_ROOT}/userland/icu/data/icudt76l.dat"
LIBOBJC_DYLIB="${PANTHERA_ROOT}/userland/objc4/lib/libobjc.A.dylib"
LIBCF_DYLIB="${PANTHERA_ROOT}/userland/corefoundation/libCoreFoundation.dylib"
LIBIOKIT_DYLIB="${PANTHERA_ROOT}/userland/iokituser/lib/libIOKit.dylib"
LIBSYSTEMCONFIGURATION_DYLIB="${PANTHERA_ROOT}/userland/configd/lib/libSystemConfiguration.dylib"
SYSTEMCONFIGURATION_INFO_PLIST="${PANTHERA_ROOT}/src/configd-1296.40.6/SystemConfiguration.fproj/Info.plist"
LIBARCHIVE_LIB_DIR="${PANTHERA_ROOT}/userland/libarchive/lib"
LIBARCHIVE_BIN_DIR="${PANTHERA_ROOT}/userland/libarchive/bin"
PATCH_CMDS_BIN_DIR="${PANTHERA_ROOT}/userland/patch_cmds/bin"
FILE_CMDS_BIN_DIR="${PANTHERA_ROOT}/userland/file_cmds/bin"
BMAKE_BIN_DIR="${PANTHERA_ROOT}/userland/bmake/bin"
BMAKE_MK_DIR="${PANTHERA_ROOT}/userland/bmake/share/mk"
CCTOOLS_BIN_DIR="${PANTHERA_ROOT}/userland/cctools/bin"
LD64_BIN_DIR="${PANTHERA_ROOT}/userland/ld64/bin"
CLANG_BIN_DIR="${PANTHERA_ROOT}/userland/clang/bin"
CLANG_LIB_DIR="${PANTHERA_ROOT}/userland/clang/lib"
PANTHERA_SDK_HELLO_BIN="${PANTHERA_ROOT}/tools/panthera_sdk_hello"
IOKIT_FB_PROBE_BIN="${PANTHERA_ROOT}/userland/iokituser/bin/iokit_fb_probe"
SC_DYNAMIC_STORE_PROBE_BIN="${PANTHERA_ROOT}/userland/configd/bin/sc_dynamic_store_probe"
DROPBEAR_BIN_DIR="${PANTHERA_ROOT}/userland/dropbear/bin"
DROPBEAR_SBIN_DIR="${PANTHERA_ROOT}/userland/dropbear/sbin"
OPENSSH_BIN_DIR="${PANTHERA_ROOT}/userland/openssh/bin"
OPENSSH_SBIN_DIR="${PANTHERA_ROOT}/userland/openssh/sbin"
OPENSSH_LIBEXEC_DIR="${PANTHERA_ROOT}/userland/openssh/libexec"
SSHD_WRAPPER="${PANTHERA_ROOT}/rootfs/usr/libexec/start_sshd.sh"
SSHD_CONFIG="${PANTHERA_ROOT}/rootfs/etc/ssh/sshd_config"
MDNSRESPONDER_SBIN_DIR="${PANTHERA_ROOT}/userland/mdnsresponder/sbin"
MDNSRESPONDER_BIN_DIR="${PANTHERA_ROOT}/userland/mdnsresponder/bin"
MDNSRESPONDER_LIB_DIR="${PANTHERA_ROOT}/userland/mdnsresponder/lib"
MDNSRESPONDER_WRAPPER="${PANTHERA_ROOT}/rootfs/usr/libexec/start_mdnsresponder.sh"
SYSLOGD_SBIN_DIR="${PANTHERA_ROOT}/userland/syslog/sbin"
NOTIFYD_BIN_DIR="${PANTHERA_ROOT}/userland/notifyd/bin"
CONFIGD_BIN="${PANTHERA_ROOT}/userland/configd/bin/configd"
SC_NETWORK_STATE_PUBLISHER_BIN="${PANTHERA_ROOT}/userland/configd/bin/sc_network_state_publisher"
SC_NETWORK_STATE_PROBE_BIN="${PANTHERA_ROOT}/userland/configd/bin/sc_network_state_probe"
KERNEL_EVENT_MONITOR_BIN="${PANTHERA_ROOT}/userland/configd/bin/kernel_event_monitor"
IPCONFIGURATION_BIN="${PANTHERA_ROOT}/userland/bootp/bin/ipconfiguration"
IPCONFIGURATION_BUNDLE_BIN="${PANTHERA_ROOT}/userland/bootp/bin/ipconfiguration.bundle"
IPCONFIGURATION_BUNDLE_ROOT="${PANTHERA_ROOT}/rootfs/System/Library/SystemConfiguration/IPConfiguration.bundle"
IPCONFIGURATION_STRINGS="${PANTHERA_ROOT}/src/bootp-531.80.4/IPConfiguration.bproj/en.lproj/Localizable.strings"
ZFS_SBIN_DIR="${PANTHERA_ROOT}/userland/zfs/sbin"
MOUNT_TOOLS_SBIN_DIR="${PANTHERA_ROOT}/userland/mount_tools/sbin"
LIBDISKARBITRATION_DYLIB="${PANTHERA_ROOT}/userland/diskarbitration/lib/libDiskArbitration.dylib"
ZFS_MOUNT_SMOKE_SCRIPT="${PANTHERA_ROOT}/rootfs/usr/libexec/zfs_mount_smoke.sh"
ZFS_MOUNT_SMOKE_PLIST="${PANTHERA_ROOT}/rootfs/System/Library/LaunchDaemons/com.panthera.zfs-mount-smoke.plist"
ZFS_HYBRID_CONFIG="${PANTHERA_ROOT}/rootfs/etc/panthera/zfs-hybrid.conf"
ZFS_HYBRID_SC_PREFS_SEED="${PANTHERA_ROOT}/rootfs/Library/Preferences/SystemConfiguration/preferences.plist"

ncurses_lib="${PANTHERA_ROOT}/userland/libsystem/build/sysroot/usr/lib/libncurses.5.4.dylib"
if selected "userland/libsystem/build/sysroot/usr/lib/libncurses.5.4.dylib" && [[ -f "${ncurses_lib}" ]]; then
  cp "${ncurses_lib}" "${mounted_volume}/usr/lib/libncurses.5.4.dylib"
  ln -sf libncurses.5.4.dylib "${mounted_volume}/usr/lib/libncurses.5.dylib"
  ln -sf libncurses.5.dylib "${mounted_volume}/usr/lib/libncurses.dylib"
  ln -sf libncurses.5.dylib "${mounted_volume}/usr/lib/libcurses.dylib"
  ln -sf libncurses.5.dylib "${mounted_volume}/usr/lib/libtermcap.dylib"
fi

if [[ -f "${mounted_volume}/usr/lib/libiconv.2.dylib" ]]; then
  ln -sf libiconv.2.dylib "${mounted_volume}/usr/lib/libiconv.dylib"
fi

if selected "userland/expat/lib/libexpat.1.dylib" && [[ -f "${EXPAT_LIB_DIR}/libexpat.1.dylib" ]]; then
  cp "${EXPAT_LIB_DIR}/libexpat.1.dylib" "${mounted_volume}/usr/lib/libexpat.1.dylib"
  ln -sf libexpat.1.dylib "${mounted_volume}/usr/lib/libexpat.dylib"
fi

if selected "userland/libxml2/lib/libxml2.2.dylib" && [[ -f "${LIBXML2_LIB_DIR}/libxml2.2.dylib" ]]; then
  cp "${LIBXML2_LIB_DIR}/libxml2.2.dylib" "${mounted_volume}/usr/lib/libxml2.2.dylib"
  ln -sf libxml2.2.dylib "${mounted_volume}/usr/lib/libxml2.dylib"
fi
if selected "userland/libxml2/bin/xmllint" && [[ -f "${LIBXML2_BIN_DIR}/xmllint" ]]; then
  cp "${LIBXML2_BIN_DIR}/xmllint" "${mounted_volume}/usr/bin/xmllint"
fi

if selected "userland/pcre/lib/libpcre.1.dylib" && [[ -f "${PCRE_LIB_DIR}/libpcre.1.dylib" ]]; then
  cp "${PCRE_LIB_DIR}/libpcre.1.dylib" "${mounted_volume}/usr/lib/libpcre.1.dylib"
  ln -sf libpcre.1.dylib "${mounted_volume}/usr/lib/libpcre.dylib"
fi

if selected "userland/libffi/lib/libffi.dylib" && [[ -f "${LIBFFI_LIB_DIR}/libffi.dylib" ]]; then
  cp "${LIBFFI_LIB_DIR}/libffi.dylib" "${mounted_volume}/usr/lib/libffi.dylib"
fi

if selected "userland/libedit/lib/libedit.3.dylib" && [[ -f "${LIBEDIT_LIB_DIR}/libedit.3.dylib" ]]; then
  cp "${LIBEDIT_LIB_DIR}/libedit.3.dylib" "${mounted_volume}/usr/lib/libedit.3.dylib"
  ln -sf libedit.3.dylib "${mounted_volume}/usr/lib/libedit.dylib"
fi

if selected "userland/libcxxabi/libc++abi.dylib" && [[ -f "${LIBCXXABI_DYLIB}" ]]; then
  cp "${LIBCXXABI_DYLIB}" "${mounted_volume}/usr/lib/libc++abi.dylib"
fi

if selected "userland/libcxx/libc++.1.dylib" && [[ -f "${LIBCXX_DYLIB}" ]]; then
  cp "${LIBCXX_DYLIB}" "${mounted_volume}/usr/lib/libc++.1.dylib"
  ln -sf libc++.1.dylib "${mounted_volume}/usr/lib/libc++.dylib"
fi

if selected "userland/icu/libicucore.A.dylib" && [[ -f "${LIBICUCORE_DYLIB}" ]]; then
  cp "${LIBICUCORE_DYLIB}" "${mounted_volume}/usr/lib/libicucore.A.dylib"
  ln -sf libicucore.A.dylib "${mounted_volume}/usr/lib/libicucore.dylib"
fi
if selected "userland/icu/data/icudt76l.dat" && [[ -f "${ICU_DATA_FILE}" ]]; then
  mkdir -p "${mounted_volume}/usr/share/icu"
  cp "${ICU_DATA_FILE}" "${mounted_volume}/usr/share/icu/icudt76l.dat"
fi

if selected "userland/objc4/lib/libobjc.A.dylib" && [[ -f "${LIBOBJC_DYLIB}" ]]; then
  cp "${LIBOBJC_DYLIB}" "${mounted_volume}/usr/lib/libobjc.A.dylib"
  ln -sf libobjc.A.dylib "${mounted_volume}/usr/lib/libobjc.dylib"
fi

if selected "userland/corefoundation/libCoreFoundation.dylib" && [[ -f "${LIBCF_DYLIB}" ]]; then
  stage_framework_dylib "CoreFoundation" "${LIBCF_DYLIB}"
fi

if selected "userland/iokituser/lib/libIOKit.dylib" && [[ -f "${LIBIOKIT_DYLIB}" ]]; then
  stage_framework_dylib "IOKit" "${LIBIOKIT_DYLIB}"
fi

if selected "userland/configd/lib/libSystemConfiguration.dylib" && [[ -f "${LIBSYSTEMCONFIGURATION_DYLIB}" ]]; then
  stage_framework_dylib "SystemConfiguration" "${LIBSYSTEMCONFIGURATION_DYLIB}"
  if [[ -f "${SYSTEMCONFIGURATION_INFO_PLIST}" ]]; then
    cp "${SYSTEMCONFIGURATION_INFO_PLIST}" \
      "${mounted_volume}/System/Library/Frameworks/SystemConfiguration.framework/Versions/A/Resources/Info.plist"
  fi
fi

if selected "userland/libarchive/lib/libarchive.13.dylib" && [[ -f "${LIBARCHIVE_LIB_DIR}/libarchive.13.dylib" ]]; then
  cp "${LIBARCHIVE_LIB_DIR}/libarchive.13.dylib" "${mounted_volume}/usr/lib/libarchive.13.dylib"
  ln -sf libarchive.13.dylib "${mounted_volume}/usr/lib/libarchive.dylib"
fi
if selected "userland/libarchive/bin/bsdtar" && [[ -f "${LIBARCHIVE_BIN_DIR}/bsdtar" ]]; then
  cp "${LIBARCHIVE_BIN_DIR}/bsdtar" "${mounted_volume}/usr/bin/bsdtar"
  ln -sf bsdtar "${mounted_volume}/usr/bin/tar"
fi
if selected "userland/libarchive/bin/bsdcpio" && [[ -f "${LIBARCHIVE_BIN_DIR}/bsdcpio" ]]; then
  cp "${LIBARCHIVE_BIN_DIR}/bsdcpio" "${mounted_volume}/usr/bin/bsdcpio"
  ln -sf bsdcpio "${mounted_volume}/usr/bin/cpio"
fi

if selected "userland/patch_cmds/bin/patch" && [[ -f "${PATCH_CMDS_BIN_DIR}/patch" ]]; then
  cp "${PATCH_CMDS_BIN_DIR}/patch" "${mounted_volume}/usr/bin/patch"
fi
if selected "userland/file_cmds/bin/install" && [[ -f "${FILE_CMDS_BIN_DIR}/install" ]]; then
  cp "${FILE_CMDS_BIN_DIR}/install" "${mounted_volume}/usr/bin/install"
fi
if selected "userland/bmake/bin/bmake" && [[ -f "${BMAKE_BIN_DIR}/bmake" ]]; then
  cp "${BMAKE_BIN_DIR}/bmake" "${mounted_volume}/usr/bin/bmake"
  if selected "userland/bmake/bin/bmake"; then
    ln -sf bmake "${mounted_volume}/usr/bin/make"
  fi
fi
if selected "userland/bmake/share/mk" && [[ -d "${BMAKE_MK_DIR}" ]]; then
  mkdir -p "${mounted_volume}/usr/share/mk"
  cp -R "${BMAKE_MK_DIR}/." "${mounted_volume}/usr/share/mk/"
fi
if selected "userland/cctools/bin/ar" && [[ -f "${CCTOOLS_BIN_DIR}/ar" ]]; then
  cp "${CCTOOLS_BIN_DIR}/ar" "${mounted_volume}/usr/bin/ar"
fi
if selected "userland/cctools/bin/ranlib" && [[ -f "${CCTOOLS_BIN_DIR}/ranlib" ]]; then
  cp "${CCTOOLS_BIN_DIR}/ranlib" "${mounted_volume}/usr/bin/ranlib"
fi
if selected "userland/ld64/bin/ld" && [[ -f "${LD64_BIN_DIR}/ld" ]]; then
  cp "${LD64_BIN_DIR}/ld" "${mounted_volume}/usr/bin/ld"
fi
if selected "userland/clang/bin/clang-17" && [[ -f "${CLANG_BIN_DIR}/clang-17" ]]; then
  cp "${CLANG_BIN_DIR}/clang-17" "${mounted_volume}/usr/bin/clang-17"
  ln -sf clang-17 "${mounted_volume}/usr/bin/clang"
fi
if selected "userland/clang/bin/cc" && [[ -f "${CLANG_BIN_DIR}/cc" ]]; then
  cp "${CLANG_BIN_DIR}/cc" "${mounted_volume}/usr/bin/cc"
fi
if selected "userland/clang/lib/libLTO.dylib" && [[ -f "${CLANG_LIB_DIR}/libLTO.dylib" ]]; then
  cp "${CLANG_LIB_DIR}/libLTO.dylib" "${mounted_volume}/usr/lib/libLTO.dylib"
fi
if selected "userland/clang/lib/clang/17" && [[ -d "${CLANG_LIB_DIR}/clang/17" ]]; then
  mkdir -p "${mounted_volume}/usr/lib/clang"
  rm -rf "${mounted_volume}/usr/lib/clang/17"
  cp -R "${CLANG_LIB_DIR}/clang/17" "${mounted_volume}/usr/lib/clang/17"
fi
if selected "tools/panthera_sdk_hello" && [[ -f "${PANTHERA_SDK_HELLO_BIN}" ]]; then
  cp "${PANTHERA_SDK_HELLO_BIN}" "${mounted_volume}/usr/bin/panthera_sdk_hello"
fi

test_pthread="${PANTHERA_ROOT}/tests/test_pthread"
if selected "tests/test_pthread" && [[ -f "${test_pthread}" ]]; then
  cp "${test_pthread}" "${mounted_volume}/usr/bin/test_pthread"
fi

test_pthread_detached="${PANTHERA_ROOT}/tests/test_pthread_detached"
if selected "tests/test_pthread_detached" && [[ -f "${test_pthread_detached}" ]]; then
  cp "${test_pthread_detached}" "${mounted_volume}/usr/bin/test_pthread_detached"
fi

test_cf="${PANTHERA_ROOT}/tests/test_cf"
if selected "tests/test_cf" && [[ -f "${test_cf}" ]]; then
  cp "${test_cf}" "${mounted_volume}/usr/bin/test_cf"
fi

if selected "userland/iokituser/bin/iokit_fb_probe" && [[ -f "${IOKIT_FB_PROBE_BIN}" ]]; then
  cp "${IOKIT_FB_PROBE_BIN}" "${mounted_volume}/usr/bin/iokit_fb_probe"
fi
if selected "userland/configd/bin/sc_dynamic_store_probe" && [[ -f "${SC_DYNAMIC_STORE_PROBE_BIN}" ]]; then
  cp "${SC_DYNAMIC_STORE_PROBE_BIN}" "${mounted_volume}/usr/bin/sc_dynamic_store_probe"
fi

if selected "userland/dropbear/sbin/dropbear" && [[ -f "${DROPBEAR_SBIN_DIR}/dropbear" ]]; then
  cp "${DROPBEAR_SBIN_DIR}/dropbear" "${mounted_volume}/usr/sbin/dropbear"
fi
if [[ -d "${DROPBEAR_BIN_DIR}" ]]; then
  for db_bin in dbclient dropbearkey dropbearconvert; do
    if selected "userland/dropbear/bin/${db_bin}" && [[ -f "${DROPBEAR_BIN_DIR}/${db_bin}" ]]; then
      cp "${DROPBEAR_BIN_DIR}/${db_bin}" "${mounted_volume}/usr/bin/${db_bin}"
    fi
  done
fi
if [[ -f "${mounted_volume}/usr/bin/dbclient" ]]; then
  ln -sf dbclient "${mounted_volume}/usr/bin/ssh"
fi
mkdir -p "${mounted_volume}/etc/dropbear"

if selected "userland/openssh/sbin/sshd" && [[ -f "${OPENSSH_SBIN_DIR}/sshd" ]]; then
  cp "${OPENSSH_SBIN_DIR}/sshd" "${mounted_volume}/usr/sbin/sshd"
fi
if [[ -d "${OPENSSH_BIN_DIR}" ]]; then
  for ssh_bin in "${OPENSSH_BIN_DIR}"/*; do
    ssh_source="userland/openssh/bin/$(basename "${ssh_bin}")"
    if selected "${ssh_source}" && [[ -f "${ssh_bin}" && -x "${ssh_bin}" ]]; then
      cp "${ssh_bin}" "${mounted_volume}/usr/bin/$(basename "${ssh_bin}")"
    fi
  done
fi
if [[ -d "${OPENSSH_LIBEXEC_DIR}" ]]; then
  for ssh_helper in "${OPENSSH_LIBEXEC_DIR}"/*; do
    ssh_helper_source="userland/openssh/libexec/$(basename "${ssh_helper}")"
    if selected "${ssh_helper_source}" && [[ -f "${ssh_helper}" && -x "${ssh_helper}" ]]; then
      cp "${ssh_helper}" "${mounted_volume}/usr/libexec/$(basename "${ssh_helper}")"
    fi
  done
fi
if selected "rootfs/etc/ssh/sshd_config" && [[ -f "${SSHD_CONFIG}" ]]; then
  cp "${SSHD_CONFIG}" "${mounted_volume}/etc/ssh/sshd_config"
fi
if selected "userland/openssh/bin/ssh-keygen" && [[ -x "${OPENSSH_BIN_DIR}/ssh-keygen" ]]; then
  if command -v ssh-keygen >/dev/null 2>&1; then
    [[ -f "${mounted_volume}/etc/ssh/ssh_host_rsa_key" ]] || \
      ssh-keygen -t rsa -b 3072 -f "${mounted_volume}/etc/ssh/ssh_host_rsa_key" -N "" -q
    [[ -f "${mounted_volume}/etc/ssh/ssh_host_ed25519_key" ]] || \
      ssh-keygen -t ed25519 -f "${mounted_volume}/etc/ssh/ssh_host_ed25519_key" -N "" -q
    chmod 600 "${mounted_volume}/etc/ssh/ssh_host_rsa_key" "${mounted_volume}/etc/ssh/ssh_host_ed25519_key"
    chmod 644 "${mounted_volume}/etc/ssh/ssh_host_rsa_key.pub" "${mounted_volume}/etc/ssh/ssh_host_ed25519_key.pub"
  else
    echo "warning: host ssh-keygen unavailable; OpenSSH host keys were not generated" >&2
  fi
fi

if selected "rootfs/usr/libexec/start_sshd.sh" && [[ -f "${SSHD_WRAPPER}" ]]; then
  cp "${SSHD_WRAPPER}" "${mounted_volume}/usr/libexec/start_sshd.sh"
  chmod 755 "${mounted_volume}/usr/libexec/start_sshd.sh"
fi

if selected "userland/mdnsresponder/sbin/mDNSResponder" && [[ -f "${MDNSRESPONDER_SBIN_DIR}/mDNSResponder" ]]; then
  cp "${MDNSRESPONDER_SBIN_DIR}/mDNSResponder" "${mounted_volume}/usr/sbin/mDNSResponder"
fi
if selected "userland/mdnsresponder/bin/dns-sd" && [[ -f "${MDNSRESPONDER_BIN_DIR}/dns-sd" ]]; then
  cp "${MDNSRESPONDER_BIN_DIR}/dns-sd" "${mounted_volume}/usr/bin/dns-sd"
fi
if selected "userland/mdnsresponder/lib/libdns_sd.dylib" && [[ -f "${MDNSRESPONDER_LIB_DIR}/libdns_sd.dylib" ]]; then
  cp "${MDNSRESPONDER_LIB_DIR}/libdns_sd.dylib" "${mounted_volume}/usr/lib/libdns_sd.dylib"
fi
if selected "rootfs/usr/libexec/start_mdnsresponder.sh" && [[ -f "${MDNSRESPONDER_WRAPPER}" ]]; then
  cp "${MDNSRESPONDER_WRAPPER}" "${mounted_volume}/usr/libexec/start_mdnsresponder.sh"
  chmod 755 "${mounted_volume}/usr/libexec/start_mdnsresponder.sh"
fi

if selected "userland/syslog/sbin/syslogd" && [[ -f "${SYSLOGD_SBIN_DIR}/syslogd" ]]; then
  cp "${SYSLOGD_SBIN_DIR}/syslogd" "${mounted_volume}/usr/sbin/syslogd"
fi

if selected "userland/notifyd/bin/notifyd" && [[ -f "${NOTIFYD_BIN_DIR}/notifyd" ]]; then
  cp "${NOTIFYD_BIN_DIR}/notifyd" "${mounted_volume}/usr/sbin/notifyd"
fi
if selected "userland/notifyd/bin/notifyutil" && [[ -f "${NOTIFYD_BIN_DIR}/notifyutil" ]]; then
  cp "${NOTIFYD_BIN_DIR}/notifyutil" "${mounted_volume}/usr/bin/notifyutil"
fi
if selected "userland/notifyd/bin/test_notify" && [[ -f "${NOTIFYD_BIN_DIR}/test_notify" ]]; then
  cp "${NOTIFYD_BIN_DIR}/test_notify" "${mounted_volume}/usr/bin/test_notify"
fi

if selected "userland/configd/bin/configd" && [[ -f "${CONFIGD_BIN}" ]]; then
  cp "${CONFIGD_BIN}" "${mounted_volume}/usr/sbin/configd"
fi
if selected "rootfs/Library/Preferences/SystemConfiguration/preferences.plist" \
  && [[ -f "${ROOTFS_DIR}/Library/Preferences/SystemConfiguration/preferences.plist" ]]; then
  mkdir -p "${mounted_volume}/Library/Preferences/SystemConfiguration"
  cp "${ROOTFS_DIR}/Library/Preferences/SystemConfiguration/preferences.plist" \
    "${mounted_volume}/Library/Preferences/SystemConfiguration/preferences.plist"
fi
if selected "userland/configd/bin/sc_network_state_publisher" && [[ -f "${SC_NETWORK_STATE_PUBLISHER_BIN}" ]]; then
  cp "${SC_NETWORK_STATE_PUBLISHER_BIN}" "${mounted_volume}/usr/sbin/sc_network_state_publisher"
fi
if selected "userland/configd/bin/sc_network_state_probe" && [[ -f "${SC_NETWORK_STATE_PROBE_BIN}" ]]; then
  cp "${SC_NETWORK_STATE_PROBE_BIN}" "${mounted_volume}/usr/bin/sc_network_state_probe"
fi
if { selected "userland/configd/bin/kernel_event_monitor" ||
     selected "userland/configd/bin/configd"; } && [[ -f "${KERNEL_EVENT_MONITOR_BIN}" ]]; then
  cp "${KERNEL_EVENT_MONITOR_BIN}" "${mounted_volume}/usr/libexec/kernel_event_monitor"
fi
if selected "userland/bootp/bin/ipconfiguration" && [[ -f "${IPCONFIGURATION_BIN}" ]]; then
  cp "${IPCONFIGURATION_BIN}" "${mounted_volume}/usr/libexec/ipconfiguration"
fi
if selected "rootfs/System/Library/SystemConfiguration/IPConfiguration.bundle/Contents/Info.plist" \
  && [[ -f "${IPCONFIGURATION_BUNDLE_ROOT}/Contents/Info.plist" ]]; then
  mkdir -p \
    "${mounted_volume}/System/Library/SystemConfiguration/IPConfiguration.bundle/Contents/MacOS" \
    "${mounted_volume}/System/Library/SystemConfiguration/IPConfiguration.bundle/Contents/Resources/en.lproj"
  cp "${IPCONFIGURATION_BUNDLE_ROOT}/Contents/Info.plist" \
    "${mounted_volume}/System/Library/SystemConfiguration/IPConfiguration.bundle/Contents/Info.plist"
  if [[ -f "${IPCONFIGURATION_BUNDLE_BIN}" ]]; then
    cp "${IPCONFIGURATION_BUNDLE_BIN}" \
      "${mounted_volume}/System/Library/SystemConfiguration/IPConfiguration.bundle/Contents/MacOS/IPConfiguration"
  fi
  if [[ -f "${IPCONFIGURATION_STRINGS}" ]]; then
    cp "${IPCONFIGURATION_STRINGS}" \
      "${mounted_volume}/System/Library/SystemConfiguration/IPConfiguration.bundle/Contents/Resources/en.lproj/Localizable.strings"
  fi
fi

if selected "userland/zfs/sbin/zsysctl" && [[ -f "${ZFS_SBIN_DIR}/zsysctl" ]]; then
  cp "${ZFS_SBIN_DIR}/zsysctl" "${mounted_volume}/usr/sbin/zsysctl"
fi
if selected "userland/zfs/sbin/zpool" && [[ -f "${ZFS_SBIN_DIR}/zpool" ]]; then
  cp "${ZFS_SBIN_DIR}/zpool" "${mounted_volume}/sbin/zpool"
fi
if selected "userland/zfs/sbin/zfs" && [[ -f "${ZFS_SBIN_DIR}/zfs" ]]; then
  cp "${ZFS_SBIN_DIR}/zfs" "${mounted_volume}/sbin/zfs"
fi
if selected "userland/zfs/sbin/mount_zfs" && [[ -f "${ZFS_SBIN_DIR}/mount_zfs" ]]; then
  cp "${ZFS_SBIN_DIR}/mount_zfs" "${mounted_volume}/sbin/mount_zfs"
fi
if selected "userland/mount_tools/sbin/mount" && [[ -f "${MOUNT_TOOLS_SBIN_DIR}/mount" ]]; then
  cp "${MOUNT_TOOLS_SBIN_DIR}/mount" "${mounted_volume}/sbin/mount"
fi
if selected "userland/mount_tools/sbin/umount" && [[ -f "${MOUNT_TOOLS_SBIN_DIR}/umount" ]]; then
  cp "${MOUNT_TOOLS_SBIN_DIR}/umount" "${mounted_volume}/sbin/umount"
fi
if selected "userland/mount_tools/sbin/panthera_zfs_hybrid_mount" \
  && [[ -f "${MOUNT_TOOLS_SBIN_DIR}/panthera_zfs_hybrid_mount" ]]; then
  cp "${MOUNT_TOOLS_SBIN_DIR}/panthera_zfs_hybrid_mount" \
    "${mounted_volume}/sbin/panthera_zfs_hybrid_mount"
fi
if [[ "${PANTHERA_STAGE_ZFS_HYBRID:-0}" == "1" ]] \
  && selected "rootfs/etc/panthera/zfs-hybrid.conf" \
  && [[ -f "${ZFS_HYBRID_CONFIG}" ]]; then
  mkdir -p "${mounted_volume}/etc/panthera"
  cp "${ZFS_HYBRID_CONFIG}" "${mounted_volume}/etc/panthera/zfs-hybrid.conf"
fi
if [[ "${PANTHERA_STAGE_ZFS_HYBRID:-0}" == "1" ]] \
  && [[ -f "${ZFS_HYBRID_SC_PREFS_SEED}" ]]; then
  mkdir -p "${mounted_volume}/System/Library/PantheraSeed/Library/Preferences/SystemConfiguration"
  cp "${ZFS_HYBRID_SC_PREFS_SEED}" \
    "${mounted_volume}/System/Library/PantheraSeed/Library/Preferences/SystemConfiguration/preferences.plist"
fi
if selected "userland/diskarbitration/lib/libDiskArbitration.dylib" \
  && [[ -f "${LIBDISKARBITRATION_DYLIB}" ]]; then
  stage_framework_dylib "DiskArbitration" "${LIBDISKARBITRATION_DYLIB}"
fi
if [[ "${PANTHERA_STAGE_ZFS_MOUNT_SMOKE:-0}" == "1" ]] \
  && selected "rootfs/usr/libexec/zfs_mount_smoke.sh" \
  && [[ -f "${ZFS_MOUNT_SMOKE_SCRIPT}" ]]; then
  cp "${ZFS_MOUNT_SMOKE_SCRIPT}" "${mounted_volume}/usr/libexec/zfs_mount_smoke.sh"
  chmod 0755 "${mounted_volume}/usr/libexec/zfs_mount_smoke.sh"
fi
if [[ "${PANTHERA_STAGE_ZFS_MOUNT_SMOKE:-0}" == "1" ]] \
  && selected "rootfs/System/Library/LaunchDaemons/com.panthera.zfs-mount-smoke.plist" \
  && [[ -f "${ZFS_MOUNT_SMOKE_PLIST}" ]]; then
  cp "${ZFS_MOUNT_SMOKE_PLIST}" \
    "${mounted_volume}/System/Library/LaunchDaemons/com.panthera.zfs-mount-smoke.plist"
fi
