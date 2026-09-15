#!/usr/bin/env bash
# tools/fetch_world_sources.sh — Bootstrap and restore external sources for Panthera
#
# Strict, idempotent source bootstrap:
#   1. Fetches and checksum-verifies all build/distfiles archives.
#   2. Fetches and checksum-verifies all manifest-referenced src/*.tar.* archives.
#   3. Downloads and extracts all Apple Open Source and upstream third-party trees
#      into src/ with overlay preservation for tracked Panthera patches.
#   4. Sets up consumer subdirectories and verifies exact sentinels across all source trees.
#   5. Emits a concise fetched/skipped summary.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
FETCH_WITH_CHECKSUM="${ROOT}/tools/fetch_with_checksum.sh"
DIST_DIR="${ROOT}/build/distfiles"
SRC_DIR="${ROOT}/src"

mkdir -p "${DIST_DIR}" "${SRC_DIR}"

FETCHED_COUNT=0
SKIPPED_COUNT=0
FAILED_COUNT=0

APPLE_OSS_BASE="https://github.com/apple-oss-distributions"

# ─────────────────────────────────────────────────────────────────────────────
# 1. Fetch distfiles referenced in docs/provenance/THIRD_PARTY_CHECKSUMS.txt
# ─────────────────────────────────────────────────────────────────────────────

# Table format: URL | REL_DEST_PATH
DISTFILES_TABLE="
https://ftp.gnu.org/gnu/bash/bash-5.2.37.tar.gz|build/distfiles/bash-5.2.37.tar.gz
https://ftp.netbsd.org/pub/NetBSD/misc/sjg/bmake-20260508.tar.gz|build/distfiles/bmake-20260508.tar.gz
https://github.com/apple-oss-distributions/cctools/archive/920a2b45080fb9badf31bf675f03b19973f0dd4f.tar.gz|build/distfiles/cctools-1030.6.3-920a2b4.tar.gz
https://github.com/llvm/llvm-project/releases/download/llvmorg-18.1.8/cmake-18.1.8.src.tar.xz|build/distfiles/cmake-18.1.8.src.tar.xz
https://curl.se/download/curl-8.12.1.tar.xz|build/distfiles/curl-8.12.1.tar.xz
https://github.com/apple-oss-distributions/ld64/archive/refs/tags/ld64-264.3.102.tar.gz|build/distfiles/ld64-264.3.102.tar.gz
https://github.com/llvm/llvm-project/releases/download/llvmorg-18.1.8/libunwind-18.1.8.src.tar.xz|build/distfiles/libunwind-18.1.8.src.tar.xz
https://www.nano-editor.org/dist/v8/nano-8.7.1.tar.xz|build/distfiles/nano-8.7.1.tar.xz
https://cdn.openbsd.org/pub/OpenBSD/OpenSSH/portable/openssh-9.7p1.tar.gz|build/distfiles/openssh-9.7p1.tar.gz
https://cdn.openbsd.org/pub/OpenBSD/OpenSSH/portable/openssh-9.9p1.tar.gz|build/distfiles/openssh-9.9p1.tar.gz
https://github.com/openssl/openssl/releases/download/openssl-3.5.5/openssl-3.5.5.tar.gz|build/distfiles/openssl-3.5.5.tar.gz
https://github.com/openzfsonosx/openzfs-fork/archive/refs/tags/zfs-macOS-2.3.1p1.tar.gz|build/distfiles/openzfs-fork-zfs-macOS-2.3.1p1.tar.gz
https://github.com/llvm/llvm-project/releases/download/llvmorg-18.1.8/runtimes-18.1.8.src.tar.xz|build/distfiles/runtimes-18.1.8.src.tar.xz
https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz|build/distfiles/zlib-1.3.1.tar.gz
"

echo ">>> Phase 1: Fetching distfiles"
while IFS='|' read -r url dest_rel; do
  [[ -z "${url:-}" ]] && continue
  dest_abs="${ROOT}/${dest_rel}"
  if [[ -f "${dest_abs}" ]]; then
    # Verify existing file against checksum
    if bash "${FETCH_WITH_CHECKSUM}" "${url}" "${dest_abs}" >/dev/null 2>&1; then
      SKIPPED_COUNT=$((SKIPPED_COUNT + 1))
      continue
    fi
  fi
  echo "  [fetch] ${dest_rel}"
  if bash "${FETCH_WITH_CHECKSUM}" "${url}" "${dest_abs}"; then
    FETCHED_COUNT=$((FETCHED_COUNT + 1))
  else
    echo "  [FAIL] Failed to fetch/verify ${dest_rel}" >&2
    exit 1
  fi
