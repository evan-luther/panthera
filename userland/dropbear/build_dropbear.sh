#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRCDIR="${PANTHERA_ROOT}/src/dropbear-2024.86"
OUT_ROOT="${PANTHERA_ROOT}/userland/dropbear"
BIN_DIR="${OUT_ROOT}/bin"
SBIN_DIR="${OUT_ROOT}/sbin"

SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
CC="$(xcrun -find clang)"
RANLIB="$(xcrun -find ranlib)"
TARGET="x86_64-apple-darwin23.0"
MINVER="14.0"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

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

mkdir -p "${BIN_DIR}" "${SBIN_DIR}"

if [[ "${PANTHERA_FORCE_REBUILD:-0}" != "1" \
   && -x "${SBIN_DIR}/dropbear" ]]; then
  echo "dropbear already staged"
  exit 0
fi

WORKDIR="$(mktemp -d /tmp/panthera-dropbear-build.XXXXXX)"
cleanup() { if [[ "${BUILD_OK:-0}" == "1" ]]; then rm -rf "${WORKDIR}"; fi; }
trap cleanup EXIT

# Copy source to workdir (Dropbear modifies files during build)
cp -R "${SRCDIR}/." "${WORKDIR}/"

TARGET_CFLAGS="-target ${TARGET} -mmacosx-version-min=${MINVER} -isysroot ${SDKROOT}"

cd "${WORKDIR}"

# Configure Dropbear for Panthera
./configure \
  --host="${TARGET}" \
  --prefix=/usr \
  --disable-zlib \
  --disable-pam \
  --disable-syslog \
  --disable-shadow \
  --disable-lastlog \
  --disable-utmp \
  --disable-utmpx \
  --disable-wtmp \
  --disable-wtmpx \
  --disable-loginfunc \
  --disable-pututline \
  --disable-pututxline \
  CC="${CC}" \
  RANLIB="${RANLIB} -no_warning_for_no_symbols" \
  CFLAGS="${TARGET_CFLAGS} -O2 -DDROPBEAR_SVR_AGENTFWD=0" \
  CPPFLAGS="${TARGET_CFLAGS}" \
  LDFLAGS="${TARGET_CFLAGS} -L${SYSROOT}/usr/lib -L${SYSROOT}/usr/lib/system -Wl,-syslibroot,${SYSROOT} -nodefaultlibs -Wl,-not_for_dyld_shared_cache" \
  LIBS="${SYSROOT}/usr/lib/libSystem.B.dylib ${CLANG_RT}"

# Patch generated Makefile so scp links $(LIBS) (libSystem.B + CLANG_RT under -nodefaultlibs)
sed -i '' 's|\$(CC) \$(LDFLAGS) -o \$@\$(EXEEXT) \$(SCPOBJS)|$(CC) $(LDFLAGS) -o $@$(EXEEXT) $(SCPOBJS) $(LIBS)|' Makefile
if ! grep -Fq '$(SCPOBJS) $(LIBS)' Makefile; then
  echo "ERROR: Failed to add Panthera runtime inputs to the Dropbear scp link rule" >&2
  exit 1
fi

# Build
make -j"${JOBS}" PROGRAMS="dropbear dbclient dropbearkey dropbearconvert scp"

# Stage
cp -f dropbear "${SBIN_DIR}/dropbear"
cp -f dbclient "${BIN_DIR}/dbclient"
cp -f dropbearkey "${BIN_DIR}/dropbearkey"
cp -f dropbearconvert "${BIN_DIR}/dropbearconvert"
if [[ -f scp ]]; then
  cp -f scp "${BIN_DIR}/scp_dropbear"
fi

# Fix install names
for f in "${SBIN_DIR}"/* "${BIN_DIR}"/*; do
  [[ -f "$f" ]] || continue
  install_name_tool -change "${SYSROOT}/usr/lib/libz.1.dylib" /usr/lib/libz.1.dylib "$f" 2>/dev/null || true
done

BUILD_OK=1
echo "Built dropbear:"
echo "  dropbear: ${SBIN_DIR}/dropbear"
echo "  dbclient: ${BIN_DIR}/dbclient"
echo "  dropbearkey: ${BIN_DIR}/dropbearkey"
