#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC="${PANTHERA_ROOT}/src/configd-1296.40.6"
SC_SRC="${SRC}/SystemConfiguration.fproj"
BOOTPD_SRC="${PANTHERA_ROOT}/src/bootp-531.80.4"
OUT_ROOT="${PANTHERA_ROOT}/userland/configd"
BOOTP_OUT="${PANTHERA_ROOT}/userland/bootp"
OBJDIR="${OUT_ROOT}/build/configd/obj"
MIGDIR="${OUT_ROOT}/mig_gen"
BINDIR="${OUT_ROOT}/bin"
IPCONFIG_OBJDIR="${BOOTP_OUT}/build/IPConfiguration/obj"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"
SDKROOT="${SDKROOT:-$(xcrun -sdk macosx --show-sdk-path)}"
CC="${CC:-$(xcrun -find clang)}"
MIG="${MIG:-$(xcrun -find mig)}"
TARGET="${TARGET:-x86_64-apple-darwin23.0}"
MINVER="${MINVER:-14.0}"

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

OUT="${BINDIR}/configd"
PROBE_OUT="${BINDIR}/sc_dynamic_store_probe"
NETWORK_STATE_OUT="${BINDIR}/sc_network_state_publisher"
NETWORK_PROBE_OUT="${BINDIR}/sc_network_state_probe"
KERNEL_EVENT_MONITOR_OUT="${BINDIR}/kernel_event_monitor"

COMMON_LDFLAGS=(
  -target "${TARGET}"
  -mmacosx-version-min="${MINVER}"
  -isysroot "${SDKROOT}"
  -Wl,-syslibroot,"${SYSROOT}"
  -nodefaultlibs
)

BASE_LINK_INPUTS=(
  -L"${SYSROOT}/usr/lib"
  -L"${SYSROOT}/usr/lib/system"
  -lpanthera_extra
  "${SYSROOT}/usr/lib/libSystemConfiguration.dylib"
  "${SYSROOT}/usr/lib/libCoreFoundation.dylib"
  "${SYSROOT}/usr/lib/libSystem.B.dylib"
  "${CLANG_RT}"
)

