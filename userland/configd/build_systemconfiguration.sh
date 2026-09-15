#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC="${PANTHERA_ROOT}/src/configd-1296.40.6"
SC_SRC="${SRC}/SystemConfiguration.fproj"
OUT_ROOT="${PANTHERA_ROOT}/userland/configd"
OBJDIR="${OUT_ROOT}/build/SystemConfiguration/obj"
INCLUDEDIR="${OUT_ROOT}/build/SystemConfiguration/include"
MIGDIR="${OUT_ROOT}/mig_gen"
LIBDIR="${OUT_ROOT}/lib"
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

OUT="${LIBDIR}/libSystemConfiguration.dylib"
SYSROOT_OUT="${SYSROOT}/usr/lib/libSystemConfiguration.dylib"

mkdir -p "${OBJDIR}" "${INCLUDEDIR}/SystemConfiguration" "${MIGDIR}" "${LIBDIR}" "${SYSROOT}/usr/include/SystemConfiguration" "${SYSROOT}/usr/lib"

echo "=== Staging SystemConfiguration headers ==="
rm -rf "${INCLUDEDIR}/CoreFoundation"
cp -R "${SYSROOT}/usr/include/CoreFoundation" "${INCLUDEDIR}/CoreFoundation"
rm -rf "${INCLUDEDIR}/net"
mkdir -p "${INCLUDEDIR}/net"
for header in if_bond_var.h if_bond_internal.h if_bridgevar.h if_vlan_var.h lacp.h; do
  cp -f "${SYSROOT}/usr/include/net/${header}" "${INCLUDEDIR}/net/${header}"
done
rm -rf "${INCLUDEDIR}/sys"
mkdir -p "${INCLUDEDIR}/sys"
for header in socket.h; do
  cp -f "${SYSROOT}/usr/include/sys/${header}" "${INCLUDEDIR}/sys/${header}"
done
find "${SC_SRC}" -maxdepth 1 -type f -name '*.h' -print0 \
  | xargs -0 -I{} cp -f {} "${SYSROOT}/usr/include/SystemConfiguration/"
find "${SC_SRC}" -maxdepth 1 -type f -name '*.h' -print0 \
  | xargs -0 -I{} cp -f {} "${INCLUDEDIR}/SystemConfiguration/"
mkdir -p "${SYSROOT}/usr/include/SystemConfiguration/helper"
mkdir -p "${INCLUDEDIR}/SystemConfiguration/helper"
find "${SC_SRC}/helper" -maxdepth 1 -type f -name '*.h' -print0 \
  | xargs -0 -I{} cp -f {} "${SYSROOT}/usr/include/SystemConfiguration/helper/"
find "${SC_SRC}/helper" -maxdepth 1 -type f -name '*.h' -print0 \
  | xargs -0 -I{} cp -f {} "${INCLUDEDIR}/SystemConfiguration/helper/"

echo "=== Generating SystemConfiguration MIG stubs ==="
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

"${MIG}" -isysroot "${SDKROOT}" \
  -I"${SC_SRC}" \
  -I"${SC_SRC}/helper" \
  -header "${MIGDIR}/helper.h" \
  -user "${MIGDIR}/helperUser.c" \
  -sheader "${MIGDIR}/helperServer.h" \
  -server "${MIGDIR}/helperServer.c" \
  "${SC_SRC}/helper/helper.defs"

COMMON_CFLAGS=(
  -target "${TARGET}"
  -mmacosx-version-min="${MINVER}"
  -isysroot "${SDKROOT}"
  -O2
  -fPIC
  -fblocks
  -DPANTHERA=1
  -DTARGET_OS_OSX=1
  -DTARGET_OS_SIMULATOR=0
  -DPRIVATE=1
  -D__BLOCKS__=1
  -I"${SC_SRC}"
  -I"${SRC}/IPMonitorControl"
  -I"${SRC}/dnsinfo"
  -I"${SRC}/nwi"
  -I"${SRC}/libSystemConfiguration"
  -I"${SC_SRC}/helper"
  -I"${SRC}/Plugins/common"
  -I"${MIGDIR}"
  -I"${OUT_ROOT}/shims"
  -I"${PANTHERA_ROOT}/userland/libsystem/build/obj/libsystem_c/shims"
  -I"${PANTHERA_ROOT}/userland/corefoundation/shims"
  -I"${PANTHERA_ROOT}/userland/iokituser/include"
  -I"${INCLUDEDIR}"
  -include "${OUT_ROOT}/shims/panthera_sc_prefix.h"
  -Wno-deprecated-declarations
  -Wno-nullability-completeness
  -Wno-availability
  -Wno-format
  -Wno-unused-function
  -Wno-unused-variable
  -Wno-implicit-function-declaration
  -Wno-incompatible-pointer-types
)

