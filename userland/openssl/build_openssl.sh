#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="3.5.5"
DISTDIR="${PANTHERA_ROOT}/build/distfiles"
TARBALL="${DISTDIR}/openssl-${VERSION}.tar.gz"
URL="https://github.com/openssl/openssl/releases/download/openssl-${VERSION}/openssl-${VERSION}.tar.gz"

OUT_ROOT="${PANTHERA_ROOT}/userland/openssl"
BIN_DIR="${OUT_ROOT}/bin"
LIB_DIR="${OUT_ROOT}/lib"
MODULES_DIR="${LIB_DIR}/ossl-modules"
ETC_SSL_DIR="${OUT_ROOT}/etc/ssl"
STAGE_DIR="${OUT_ROOT}/stage"

SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
CC="$(xcrun -find clang)"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
SYSROOT_LIB_DIR="${PANTHERA_ROOT}/userland/libsystem/build/sysroot/usr/lib"

mkdir -p "${DISTDIR}" "${BIN_DIR}" "${LIB_DIR}" "${MODULES_DIR}" "${ETC_SSL_DIR}"

sync_sysroot_libs() {
  mkdir -p "${SYSROOT_LIB_DIR}"
  if [[ -f "${LIB_DIR}/libcrypto.3.dylib" ]]; then
    cp -f "${LIB_DIR}/libcrypto.3.dylib" "${SYSROOT_LIB_DIR}/libcrypto.3.dylib"
    ln -sfn libcrypto.3.dylib "${SYSROOT_LIB_DIR}/libcrypto.dylib"
  fi
  if [[ -f "${LIB_DIR}/libssl.3.dylib" ]]; then
    cp -f "${LIB_DIR}/libssl.3.dylib" "${SYSROOT_LIB_DIR}/libssl.3.dylib"
    ln -sfn libssl.3.dylib "${SYSROOT_LIB_DIR}/libssl.dylib"
  fi
}

verify_guest_symbols() {
  local binary="$1"
  shift
  local tmpdir
  tmpdir="$(mktemp -d /tmp/panthera-openssl-syms.XXXXXX)"

  nm -u "${binary}" | sed 's/^_//' | sort -u > "${tmpdir}/undef.txt"
  {
    find "${SYSROOT_LIB_DIR}" -name '*.dylib' -type f -print0 | xargs -0 nm -gU 2>/dev/null
    for dep in "$@"; do
      nm -gU "${dep}"
    done
  } | awk '{print $3}' | sed 's/^_//' | sort -u > "${tmpdir}/exports.txt"

  if ! comm -23 "${tmpdir}/undef.txt" "${tmpdir}/exports.txt" > "${tmpdir}/missing.txt"; then
    rm -rf "${tmpdir}"
    echo "Failed to compare guest symbols for ${binary}" >&2
    exit 1
  fi

  if [[ -s "${tmpdir}/missing.txt" ]]; then
    echo "Guest-incompatible symbols detected in ${binary}:" >&2
    cat "${tmpdir}/missing.txt" >&2
    rm -rf "${tmpdir}"
    exit 1
  fi

  rm -rf "${tmpdir}"
}

if [[ "${PANTHERA_FORCE_REBUILD:-0}" != "1" \
   && -x "${BIN_DIR}/openssl" \
   && -f "${LIB_DIR}/libcrypto.3.dylib" \
   && -f "${LIB_DIR}/libssl.3.dylib" \
   && -f "${ETC_SSL_DIR}/openssl.cnf" ]]; then
  sync_sysroot_libs
  echo "OpenSSL ${VERSION} already staged"
  exit 0
fi

"${PANTHERA_ROOT}/tools/fetch_with_checksum.sh" "${URL}" "${TARBALL}"

WORKDIR="$(mktemp -d /tmp/panthera-openssl-build.XXXXXX)"
trap 'rm -rf "${WORKDIR}"' EXIT

tar -C "${WORKDIR}" -xf "${TARBALL}"
SRCDIR="${WORKDIR}/openssl-${VERSION}"

cd "${SRCDIR}"
if [[ "${PANTHERA_OPENSSL_INIT_TRACE:-0}" == "1" ]]; then
  patch -p0 < "${PANTHERA_ROOT}/userland/openssl/patches/panthera-openssl-init-trace.patch"
fi
perl ./Configure \
  darwin64-x86_64-cc \
  no-tests \
  no-asm \
  no-async \
  no-engine \
  no-module \
  shared \
  --prefix=/usr \
  --openssldir=/etc/ssl \
  CC="${CC} -target x86_64-apple-darwin23.0 -mmacosx-version-min=14.0 -isysroot ${SDKROOT} -DOPENSSL_NO_APPLE_CRYPTO_RANDOM"

make -j"${JOBS}"

rm -rf "${STAGE_DIR}"
make install_sw DESTDIR="${STAGE_DIR}"

rm -f "${BIN_DIR}/openssl"
rm -f "${LIB_DIR}/libcrypto.3.dylib" "${LIB_DIR}/libssl.3.dylib" \
  "${LIB_DIR}/libcrypto.dylib" "${LIB_DIR}/libssl.dylib"
rm -rf "${MODULES_DIR}"
rm -f "${ETC_SSL_DIR}/openssl.cnf"

cp -f "${STAGE_DIR}/usr/bin/openssl" "${BIN_DIR}/openssl"
cp -f "${STAGE_DIR}/usr/lib/libcrypto.3.dylib" "${LIB_DIR}/libcrypto.3.dylib"
cp -f "${STAGE_DIR}/usr/lib/libssl.3.dylib" "${LIB_DIR}/libssl.3.dylib"
ln -sfn libcrypto.3.dylib "${LIB_DIR}/libcrypto.dylib"
ln -sfn libssl.3.dylib "${LIB_DIR}/libssl.dylib"
sync_sysroot_libs

verify_guest_symbols "${LIB_DIR}/libcrypto.3.dylib"
verify_guest_symbols "${LIB_DIR}/libssl.3.dylib" "${LIB_DIR}/libcrypto.3.dylib"
verify_guest_symbols "${BIN_DIR}/openssl" "${LIB_DIR}/libcrypto.3.dylib" "${LIB_DIR}/libssl.3.dylib"

mkdir -p "${MODULES_DIR}"
if [[ -d "${STAGE_DIR}/usr/lib/ossl-modules" ]]; then
  cp -R "${STAGE_DIR}/usr/lib/ossl-modules/." "${MODULES_DIR}/"
fi

if [[ -f "${STAGE_DIR}/etc/ssl/openssl.cnf" ]]; then
  cp -f "${STAGE_DIR}/etc/ssl/openssl.cnf" "${ETC_SSL_DIR}/openssl.cnf"
else
  cp -f "${SRCDIR}/apps/openssl.cnf" "${ETC_SSL_DIR}/openssl.cnf"
fi

echo "Built OpenSSL ${VERSION}: ${BIN_DIR}/openssl"