SERVICE_LINK_INPUTS=(
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

mkdir -p "${OBJDIR}" "${MIGDIR}" "${BINDIR}"

if [[ ! -x "${SYSROOT}/usr/lib/libSystemConfiguration.dylib" ]]; then
  echo "libSystemConfiguration.dylib missing; run userland/configd/build_systemconfiguration.sh first" >&2
  exit 1
fi

PATCHDIR="${OUT_ROOT}/patches"
if [ -d "${PATCHDIR}" ]; then
  shopt -s nullglob
  patchfiles=("${PATCHDIR}"/*.patch)
  shopt -u nullglob
  if [ ${#patchfiles[@]} -gt 0 ]; then
    IFS=$'\n' patchfiles=($(LC_ALL=C sort <<<"${patchfiles[*]}"))
    unset IFS
    for patchfile in "${patchfiles[@]}"; do
      [ -f "${patchfile}" ] || continue
      patchname="$(basename "${patchfile}")"
      if patch -p0 -d "${SRC}" -N --dry-run < "${patchfile}" >/dev/null 2>&1; then
        echo "Applying ${patchname} to SystemConfiguration source..."
        if ! patch -p0 -d "${SRC}" -f -s < "${patchfile}"; then
          echo "ERROR: Failed to apply patch ${patchname} to SystemConfiguration source at ${SRC}" >&2
          exit 1
        fi
      elif patch -p0 -d "${SRC}" -f -R --dry-run < "${patchfile}" >/dev/null 2>&1; then
        echo "Patch ${patchname} already applied to SystemConfiguration source, skipping."
      else
        echo "ERROR: Patch ${patchname} does not apply cleanly to SystemConfiguration source at ${SRC} (partial or conflicting source detected)." >&2
        echo "Source tree is in an inconsistent partial patch state." >&2
        echo "To recover, reset configd source before rebuilding:" >&2
        echo "  rm -rf src/configd-1296.40.6 && bash tools/fetch_world_sources.sh" >&2
        exit 1
      fi
    done
  fi
fi

if [[ "${PANTHERA_CONFIGD_SKIP_IPCONFIGURATION_REBUILD:-0}" != "1" \
   || ! -f "${IPCONFIG_OBJDIR}/IPConfiguration_ipconfigd.o" \
   || ! -f "${IPCONFIG_OBJDIR}/ipconfigServer.o" \
   || ! -f "${IPCONFIG_OBJDIR}/panthera_ipconfiguration_compat.o" \
   || ! -f "${BOOTP_OUT}/bin/ipconfiguration.bundle" ]]; then
  echo "=== Building IPConfiguration bundle for configd plugin loading ==="
  bash "${BOOTP_OUT}/build_ipconfiguration.sh"
fi

echo "=== Generating configd MIG server stubs ==="
CONFIG_DEFS="${MIGDIR}/config_panthera.defs"
sed 's/^UseSpecialReplyPort 1;/UseSpecialReplyPort 0;/' \
  "${SC_SRC}/config.defs" > "${CONFIG_DEFS}"

"${MIG}" -isysroot "${SDKROOT}" \
  -I"${SC_SRC}" \
  -header "${MIGDIR}/config.h" \
  -user "${MIGDIR}/configUser.c" \
  -sheader "${MIGDIR}/configServer.h" \
  -server "${MIGDIR}/configServer.c" \
  "${CONFIG_DEFS}"

COMMON_CFLAGS=(
  -target "${TARGET}"
  -mmacosx-version-min="${MINVER}"
  -isysroot "${SDKROOT}"
  -O2
  -fPIC
  -DPANTHERA=1
  -DPRIVATE=1
  -DTARGET_OS_OSX=1
  -DTARGET_OS_SIMULATOR=0
  -I"${SC_SRC}"
  -I"${BOOTPD_SRC}/IPConfiguration.bproj"
  -I"${BOOTPD_SRC}/bootplib"
  -I"${BOOTPD_SRC}/IPConfiguration_framework"
  -I"${BOOTP_OUT}/include"
  -I"${IPCONFIG_OBJDIR%/obj}/mig"
  -I"${SRC}/Plugins/KernelEventMonitor"
  -I"${SRC}/Plugins/common"
  -I"${SRC}/IPMonitorControl"
  -I"${SRC}/dnsinfo"
  -I"${SRC}/nwi"
  -I"${MIGDIR}"
  -I"${OUT_ROOT}/shims"
  -I"${SYSROOT}/usr/include"
  -I"${PANTHERA_ROOT}/userland/libsystem/build/obj/libsystem_c/shims"
  -include "${OUT_ROOT}/shims/panthera_sc_prefix.h"
  -Wno-deprecated-declarations
  -Wno-nullability-completeness
  -Wno-availability
  -Wno-unused-function
  -Wno-unused-variable
  -Wno-implicit-function-declaration
  -Wno-incompatible-pointer-types
  -Wno-macro-redefined
)

echo "=== Compiling Panthera configd ==="
"${CC}" "${COMMON_CFLAGS[@]}" \
  -c "${OUT_ROOT}/panthera_configd.c" -o "${OBJDIR}/panthera_configd.o"
"${CC}" "${COMMON_CFLAGS[@]}" \
  -c "${OUT_ROOT}/panthera_route_manager.c" \
  -o "${OBJDIR}/panthera_route_manager.o"
"${CC}" "${COMMON_CFLAGS[@]}" -DPANTHERA_NETWORK_STATE_NO_MAIN=1 -c "${OUT_ROOT}/panthera_network_state_publisher.c" -o "${OBJDIR}/panthera_network_state_publisher_configd.o"
"${CC}" "${COMMON_CFLAGS[@]}" -c "${MIGDIR}/configServer.c" -o "${OBJDIR}/configServer.o"
kernel_event_sources=(
  eventmon.c
  ev_dlil.c
  ev_extra.c
  ev_ipv4.c
  ev_ipv6.c
)
kernel_event_objects=()
for src_file in "${kernel_event_sources[@]}"; do
  obj="${OBJDIR}/KernelEventMonitor_${src_file%.c}.o"
  echo "  CC KernelEventMonitor/${src_file}"
  "${CC}" "${COMMON_CFLAGS[@]}" -fblocks -D__BLOCKS__=1 \
    -c "${SRC}/Plugins/KernelEventMonitor/${src_file}" -o "${obj}"
  kernel_event_objects+=("${obj}")
done
"${CC}" "${COMMON_CFLAGS[@]}" -fblocks -D__BLOCKS__=1 \
  -c "${OUT_ROOT}/panthera_sc_compat.c" \
  -o "${OBJDIR}/panthera_sc_compat_configd_kem.o"

echo "=== Linking configd ==="
"${CC}" "${COMMON_LDFLAGS[@]}" \
  -o "${OUT}" \
  "${OBJDIR}/panthera_configd.o" \
  "${OBJDIR}/panthera_route_manager.o" \
  "${OBJDIR}/panthera_network_state_publisher_configd.o" \
  "${OBJDIR}/configServer.o" \
  -Wl,-needed_library,"${SYSROOT}/usr/lib/system/libIPConfiguration.dylib" \
  "${SERVICE_LINK_INPUTS[@]}"

echo "=== Building SCDynamicStore probe ==="
"${CC}" "${COMMON_CFLAGS[@]}" -c "${OUT_ROOT}/panthera_sc_probe.c" -o "${OBJDIR}/panthera_sc_probe.o"
"${CC}" "${COMMON_LDFLAGS[@]}" \
  -o "${PROBE_OUT}" \
  "${OBJDIR}/panthera_sc_probe.o" \
  "${BASE_LINK_INPUTS[@]}"

echo "=== Building network state publisher ==="
"${CC}" "${COMMON_CFLAGS[@]}" -c "${OUT_ROOT}/panthera_network_state_publisher.c" -o "${OBJDIR}/panthera_network_state_publisher.o"
"${CC}" "${COMMON_LDFLAGS[@]}" \
  -o "${NETWORK_STATE_OUT}" \
  "${OBJDIR}/panthera_network_state_publisher.o" \
  "${BASE_LINK_INPUTS[@]}"

echo "=== Building network state probe ==="
"${CC}" "${COMMON_CFLAGS[@]}" -c "${OUT_ROOT}/panthera_sc_network_probe.c" -o "${OBJDIR}/panthera_sc_network_probe.o"
"${CC}" "${COMMON_LDFLAGS[@]}" \
  -o "${NETWORK_PROBE_OUT}" \
  "${OBJDIR}/panthera_sc_network_probe.o" \
  "${BASE_LINK_INPUTS[@]}"

echo "=== Building Apple KernelEventMonitor standalone ==="
"${CC}" "${COMMON_CFLAGS[@]}" -fblocks -D__BLOCKS__=1 \
  -c "${OUT_ROOT}/panthera_kernel_event_monitor_main.c" \
  -o "${OBJDIR}/panthera_kernel_event_monitor_main.o"
"${CC}" "${COMMON_CFLAGS[@]}" -fblocks -D__BLOCKS__=1 \
  -c "${OUT_ROOT}/panthera_sc_compat.c" \
  -o "${OBJDIR}/panthera_sc_compat_kernel_event.o"
"${CC}" "${COMMON_LDFLAGS[@]}" \
  -o "${KERNEL_EVENT_MONITOR_OUT}" \
  "${OBJDIR}/panthera_kernel_event_monitor_main.o" \
  "${OBJDIR}/panthera_sc_compat_kernel_event.o" \
  "${kernel_event_objects[@]}" \
  "${SERVICE_LINK_INPUTS[@]}"

echo "=== Build complete ==="
echo "  ${OUT}"
echo "  ${PROBE_OUT}"
echo "  ${NETWORK_STATE_OUT}"
echo "  ${NETWORK_PROBE_OUT}"
echo "  ${KERNEL_EVENT_MONITOR_OUT}"