sources=(
  BondConfiguration.c
  BridgeConfiguration.c
  CaptiveNetwork.c
  DHCP.c
  LinkConfiguration.c
  SCControlPrefs.c
  SCD.c
  SCDAdd.c
  SCDCache.c
  SCDConsoleUser.c
  SCDGet.c
  SCDHostName.c
  SCDKeys.c
  SCDList.c
  SCDNotifierAdd.c
  SCDNotifierCancel.c
  SCDNotifierGetChanges.c
  SCDNotifierInformViaCallback.c
  SCDNotifierInformViaFD.c
  SCDNotifierRemove.c
  SCDNotifierSetKeys.c
  SCDNotifierWait.c
  SCDNotify.c
  SCDOpen.c
  SCDPlugin.c
  SCDPrivate.c
  SCDRemove.c
  SCDSet.c
  SCDSnapshot.c
  SCLocation.c
  SCNetwork.c
  SCNetworkCategory.c
  SCNetworkConfigurationInternal.c
  SCNetworkConfigurationPrivate.c
  SCNetworkInterface.c
  SCNetworkInterfaceProvider.c
  SCNetworkMigration.c
  SCNetworkProtocol.c
  SCNetworkService.c
  SCNetworkSet.c
  SCP.c
  SCPAdd.c
  SCPApply.c
  SCPCommit.c
  SCPGet.c
  SCPList.c
  SCPLock.c
  SCPOpen.c
  SCPPath.c
  SCPRemove.c
  SCPSet.c
  SCPUnlock.c
  SCPreferencesKeychainPrivate.c
  SCPreferencesPathKey.c
  SCProxies.c
  SCSchemaDefinitions.c
  VLANConfiguration.c
)

objects=()

# SCNetworkInterface.c includes private <EAP8021X/EAPClientProperties.h> when
# !TARGET_OS_IPHONE. Gate this inclusion on __has_include so builds succeed on
# SDKs lacking private EAP8021X headers while preserving the existing fallback.
perl -0777 -pi \
  -e 's/#if(\s+)!TARGET_OS_IPHONE(\s*\n#include <EAP8021X\/EAPClientProperties\.h>)/#if$1!TARGET_OS_IPHONE && __has_include(<EAP8021X\/EAPClientProperties.h>)$2/g;' \
  "${SC_SRC}/SCNetworkInterface.c"

# Upstream SCNetworkInterface.c references kSCNetworkInterfaceTypeCellular in
# wireless interface checks but omits its definition alongside other public
# CFString interface-type constants. Define kSCNetworkInterfaceTypeCellular
# immediately after Serial if missing.
perl -0777 -pi \
  -e 'if (!/const\s+CFStringRef\s+kSCNetworkInterfaceTypeCellular\s*=/) { s/(const\s+CFStringRef\s+kSCNetworkInterfaceTypeSerial\s*=\s*CFSTR\("Serial"\);)/$1\nconst CFStringRef kSCNetworkInterfaceTypeCellular\t= CFSTR("Cellular");/g; }' \
  "${SC_SRC}/SCNetworkInterface.c"

# Upstream SCNetworkInterface.c attempts to query interface type via SIOCGIFTYPE
# ioctl on struct ifreq (ifr_type). Host SDKs lacking SIOCGIFTYPE fail to build
# against host struct ifreq. Guard the ioctl block with #ifdef SIOCGIFTYPE and
# provide the zero-valued fallback when unavailable.
perl -0777 -pi \
  -e 'if (!/#ifdef\s+SIOCGIFTYPE/) { s/(\t+CFStringRef\s+bsdName\s*=\s*SCNetworkInterfaceGetBSDName\(interface\);\n)(\t+struct\s+ifreq\s+ifr;[\s\S]*?&ifr\.ifr_type\.ift_subfamily\);\n)/$1#ifdef SIOCGIFTYPE\n$2#else\t\/\/ SIOCGIFTYPE\n\t\tint zero = 0;\n\t\t(void)bsdName;\n\t\tinterfacePrivate->family = CFNumberCreate(NULL, kCFNumberSInt32Type, &zero);\n\t\tinterfacePrivate->subfamily = CFNumberCreate(NULL, kCFNumberSInt32Type, &zero);\n#endif\t\/\/ SIOCGIFTYPE\n/g; }' \
  "${SC_SRC}/SCNetworkInterface.c"

