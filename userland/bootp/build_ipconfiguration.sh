#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BOOTPD_SRC="${PANTHERA_ROOT}/src/bootp-531.80.4"
IPCONFIG_SRC="${BOOTPD_SRC}/IPConfiguration.bproj"
BOOTPLIB_SRC="${BOOTPD_SRC}/bootplib"
IPCONFIG_FRAMEWORK_SRC="${BOOTPD_SRC}/IPConfiguration_framework"
CONFIGD_SRC="${PANTHERA_ROOT}/src/configd-1296.40.6"
CONFIGD_OUT="${PANTHERA_ROOT}/userland/configd"
OUT_ROOT="${PANTHERA_ROOT}/userland/bootp"
OBJDIR="${OUT_ROOT}/build/IPConfiguration/obj"
MIGDIR="${OUT_ROOT}/build/IPConfiguration/mig"
BINDIR="${OUT_ROOT}/bin"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
CC="$(xcrun -find clang)"
MIG="$(xcrun -find mig)"
TARGET="${TARGET:-x86_64-apple-darwin23.0}"
MINVER="${MINVER:-14.0}"
OUT="${BINDIR}/ipconfiguration"
BUNDLE_OUT="${BINDIR}/ipconfiguration.bundle"
DYLIB_OUT="${BINDIR}/libIPConfiguration.dylib"

# Locate Darwin compiler runtime (libclang_rt.osx.a)
CLANG_RT="$("${CC}" -target "${TARGET}" -print-libgcc-file-name 2>/dev/null || true)"
if [ -z "${CLANG_RT}" ] || [ ! -f "${CLANG_RT}" ]; then
  CLANG_RT="$("${CC}" -print-file-name=libclang_rt.osx.a 2>/dev/null || true)"
fi
if [ -z "${CLANG_RT}" ] || [ ! -f "${CLANG_RT}" ]; then
  CLANG_RESOURCE_DIR="$("${CC}" -print-resource-dir 2>/dev/null || true)"
  if [ -n "${CLANG_RESOURCE_DIR}" ] && [ -f "${CLANG_RESOURCE_DIR}/lib/darwin/libclang_rt.osx.a" ]; then
    CLANG_RT="${CLANG_RESOURCE_DIR}/lib/darwin/libclang_rt.osx.a"
  fi
fi

if [ -z "${CLANG_RT}" ] || [ ! -f "${CLANG_RT}" ]; then
  echo "ERROR: Unable to locate Darwin compiler runtime archive (libclang_rt.osx.a) using ${CC}" >&2
  exit 1
fi
mkdir -p "${OBJDIR}" "${MIGDIR}" "${BINDIR}"

if [[ ! -x "${SYSROOT}/usr/lib/libSystemConfiguration.dylib" ]]; then
  echo "libSystemConfiguration.dylib missing; run userland/configd/build_systemconfiguration.sh first" >&2
  exit 1
fi