done <<< "${DISTFILES_TABLE}"

# ─────────────────────────────────────────────────────────────────────────────
# 2. Fetch manifest-referenced src/*.tar.* archives
# ─────────────────────────────────────────────────────────────────────────────

SRC_ARCHIVES_TABLE="
https://github.com/apple-oss-distributions/adv_cmds/archive/refs/tags/adv_cmds-213.tar.gz|src/adv_cmds-213.tar.gz
https://github.com/apple-oss-distributions/diskdev_cmds/archive/refs/tags/diskdev_cmds-593.tar.gz|src/diskdev_cmds-593.tar.gz
https://github.com/apple-oss-distributions/basic_cmds/archive/refs/tags/basic_cmds-70.tar.gz|src/basic_cmds-70.tar.gz
https://distfiles.gentoo.org/distfiles/89/dash-0.5.12.tar.gz|src/dash-0.5.12.tar.gz
https://matt.ucc.asn.au/dropbear/releases/dropbear-2024.86.tar.bz2|src/dropbear-2024.86.tar.bz2
https://github.com/apple-oss-distributions/file_cmds/archive/refs/tags/file_cmds-475.tar.gz|src/file_cmds-475.tar.gz
https://github.com/apple-oss-distributions/shell_cmds/archive/refs/tags/shell_cmds-326.tar.gz|src/shell_cmds-326.tar.gz
https://github.com/apple-oss-distributions/text_cmds/archive/refs/tags/text_cmds-197.tar.gz|src/text_cmds-197.tar.gz
"

echo ""
echo ">>> Phase 2: Fetching manifest-tracked src archives"
while IFS='|' read -r url dest_rel; do
  [[ -z "${url:-}" ]] && continue
  dest_abs="${ROOT}/${dest_rel}"
  if [[ -f "${dest_abs}" ]]; then
    if bash "${FETCH_WITH_CHECKSUM}" "${url}" "${dest_abs}" >/dev/null 2>&1; then
      SKIPPED_COUNT=$((SKIPPED_COUNT + 1))
      continue
    fi
  fi
  echo "  [fetch] ${dest_rel}"
  if bash "${FETCH_WITH_CHECKSUM}" "${url}" "${dest_abs}"; then
    FETCHED_COUNT=$((FETCHED_COUNT + 1))
  else
    echo "  [FAIL] Failed to fetch/verify ${dest_rel}" >&2
    exit 1
  fi
done <<< "${SRC_ARCHIVES_TABLE}"

# ─────────────────────────────────────────────────────────────────────────────
# 3. Restore upstream source trees into src/ with overlay preservation
# ─────────────────────────────────────────────────────────────────────────────