# Upstream SCD.c erroneously casts the addresses (&pack->olp_wall_time.tv_sec /
# &pack->olp_wall_time.tv_nsec) rather than reading the timespec member values.
# Panthera's private os_log compatibility shim defines olp_wall_time as a
# concrete struct timespec populated by mach_get_times(). Strip the erroneous
# address-of operators so tv_now receives actual timestamp values.
perl -pi \
  -e 's/&pack->olp_wall_time\.tv_sec\b/pack->olp_wall_time.tv_sec/g;' \
  -e 's/&pack->olp_wall_time\.tv_nsec\b/pack->olp_wall_time.tv_nsec/g;' \
  "${SC_SRC}/SCD.c"

# Upstream SCDPrivate.c queries file size using CFURLCopyResourcePropertyForKey
# and kCFURLFileSizeKey, which are not implemented in CF-1153. Rewrite the query
# in _SCCreatePropertyListFromResource to use CFURLCreatePropertyFromResource
# and kCFURLFileLength while preserving CFNumber ownership semantics.
perl -0777 -pi \
  -e 's/if\s*\(!CFURLCopyResourcePropertyForKey\s*\(\s*url\s*,\s*kCFURLFileSizeKey\s*,\s*&val\s*,\s*NULL\s*\)\s*\|\|\s*\(val\s*==\s*NULL\)\s*\)/val = (CFNumberRef)CFURLCreatePropertyFromResource(NULL, url, kCFURLFileLength, NULL);\n\tif (val == NULL)/g;' \
  "${SC_SRC}/SCDPrivate.c"

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

echo "=== Compiling SystemConfiguration sources ==="
for src_file in "${sources[@]}"; do
  obj="${OBJDIR}/${src_file%.c}.o"
  echo "  CC ${src_file}"
  "${CC}" "${COMMON_CFLAGS[@]}" -c "${SC_SRC}/${src_file}" -o "${obj}"
  objects+=("${obj}")
done

echo "  CC configUser.c"
"${CC}" "${COMMON_CFLAGS[@]}" -c "${MIGDIR}/configUser.c" -o "${OBJDIR}/configUser.o"
objects+=("${OBJDIR}/configUser.o")

echo "  CC libSystemConfiguration_client.c"
"${CC}" "${COMMON_CFLAGS[@]}" -c "${SRC}/libSystemConfiguration/libSystemConfiguration_client.c" -o "${OBJDIR}/libSystemConfiguration_client.o"
objects+=("${OBJDIR}/libSystemConfiguration_client.o")

echo "  CC SCHelper_client.c"
"${CC}" "${COMMON_CFLAGS[@]}" -c "${SC_SRC}/helper/SCHelper_client.c" -o "${OBJDIR}/SCHelper_client.o"
objects+=("${OBJDIR}/SCHelper_client.o")

echo "  CC helperUser.c"
"${CC}" "${COMMON_CFLAGS[@]}" -c "${MIGDIR}/helperUser.c" -o "${OBJDIR}/helperUser.o"
objects+=("${OBJDIR}/helperUser.o")

echo "  CC panthera_sc_compat.c"
"${CC}" "${COMMON_CFLAGS[@]}" -c "${OUT_ROOT}/panthera_sc_compat.c" -o "${OBJDIR}/panthera_sc_compat.o"
objects+=("${OBJDIR}/panthera_sc_compat.o")

echo "=== Linking libSystemConfiguration.dylib ==="
"${CC}" -target "${TARGET}" \
  -mmacosx-version-min="${MINVER}" \
  -isysroot "${SDKROOT}" \
  -Wl,-syslibroot,"${SYSROOT}" \
  -dynamiclib \
  -nodefaultlibs \
  -install_name /usr/lib/libSystemConfiguration.dylib \
  -o "${OUT}" \
  "${objects[@]}" \
  -L"${SYSROOT}/usr/lib" \
  -L"${SYSROOT}/usr/lib/system" \
  -lpanthera_extra \
  "${SYSROOT}/usr/lib/libIOKit.dylib" \
  "${SYSROOT}/usr/lib/libCoreFoundation.dylib" \
  "${PANTHERA_ROOT}/userland/objc4/lib/libobjc.A.dylib" \
  "${SYSROOT}/usr/lib/system/libsystem_trace.dylib" \
  "${SYSROOT}/usr/lib/libSystem.B.dylib" \
  "${CLANG_RT}" \
  -Wl,-not_for_dyld_shared_cache

install -m 755 "${OUT}" "${SYSROOT_OUT}"

echo "=== Build complete ==="
echo "  ${OUT}"
echo "  ${SYSROOT_OUT}"
echo "  exports: $(nm -gU "${SYSROOT_OUT}" | grep -c ' T ')"