PATCHDIR="${OUT_ROOT}/patches"
if [ -d "${PATCHDIR}" ]; then
  for patchfile in "${PATCHDIR}"/*.patch; do
    [ -f "${patchfile}" ] || continue
    patchname="$(basename "${patchfile}")"
    if patch -p0 -d "${BOOTPD_SRC}" -N --dry-run < "${patchfile}" >/dev/null 2>&1; then
      echo "Applying ${patchname} to bootp source..."
      if ! patch -p0 -d "${BOOTPD_SRC}" -f -s < "${patchfile}"; then
        echo "ERROR: Failed to apply patch ${patchname} to bootp source at ${BOOTPD_SRC}" >&2
        exit 1
      fi
    elif patch -p0 -d "${BOOTPD_SRC}" -f -R --dry-run < "${patchfile}" >/dev/null 2>&1; then
      echo "Patch ${patchname} already applied to bootp source, skipping."
    else
      echo "ERROR: Patch ${patchname} does not apply cleanly to bootp source at ${BOOTPD_SRC} (partial or conflicting source detected)." >&2
      echo "Source tree is in an inconsistent partial patch state." >&2
      echo "To recover, reset bootp source before rebuilding:" >&2
      echo "  rm -rf src/bootp-531.80.4 && bash tools/fetch_world_sources.sh" >&2
      exit 1
    fi
  done
fi

echo "=== Generating IPConfiguration MIG stubs ==="
"${MIG}" -isysroot "${SDKROOT}" \
  -I"${BOOTPLIB_SRC}" \
  -I"${IPCONFIG_SRC}" \
  -header "${MIGDIR}/ipconfig.h" \
  -user "${MIGDIR}/ipconfigUser.c" \
  -sheader "${MIGDIR}/ipconfigServer.h" \
  -server "${MIGDIR}/ipconfigServer.c" \
  "${BOOTPLIB_SRC}/ipconfig.defs"

COMMON_CFLAGS=(
  -target "${TARGET}"
  -mmacosx-version-min="${MINVER}"
  -isysroot "${SDKROOT}"
  -O2
  -fPIC
  -fblocks
  -DPANTHERA=1
  -DPRIVATE=1
  -DTARGET_OS_OSX=1
  -DTARGET_OS_SIMULATOR=0
  -D__BLOCKS__=1
  -D__APPLE_USE_RFC_3542
  -DSC_LOG_HANDLE=IPConfigLogGetHandle
  -DUSE_SYSTEMCONFIGURATION_PRIVATE_HEADERS
  -I"${OUT_ROOT}/include"
  -I"${IPCONFIG_SRC}"
  -I"${BOOTPLIB_SRC}"
  -I"${IPCONFIG_FRAMEWORK_SRC}"
  -I"${CONFIGD_SRC}/SystemConfiguration.fproj"
  -I"${CONFIGD_SRC}/IPMonitorControl"
  -I"${CONFIGD_SRC}/Plugins/common"
  -I"${CONFIGD_OUT}/shims"
  -I"${MIGDIR}"
  -I"${SYSROOT}/usr/include"
  -I"${PANTHERA_ROOT}/userland/iokituser/include"
  -I"${PANTHERA_ROOT}/userland/libsystem/build/obj/libsystem_c/shims"
  -I"${PANTHERA_ROOT}/userland/corefoundation/shims"
  -include "${OUT_ROOT}/panthera_bootp_prefix.h"
  -Wno-deprecated-declarations
  -Wno-nullability-completeness
  -Wno-availability
  -Wno-format
  -Wno-unused-function
  -Wno-unused-variable
  -Wno-implicit-function-declaration
  -Wno-incompatible-pointer-types
  -Wno-int-conversion
  -Wno-macro-redefined
)

bootplib_sources=(
  cfutil.c
  util.c
  dynarray.c
  ptrlist.c
  interfaces.c
  ioregpath.c
  IPConfigurationLog.c
  dhcp_options.c
  dhcplib.c
  in_cksum.c
  bpflib.c
  DNSNameList.c
  udp_transmit.c
  IPv4ClasslessRoute.c
  arp.c
  inetroute.c
  host_identifier.c
  DHCPDUID.c
  DHCPv6.c
  DHCPv6Options.c
  DNSEncryptedServers.c
  RouterAdvertisement.c
  IPv6Socket.c
  IPConfigurationControlPrefs.c
)

ipconfig_sources=(
  ipconfigd.c
  server.c
  dhcp.c
  bootp_session.c
  arp_session.c
  manual.c
  linklocal.c
  failover.c
  FDSet.c
  timer.c
  ifutil.c
  sysconfig.c
  rtutil.c
  IPConfigurationAgentUtil.c
  CGA.c
  HostUUID.c
  DHCPLease.c
  DHCPDUIDIAID.c
  DHCPv6Client.c
  DHCPv6Socket.c
  ICMPv6Socket.c
  RTADVSocket.c
)

objects=()

echo "=== Compiling bootplib subset ==="
for src_file in "${bootplib_sources[@]}"; do
  obj="${OBJDIR}/bootplib_${src_file%.c}.o"
  echo "  CC bootplib/${src_file}"
  "${CC}" "${COMMON_CFLAGS[@]}" -c "${BOOTPLIB_SRC}/${src_file}" -o "${obj}"
  objects+=("${obj}")
done

echo "=== Compiling IPConfiguration subset ==="
for src_file in "${ipconfig_sources[@]}"; do
  obj="${OBJDIR}/IPConfiguration_${src_file%.c}.o"
  echo "  CC IPConfiguration.bproj/${src_file}"
  "${CC}" "${COMMON_CFLAGS[@]}" -c "${IPCONFIG_SRC}/${src_file}" -o "${obj}"
  objects+=("${obj}")
done

echo "=== Compiling MIG and Panthera launcher ==="
"${CC}" "${COMMON_CFLAGS[@]}" -c "${MIGDIR}/ipconfigServer.c" -o "${OBJDIR}/ipconfigServer.o"
objects+=("${OBJDIR}/ipconfigServer.o")
"${CC}" "${COMMON_CFLAGS[@]}" -c "${OUT_ROOT}/panthera_ipconfiguration_compat.c" -o "${OBJDIR}/panthera_ipconfiguration_compat.o"
objects+=("${OBJDIR}/panthera_ipconfiguration_compat.o")
"${CC}" "${COMMON_CFLAGS[@]}" -c "${OUT_ROOT}/panthera_ipconfiguration_main.c" -o "${OBJDIR}/panthera_ipconfiguration_main.o"
objects+=("${OBJDIR}/panthera_ipconfiguration_main.o")

TARGET_LINK_INPUTS=(
  -L"${SYSROOT}/usr/lib"
  -L"${SYSROOT}/usr/lib/system"
  -lpanthera_extra
  "${SYSROOT}/usr/lib/libSystemConfiguration.dylib"
  "${SYSROOT}/usr/lib/libIOKit.dylib"
  "${SYSROOT}/usr/lib/libCoreFoundation.dylib"
  "${SYSROOT}/usr/lib/system/libdispatch.dylib"
  "${SYSROOT}/usr/lib/system/libxpc.dylib"
  "${SYSROOT}/usr/lib/system/libsystem_notify.dylib"
  "${SYSROOT}/usr/lib/system/libsystem_trace.dylib"
  "${SYSROOT}/usr/lib/libSystem.B.dylib"
  "${CLANG_RT}"
)

echo "=== Linking IPConfiguration standalone ==="
"${CC}" -target "${TARGET}" \
  -mmacosx-version-min="${MINVER}" \
  -isysroot "${SDKROOT}" \
  -Wl,-syslibroot,"${SYSROOT}" \
  -nodefaultlibs \
  -Wl,-bind_at_load \
  -o "${OUT}" \
  "${objects[@]}" \
  "${TARGET_LINK_INPUTS[@]}"

bundle_objects=()
for obj in "${objects[@]}"; do
  case "${obj##*/}" in
    panthera_ipconfiguration_main.o)
      continue
      ;;
  esac
  bundle_objects+=("${obj}")
