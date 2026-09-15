#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
VERSION="1790.80.10"
EXPECTED_COMMIT="8769ab51605e465425d33d757f602ce5905ca639"
BUILD_REVISION="panthera-tcp-loopback-ipc-ipv4-noscan-4"
SRC_DIR="${PANTHERA_ROOT}/src/mDNSResponder-${VERSION}"
OUT_ROOT="${PANTHERA_ROOT}/userland/mdnsresponder"
SBIN_DIR="${OUT_ROOT}/sbin"
BIN_DIR="${OUT_ROOT}/bin"
LIB_DIR="${OUT_ROOT}/lib"
BUILD_STAMP="${OUT_ROOT}/.build-revision"
SDKROOT="${SDKROOT:-$(xcrun -sdk macosx --show-sdk-path)}"
CC="${CC:-$(xcrun -find clang)}"
TARGET="${TARGET:-x86_64-apple-darwin23.0}"
MINVER="${MINVER:-14.0}"

mkdir -p "${SBIN_DIR}" "${BIN_DIR}" "${LIB_DIR}" "${PANTHERA_ROOT}/src"

if [[ "${PANTHERA_FORCE_REBUILD:-0}" != "1" \
   && -x "${SBIN_DIR}/mDNSResponder" \
   && -x "${BIN_DIR}/dns-sd" \
   && -f "${LIB_DIR}/libdns_sd.dylib" \
   && -f "${BUILD_STAMP}" \
   && "$(< "${BUILD_STAMP}")" == "${BUILD_REVISION}" ]]; then
  echo "mDNSResponder ${VERSION} already staged"
  exit 0
fi

REQUIRED_SENTINELS=(
  "LICENSE"
  "mDNSPosix/Makefile"
  "mDNSPosix/PosixDaemon.c"
  "Clients/dns-sd.c"
  "mDNSShared/dns_sd.h"
)

has_valid_source() {
  if [[ ! -d "${SRC_DIR}" ]]; then
    return 1
  fi
  for sentinel in "${REQUIRED_SENTINELS[@]}"; do
    if [[ ! -f "${SRC_DIR}/${sentinel}" ]]; then
      return 1
    fi
  done
  return 0
}

if ! has_valid_source; then
  if [[ -f "${PANTHERA_ROOT}/tools/fetch_world_sources.sh" ]]; then
    echo "mDNSResponder ${VERSION} source missing or incomplete at ${SRC_DIR}; fetching via tools/fetch_world_sources.sh..."
    bash "${PANTHERA_ROOT}/tools/fetch_world_sources.sh"
  fi
fi

if ! has_valid_source; then
  echo "mDNSResponder ${VERSION} source not found or incomplete: ${SRC_DIR}" >&2
  echo "Expected source tree with release sentinels. Run tools/fetch_world_sources.sh to fetch upstream sources." >&2
  exit 1
fi

if [[ -d "${SRC_DIR}/.git" ]]; then
  actual_commit="$(git -C "${SRC_DIR}" rev-parse HEAD 2>/dev/null || true)"
  if [[ -z "${actual_commit}" ]]; then
    echo "Unable to identify mDNSResponder git source at ${SRC_DIR}" >&2
    exit 1
  fi
  if [[ "${actual_commit}" != "${EXPECTED_COMMIT}" ]]; then
    echo "mDNSResponder source commit mismatch: expected ${EXPECTED_COMMIT}, got ${actual_commit}" >&2
    exit 1
  fi
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
      if patch -p0 -d "${SRC_DIR}" -N --dry-run < "${patchfile}" >/dev/null 2>&1; then
        echo "Applying ${patchname} to mDNSResponder source..."
        if ! patch -p0 -d "${SRC_DIR}" -f -s < "${patchfile}"; then
          echo "ERROR: Failed to apply patch ${patchname} to mDNSResponder source at ${SRC_DIR}" >&2
          exit 1
        fi
      elif patch -p0 -d "${SRC_DIR}" -f -R --dry-run < "${patchfile}" >/dev/null 2>&1; then
        echo "Patch ${patchname} already applied to mDNSResponder source, skipping."
      else
        echo "ERROR: Patch ${patchname} does not apply cleanly to mDNSResponder source at ${SRC_DIR} (partial or conflicting source detected)." >&2
        echo "Source tree is in an inconsistent partial patch state." >&2
        echo "To recover, reset mDNSResponder source before rebuilding:" >&2
        echo "  rm -rf src/mDNSResponder-${VERSION} && bash tools/fetch_world_sources.sh" >&2
        exit 1
      fi
    done
  fi
fi

make -C "${SRC_DIR}/mDNSPosix" clean

make -C "${SRC_DIR}/mDNSPosix" os=x tls=no Daemon Clients \
  CC="${CC} -target ${TARGET} -mmacosx-version-min=${MINVER} -isysroot ${SDKROOT}" \
  LINKOPTS="-isysroot ${SDKROOT} -lSystem" \
  CFLAGS="-Wno-error" \
  CFLAGS_OS="-DUSE_TCP_LOOPBACK -DMDNS_TCP_SERVERPORT_CENTENNIAL=53545 -DPANTHERA_MDNS_SKIP_INTERFACE_SCAN=1 -D'IsSystemServiceDisabled()=0' -no-cpp-precomp -Wno-declaration-after-statement -Wno-unused-but-set-variable -Wno-deprecated-declarations -D__MAC_OS_X_VERSION_MIN_REQUIRED=__MAC_OS_X_VERSION_10_4 -DHAVE_STRLCPY=1 -DTARGET_OS_MAC -D__APPLE_USE_RFC_2292 -DIPV6_2292_PKTINFO=IPV6_2292PKTINFO -include ${OUT_ROOT}/panthera_mdns_compat.h"

cp -f "${SRC_DIR}/mDNSPosix/build/prod/mdnsd" "${SBIN_DIR}/mDNSResponder"
cp -f "${SRC_DIR}/mDNSPosix/build/prod/libdns_sd.dylib" "${LIB_DIR}/libdns_sd.dylib"
cp -f "${SRC_DIR}/Clients/build/dns-sd" "${BIN_DIR}/dns-sd"

install_name_tool -id /usr/lib/libdns_sd.dylib "${LIB_DIR}/libdns_sd.dylib"
install_name_tool -change build/prod/libdns_sd.dylib /usr/lib/libdns_sd.dylib "${BIN_DIR}/dns-sd"
printf '%s\n' "${BUILD_REVISION}" > "${BUILD_STAMP}"

echo "Built mDNSResponder ${VERSION}:"
echo "  daemon: ${SBIN_DIR}/mDNSResponder"
echo "  client: ${BIN_DIR}/dns-sd"
echo "  lib:    ${LIB_DIR}/libdns_sd.dylib"