# Helper function to unpack tarball into a directory, preserving existing overlays
# Usage: unpack_tree URL DEST_DIR SENTINEL_FILE [STRIP_COMPONENTS]
unpack_tree() {
  local url="$1"
  local dest_dir="$2"
  local sentinel="$3"
  local strip="${4:-1}"
  local marker="${dest_dir}/.panthera-fetched"

  # If sentinel file and marker matching exact URL already exist, skip
  if [[ -d "${dest_dir}" && -e "${dest_dir}/${sentinel}" && -f "${marker}" ]]; then
    if [[ "$(cat "${marker}" 2>/dev/null || true)" == "${url}" ]]; then
      SKIPPED_COUNT=$((SKIPPED_COUNT + 1))
      return 0
    fi
  fi

  echo "  [restore] $(basename "${dest_dir}")"
  local tmp_archive
  tmp_archive="$(mktemp /tmp/panthera-fetch.XXXXXX)"
  
  if ! curl -fsSL -o "${tmp_archive}" "${url}"; then
    echo "  [FAIL] Download error: ${url}" >&2
    rm -f "${tmp_archive}"
    exit 1
  fi

  local tmp_extract
  tmp_extract="$(mktemp -d /tmp/panthera-extract.XXXXXX)"

  # Extract into clean temporary extract directory
  local tar_flags="-xf"
  case "${url}" in
    *.tar.bz2|*.tbz2) tar_flags="-xjf" ;;
    *.tar.xz|*.txz) tar_flags="-xJf" ;;
    *.tar.gz|*.tgz) tar_flags="-xzf" ;;
  esac

  if ! tar ${tar_flags} "${tmp_archive}" -C "${tmp_extract}"; then
    echo "  [FAIL] Extraction failed for ${url}" >&2
    rm -f "${tmp_archive}"
    rm -rf "${tmp_extract}"
    exit 1
  fi
  rm -f "${tmp_archive}"

  # Locate unpacked root inside tmp_extract
  local unpack_root="${tmp_extract}"
  if [[ "${strip}" == "1" ]]; then
    local entries=(${tmp_extract}/*)
    if [[ ${#entries[@]} -eq 1 && -d "${entries[0]}" ]]; then
      unpack_root="${entries[0]}"
    fi
  fi

  # Preserve existing tracked files / overlays
  local tmp_overlay
  tmp_overlay="$(mktemp -d /tmp/panthera-overlay.XXXXXX)"
  if [[ -d "${dest_dir}" ]]; then
    cp -R "${dest_dir}/." "${tmp_overlay}/" 2>/dev/null || true
  fi

  mkdir -p "${dest_dir}"
  cp -R "${unpack_root}/." "${dest_dir}/"
  rm -rf "${tmp_extract}"

  # Re-apply tracked overlays on top of fresh upstream extraction
  if [[ -d "${tmp_overlay}" && "$(ls -A "${tmp_overlay}" 2>/dev/null)" ]]; then
    cp -R "${tmp_overlay}/." "${dest_dir}/"
  fi
  rm -rf "${tmp_overlay}"

  # Apply OpenIOKit patches if present and not yet applied
  local kext_patch="${ROOT}/kexts/OpenIOKit/patches/$(basename "${dest_dir}").patch"
  local patch_marker="${dest_dir}/.panthera-patches-applied"
  if [[ -f "${kext_patch}" && ! -f "${patch_marker}" ]]; then
    echo "  [patch] $(basename "${kext_patch}") -> $(basename "${dest_dir}")"
    (cd "${dest_dir}" && patch -p1 < "${kext_patch}")
    touch "${patch_marker}"
  fi

  # Setup consumer subdirectories for tools that expect nested layouts
  local base_name
  base_name="$(basename "${dest_dir}")"
  case "${base_name}" in
    awk)
      mkdir -p "${dest_dir}/src"
      cp -p "${dest_dir}/"*.c "${dest_dir}/"*.h "${dest_dir}/"*.tab.* "${dest_dir}/src/" 2>/dev/null || true
      ;;
    bzip2)
      mkdir -p "${dest_dir}/bzip2"
      cp -p "${dest_dir}/"*.c "${dest_dir}/"*.h "${dest_dir}/bzip2/" 2>/dev/null || true
      ;;
    less)
      if [[ ! -d "${dest_dir}/less" ]]; then
        mkdir -p "${dest_dir}/less"
        cp -p "${dest_dir}/"*.c "${dest_dir}/"*.h "${dest_dir}/less/" 2>/dev/null || true
      fi
      ;;
  esac

  # Verify sentinel
  if [[ ! -e "${dest_dir}/${sentinel}" ]]; then
    echo "  [FAIL] Sentinel missing after extraction: ${dest_dir}/${sentinel}" >&2
    exit 1
  fi

  printf '%s\n' "${url}" > "${marker}"
  FETCHED_COUNT=$((FETCHED_COUNT + 1))
}

echo ""
echo ">>> Phase 3: Restoring source trees"

# Table format: URL | DEST_REL_DIR | SENTINEL | STRIP_COMPONENTS
TREES_TABLE="
${APPLE_OSS_BASE}/xnu/archive/refs/tags/xnu-10002.41.9.tar.gz|src/xnu-10002.41.9|pexpert/pexpert/i386/boot.h|1
${APPLE_OSS_BASE}/AvailabilityVersions/archive/refs/tags/AvailabilityVersions-137.4.tar.gz|src/AvailabilityVersions-137.4|CMakeLists.txt|1
${APPLE_OSS_BASE}/libplatform/archive/refs/tags/libplatform-306.0.1.tar.gz|src/libplatform-306.0.1|LICENSE|1
${APPLE_OSS_BASE}/libdispatch/archive/refs/tags/libdispatch-1462.0.4.tar.gz|src/libdispatch-1462.0.4|CMakeLists.txt|1
${APPLE_OSS_BASE}/dtrace/archive/refs/tags/dtrace-401.tar.gz|src/dtrace-401|lib/libdtrace|1
${APPLE_OSS_BASE}/Libc/archive/refs/tags/Libc-1583.40.7.tar.gz|src/Libc-1583.40.7|include/stdio.h|1
${APPLE_OSS_BASE}/libpthread/archive/refs/tags/libpthread-519.tar.gz|src/libpthread-519|include/darwin_posix_pthread.modulemap|1
${APPLE_OSS_BASE}/libmalloc/archive/refs/tags/libmalloc-474.0.13.tar.gz|src/libmalloc-474.0.13|featureflags/libmalloc.plist|1
${APPLE_OSS_BASE}/libclosure/archive/refs/tags/libclosure-90.tar.gz|src/libclosure-90|Block.h|1
${APPLE_OSS_BASE}/dyld/archive/refs/tags/dyld-1122.1.2.tar.gz|src/dyld-1122.1.2|include/dlfcn_private.h|1
${APPLE_OSS_BASE}/objc4/archive/refs/tags/objc4-906.tar.gz|src/objc4-906|runtime/objc.h|1
${APPLE_OSS_BASE}/bootstrap_cmds/archive/refs/tags/bootstrap_cmds-133.tar.gz|src/bootstrap_cmds-133|APPLE_LICENSE|1
${APPLE_OSS_BASE}/CommonCrypto/archive/refs/tags/CommonCrypto-600025.tar.gz|src/CommonCrypto-600025|ASAN.xcconfig|1
${APPLE_OSS_BASE}/Libinfo/archive/refs/tags/Libinfo-583.0.1.tar.gz|src/Libinfo-583.0.1|lookup.subproj/pwd.h|1
${APPLE_OSS_BASE}/Libnotify/archive/refs/tags/Libnotify-317.tar.gz|src/Libnotify-317|notify.h|1
${APPLE_OSS_BASE}/configd/archive/refs/tags/configd-1296.40.6.tar.gz|src/configd-1296.40.6|SystemConfiguration.fproj|1
${APPLE_OSS_BASE}/architecture/archive/refs/tags/architecture-282.tar.gz|src/architecture-282|APPLE_LICENSE|1
${APPLE_OSS_BASE}/files/archive/refs/tags/files-926.0.2.tar.gz|src/files-926.0.2|private/etc|1
${APPLE_OSS_BASE}/IOPCIFamily/archive/refs/tags/IOPCIFamily-617.40.5.0.1.tar.gz|src/IOPCIFamily-617.40.5.0.1|IOPCIBridge.cpp|1
${APPLE_OSS_BASE}/IOStorageFamily/archive/refs/tags/IOStorageFamily-312.tar.gz|src/IOStorageFamily-312|IOStorage.cpp|1
${APPLE_OSS_BASE}/IOHIDFamily/archive/refs/tags/IOHIDFamily-2008.40.6.tar.gz|src/IOHIDFamily-2008.40.6|HID/HID.xcconfig|1
${APPLE_OSS_BASE}/IOKitTools/archive/refs/tags/IOKitTools-124.tar.gz|src/IOKitTools-124|ioreg.tproj|1
${APPLE_OSS_BASE}/kext_tools/archive/refs/tags/kext_tools-740.tar.gz|src/kext_tools-740|kextcache_main.c|1
${APPLE_OSS_BASE}/hfs/archive/refs/tags/hfs-650.0.2.tar.gz|src/hfs-650.0.2|core/hfs_vfsops.c|1
${APPLE_OSS_BASE}/IONetworkingFamily/archive/refs/tags/IONetworkingFamily-177.tar.gz|src/IONetworkingFamily-177|IONetworkController.cpp|1
${APPLE_OSS_BASE}/IOATAFamily/archive/refs/tags/IOATAFamily-261.tar.gz|src/IOATAFamily-261|ATADeviceNub.cpp|1
${APPLE_OSS_BASE}/IOATAPIProtocolTransport/archive/refs/tags/IOATAPIProtocolTransport-353.tar.gz|src/IOATAPIProtocolTransport-353|IOATAPIProtocolTransport.cpp|1
${APPLE_OSS_BASE}/IOSCSIArchitectureModelFamily/archive/refs/tags/IOSCSIArchitectureModelFamily-139.0.2.tar.gz|src/IOSCSIArchitectureModelFamily-139.0.2|APPLE_LICENSE|1
${APPLE_OSS_BASE}/IOSCSIParallelFamily/archive/refs/tags/IOSCSIParallelFamily-339.tar.gz|src/IOSCSIParallelFamily-339|APPLE_LICENSE|1
${APPLE_OSS_BASE}/IOCDStorageFamily/archive/refs/tags/IOCDStorageFamily-61.tar.gz|src/IOCDStorageFamily-61|APPLE_LICENSE|1
${APPLE_OSS_BASE}/IODVDStorageFamily/archive/refs/tags/IODVDStorageFamily-45.tar.gz|src/IODVDStorageFamily-45|APPLE_LICENSE|1
${APPLE_OSS_BASE}/IOBDStorageFamily/archive/refs/tags/IOBDStorageFamily-22.tar.gz|src/IOBDStorageFamily-22|APPLE_LICENSE|1
${APPLE_OSS_BASE}/IOSerialFamily/archive/refs/tags/IOSerialFamily-93.200.2.tar.gz|src/IOSerialFamily-93.200.2|APPLE_LICENSE|1
${APPLE_OSS_BASE}/IOGraphics/archive/refs/tags/IOGraphics-598.tar.gz|src/IOGraphics-598|IOGraphicsFamily/IOBootFramebuffer.cpp|1
${APPLE_OSS_BASE}/IOAudioFamily/archive/refs/tags/IOAudioFamily-540.3.tar.gz|src/IOAudioFamily-540.3|APPLE_LICENSE|1
${APPLE_OSS_BASE}/IOFireWireFamily/archive/refs/tags/IOFireWireFamily-487.tar.gz|src/IOFireWireFamily-487|APPLE_LICENSE|1
${APPLE_OSS_BASE}/IOFireWireAVC/archive/refs/tags/IOFireWireAVC-431.tar.gz|src/IOFireWireAVC-431|IOFireWireAVC|1
${APPLE_OSS_BASE}/IOFireWireSBP2/archive/refs/tags/IOFireWireSBP2-445.tar.gz|src/IOFireWireSBP2-445|APPLE_LICENSE|1
${APPLE_OSS_BASE}/IOFireWireSerialBusProtocolTransport/archive/refs/tags/IOFireWireSerialBusProtocolTransport-260.tar.gz|src/IOFireWireSerialBusProtocolTransport-260|APPLE_LICENSE|1
${APPLE_OSS_BASE}/IOACPIFamily/archive/refs/tags/IOACPIFamily-8.tar.gz|src/IOACPIFamily-8|IOACPIFamily.pbproj/project.pbxproj|1
${APPLE_OSS_BASE}/IOUSBFamily/archive/refs/tags/IOUSBFamily-630.4.5.tar.gz|src/IOUSBFamily-630.4.5|IOUSBFamily/Classes/IOUSBBus.cpp|1
${APPLE_OSS_BASE}/IOI2CFamily/archive/refs/tags/IOI2CFamily-1011.0.1.tar.gz|src/IOI2CFamily-1011.0.1|I2CControllers/IOI2CControllerPMU.cpp|1
${APPLE_OSS_BASE}/AppleAPIC/archive/refs/tags/AppleAPIC-13.tar.gz|src/AppleAPIC-13|Apple8259PIC.h|1
${APPLE_OSS_BASE}/AppleSMBIOS/archive/refs/tags/AppleSMBIOS-42.tar.gz|src/AppleSMBIOS-42|AppleSMBIOS.cpp|1
${APPLE_OSS_BASE}/AppleI386GenericPlatform/archive/refs/tags/AppleI386GenericPlatform-5.tar.gz|src/AppleI386GenericPlatform-5|AppleI386CPU.cpp|1
${APPLE_OSS_BASE}/AppleI386PCI/archive/refs/tags/AppleI386PCI-6.tar.gz|src/AppleI386PCI-6|AppleI386AGP.cpp|1
${APPLE_OSS_BASE}/AppleGenericPCATA/archive/refs/tags/AppleGenericPCATA-5.tar.gz|src/AppleGenericPCATA-5|AppleGenericPCATA.pbproj/project.pbxproj|1
${APPLE_OSS_BASE}/AppleIntelPIIXATA/archive/refs/tags/AppleIntelPIIXATA-251.0.1.tar.gz|src/AppleIntelPIIXATA-251.0.1|AppleIntelICHxSATA.cpp|1
${APPLE_OSS_BASE}/AppleIntel8255x/archive/refs/tags/AppleIntel8255x-19.tar.gz|src/AppleIntel8255x-19|AppleIntel8255x.xcodeproj/project.pbxproj|1
${APPLE_OSS_BASE}/ApplePS2Controller/archive/refs/tags/ApplePS2Controller-8.tar.gz|src/ApplePS2Controller-8|ApplePS2Controller.cpp|1
${APPLE_OSS_BASE}/ApplePS2Keyboard/archive/refs/tags/ApplePS2Keyboard-9.tar.gz|src/ApplePS2Keyboard-9|ApplePS2Keyboard.cpp|1
${APPLE_OSS_BASE}/ApplePS2Mouse/archive/refs/tags/ApplePS2Mouse-10.tar.gz|src/ApplePS2Mouse-10|ApplePS2Mouse.cpp|1
${APPLE_OSS_BASE}/Apple16X50Serial/archive/refs/tags/Apple16X50Serial-24.tar.gz|src/Apple16X50Serial-24|Apple16X50ACPI.cpp|1
${APPLE_OSS_BASE}/AppleRTL8139Ethernet/archive/refs/tags/AppleRTL8139Ethernet-153.tar.gz|src/AppleRTL8139Ethernet-153|RTL8139.cpp|1
${APPLE_OSS_BASE}/AppleFileSystemDriver/archive/refs/tags/AppleFileSystemDriver-31.tar.gz|src/AppleFileSystemDriver-31|AppleFileSystemDriver.cpp|1
${APPLE_OSS_BASE}/Libsystem/archive/refs/tags/Libsystem-1336.tar.gz|src/Libsystem-1336|init.c|1
${APPLE_OSS_BASE}/IOKitUser/archive/refs/tags/IOKitUser-100065.40.4.tar.gz|src/IOKitUser-100065.40.4|IOKitLib.h|1
${APPLE_OSS_BASE}/launchd/archive/refs/tags/launchd-842.92.1.tar.gz|src/launchd-842.92.1|SystemStarter/IPC.c|1
${APPLE_OSS_BASE}/bootp/archive/refs/tags/bootp-531.80.4.tar.gz|src/bootp-531.80.4|bootplib/interfaces.c|1
${APPLE_OSS_BASE}/mDNSResponder/archive/refs/tags/mDNSResponder-1790.80.10.tar.gz|src/mDNSResponder-1790.80.10|LICENSE|1
${APPLE_OSS_BASE}/CF/archive/refs/tags/CF-1153.18.tar.gz|src/CF-1153.18|CFBase.h|1
${APPLE_OSS_BASE}/ICU/archive/refs/tags/ICU-76142.3.1.1.tar.gz|src/ICU-76142.3.1.1|ICU.plist|1
${APPLE_OSS_BASE}/ncurses/archive/refs/tags/ncurses-71.100.2.tar.gz|src/apple-ncurses-71.100.2|ncurses/ANNOUNCE|1
${APPLE_OSS_BASE}/zsh/archive/refs/tags/zsh-108.tar.gz|src/zsh-108|Makefile|1
${APPLE_OSS_BASE}/expat/archive/refs/tags/expat-45.tar.gz|src/expat-45|README|1
${APPLE_OSS_BASE}/libxml2/archive/refs/tags/libxml2-39.10.tar.gz|src/libxml2-39.10|Configurations/Base.xcconfig|1
${APPLE_OSS_BASE}/pcre/archive/refs/tags/pcre-21.tar.gz|src/pcre-21|Makefile|1
${APPLE_OSS_BASE}/libffi/archive/refs/tags/libffi-40.tar.gz|src/libffi-40|ChangeLog.old|1
${APPLE_OSS_BASE}/libedit/archive/refs/tags/libedit-65.tar.gz|src/libedit-65|UPDATING|1
${APPLE_OSS_BASE}/libarchive/archive/refs/tags/libarchive-160.60.3.tar.gz|src/libarchive-160.60.3|config.h|1
${APPLE_OSS_BASE}/patch_cmds/archive/refs/tags/patch_cmds-72.tar.gz|src/patch_cmds-72|cmp/cmp.1|1
${APPLE_OSS_BASE}/sudo/archive/refs/tags/sudo-114.60.3.tar.gz|src/sudo-114.60.3|QA/README.md|1
${APPLE_OSS_BASE}/system_cmds/archive/refs/tags/system_cmds-1039.tar.gz|src/system_cmds|APPLE_LICENSE|1
${APPLE_OSS_BASE}/file_cmds/archive/refs/tags/file_cmds-475.tar.gz|src/file_cmds-475|chflags/chflags.1|1
${APPLE_OSS_BASE}/shell_cmds/archive/refs/tags/shell_cmds-326.tar.gz|src/shell_cmds-326|alias/alias.1|1
${APPLE_OSS_BASE}/text_cmds/archive/refs/tags/text_cmds-197.tar.gz|src/text_cmds-197|banner/banner.6|1
${APPLE_OSS_BASE}/basic_cmds/archive/refs/tags/basic_cmds-70.tar.gz|src/basic_cmds-70|basic_cmds.xcodeproj/project.pbxproj|1
${APPLE_OSS_BASE}/syslog/archive/refs/tags/syslog-406.tar.gz|src/syslog-406|libsystem_asl.tproj/include/asl_client.h|1
${APPLE_OSS_BASE}/libcppabi/archive/refs/tags/libcppabi-26.tar.gz|src/libcppabi-26|exports/libcppabi-.exp|1
https://github.com/llvm/llvm-project/releases/download/llvmorg-19.1.0/libcxx-19.1.0.src.tar.xz|src/llvm-libcxx-19/libcxx|include/string|1
https://github.com/llvm/llvm-project/releases/download/llvmorg-17.0.6/llvm-project-17.0.6.src.tar.xz|src/llvm-project-17.0.6|llvm/CMakeLists.txt|1
https://ftp.gnu.org/gnu/libiconv/libiconv-1.17.tar.gz|src/libiconv-1.17|COPYING|1
https://matt.ucc.asn.au/dropbear/releases/dropbear-2024.86.tar.bz2|src/dropbear-2024.86|LICENSE|1
https://ftp.netbsd.org/pub/NetBSD/misc/sjg/bmake-20260508.tar.gz|src/bmake-20260508|LICENSE|1
https://github.com/apple-oss-distributions/cctools/archive/920a2b45080fb9badf31bf675f03b19973f0dd4f.tar.gz|src/cctools-1030.6.3|ar/ar.c|1
https://github.com/apple-oss-distributions/ld64/archive/refs/tags/ld64-264.3.102.tar.gz|src/ld64-264.3.102|src/ld/ld.cpp|1
${APPLE_OSS_BASE}/bash/archive/refs/tags/bash-142.tar.gz|src/bash|bash-3.2/ABOUT-NLS|1
${APPLE_OSS_BASE}/awk/archive/refs/tags/awk-40.tar.gz|src/awk|src/main.c|1
${APPLE_OSS_BASE}/less/archive/refs/tags/less-50.tar.gz|src/less|less/main.c|1
${APPLE_OSS_BASE}/vim/archive/refs/tags/vim-163.tar.gz|src/vim|src/main.c|1
${APPLE_OSS_BASE}/bzip2/archive/refs/tags/bzip2-47.tar.gz|src/bzip2|bzip2/bzip2.c|1
${APPLE_OSS_BASE}/adv_cmds/archive/refs/tags/adv_cmds-213.tar.gz|src/adv_cmds|ps/ps.c|1
${APPLE_OSS_BASE}/diskdev_cmds/archive/refs/tags/diskdev_cmds-593.tar.gz|src/diskdev_cmds|mount.tproj/mount.c|1
"

while IFS='|' read -r url dest_rel sentinel strip; do
  [[ -z "${url:-}" ]] && continue
  dest_abs="${ROOT}/${dest_rel}"
  unpack_tree "${url}" "${dest_abs}" "${sentinel}" "${strip}"
done <<< "${TREES_TABLE}"

# ─────────────────────────────────────────────────────────────────────────────
# 4. Final Summary
# ─────────────────────────────────────────────────────────────────────────────

echo ""
echo "================================================================================"
echo "Panthera World Source Bootstrap Complete"
echo "  Fetched: ${FETCHED_COUNT}"
echo "  Skipped: ${SKIPPED_COUNT} (already verified/complete)"
echo "  Failed:  ${FAILED_COUNT}"
echo "================================================================================"

exit 0