done

echo "=== Linking IPConfiguration bundle executable ==="
"${CC}" -target "${TARGET}" \
  -mmacosx-version-min="${MINVER}" \
  -isysroot "${SDKROOT}" \
  -Wl,-syslibroot,"${SYSROOT}" \
  -bundle \
  -nodefaultlibs \
  -Wl,-bind_at_load \
  -o "${BUNDLE_OUT}" \
  "${bundle_objects[@]}" \
  "${TARGET_LINK_INPUTS[@]}"

echo "=== Linking IPConfiguration shared-cache dylib ==="
"${CC}" -target "${TARGET}" \
  -mmacosx-version-min="${MINVER}" \
  -isysroot "${SDKROOT}" \
  -Wl,-syslibroot,"${SYSROOT}" \
  -dynamiclib \
  -nodefaultlibs \
  -Wl,-bind_at_load \
  -Wl,-no_fixup_chains \
  -install_name /usr/lib/system/libIPConfiguration.dylib \
  -o "${DYLIB_OUT}" \
  "${bundle_objects[@]}" \
  "${TARGET_LINK_INPUTS[@]}"
mkdir -p "${SYSROOT}/usr/lib/system"
cp -f "${DYLIB_OUT}" "${SYSROOT}/usr/lib/system/libIPConfiguration.dylib"

echo "=== Build complete ==="
echo "  ${OUT}"
echo "  ${BUNDLE_OUT}"
echo "  ${DYLIB_OUT}"
