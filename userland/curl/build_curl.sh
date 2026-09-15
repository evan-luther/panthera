#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="8.12.1"
DISTDIR="${PANTHERA_ROOT}/build/distfiles"
TARBALL="${DISTDIR}/curl-${VERSION}.tar.xz"
URL="https://curl.se/download/curl-${VERSION}.tar.xz"

OUT_ROOT="${PANTHERA_ROOT}/userland/curl"
BIN_DIR="${OUT_ROOT}/bin"
LIB_DIR="${OUT_ROOT}/lib"
INC_DIR="${OUT_ROOT}/include"

SYSROOT_LIB_DIR="${PANTHERA_ROOT}/userland/libsystem/build/sysroot/usr/lib"
OPENSSL_STAGE="${PANTHERA_ROOT}/userland/openssl/stage/usr"
ZLIB_STAGE="${PANTHERA_ROOT}/userland/zlib/stage"
NCURSES_BUILD_DIR="${PANTHERA_ROOT}/build/apple-ncurses-target"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
CC="$(xcrun -find clang)"
TARGET="x86_64-apple-darwin23.0"
MINVER="14.0"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# Build zlib if not already done
ZLIB_BUILD_SCRIPT="${PANTHERA_ROOT}/userland/zlib/build_zlib.sh"
if [[ -x "${ZLIB_BUILD_SCRIPT}" ]]; then
  bash "${ZLIB_BUILD_SCRIPT}"
fi

mkdir -p "${DISTDIR}" "${BIN_DIR}" "${LIB_DIR}" "${INC_DIR}"

if [[ "${PANTHERA_FORCE_REBUILD:-0}" != "1" \
   && -x "${BIN_DIR}/curl" ]]; then
  echo "curl ${VERSION} already staged"
  exit 0
fi

"${PANTHERA_ROOT}/tools/fetch_with_checksum.sh" "${URL}" "${TARBALL}"

WORKDIR="$(mktemp -d /tmp/panthera-curl-build.XXXXXX)"
trap 'rm -rf "${WORKDIR}"' EXIT

tar -C "${WORKDIR}" -xf "${TARBALL}"
SRCDIR="${WORKDIR}/curl-${VERSION}"
BUILDDIR="${WORKDIR}/build"
mkdir -p "${BUILDDIR}"

# Panthera: yield CPU between BIO I/O ops for QEMU/TCG single-CPU scheduling.
# Without these, the virtual NIC thread doesn't get time to process packets
# between non-blocking SSL_connect I/O operations.  This is NOT a select()
# workaround — select() works correctly after the libsystem_c fd_set fix.
OSSL_SRC="${SRCDIR}/lib/vtls/openssl.c"
if [[ -f "${OSSL_SRC}" ]]; then
  # After BIO write result, add a yield
  # After bio_write succeeds, poll briefly for response data
  sed -i '' '/return (int)nwritten;/{
    i\
  if(nwritten > 0) { struct pollfd _p; _p.fd = cf->conn->sock[cf->sockindex]; _p.events = POLLIN; poll(\&_p, 1, 200); }
  }' "${OSSL_SRC}"
  # On bio_read EAGAIN, poll for data instead of immediate retry
  sed -i '' 's/      BIO_set_retry_read(bio);/      { struct pollfd _p; _p.fd = cf->conn->sock[cf->sockindex]; _p.events = POLLIN; poll(\&_p, 1, 200); } BIO_set_retry_read(bio);/' "${OSSL_SRC}"
fi

TARGET_CFLAGS="-target ${TARGET} -mmacosx-version-min=${MINVER} -isysroot ${SDKROOT}"

cd "${BUILDDIR}"
BUILD_TRIPLE="$(uname -m)-apple-darwin$(uname -r | cut -d. -f1).0"

"${SRCDIR}/configure" \
  --host="${TARGET}" \
  --build="${BUILD_TRIPLE}" \
  --prefix=/usr \
  --with-openssl="${OPENSSL_STAGE}" \
  --with-zlib="${ZLIB_STAGE}" \
  --without-brotli \
  --without-zstd \
  --without-nghttp2 \
  --without-nghttp3 \
  --without-ngtcp2 \
  --without-libidn2 \
  --without-libpsl \
  --without-librtmp \
  --without-libssh2 \
  --disable-ldap \
  --disable-ldaps \
  --disable-rtsp \
  --disable-dict \
  --disable-telnet \
  --disable-tftp \
  --disable-pop3 \
  --disable-imap \
  --disable-smb \
  --disable-smtp \
  --disable-gopher \
  --disable-mqtt \
  --disable-manual \
  --disable-ntlm \
  --enable-http \
  --enable-ftp \
  --enable-file \
  --enable-proxy \
  --enable-cookies \
  --enable-ipv6 \
  --without-apple-idna \
  --disable-threaded-resolver \
  ac_cv_header_SystemConfiguration_SCDynamicStoreCopySpecific_h=no \
  ac_cv_func_SCDynamicStoreCopyProxies=no \
  curl_cv_apple=no \
  CC="${CC} ${TARGET_CFLAGS}" \
  CFLAGS="-O2" \
  CPPFLAGS="-I${OPENSSL_STAGE}/include -I${ZLIB_STAGE}/include -DTARGET_OS_IPHONE=1" \
  LDFLAGS="${TARGET_CFLAGS} -L${SYSROOT_LIB_DIR} -L${OPENSSL_STAGE}/lib -L${ZLIB_STAGE}/lib -Wl,-not_for_dyld_shared_cache -Wl,-bind_at_load" \
  LIBS="-lSystem"

make -j"${JOBS}"

# Copy the real binary (libtool puts it in .libs/)
if [[ -f "${BUILDDIR}/src/.libs/curl" ]]; then
  cp -f "${BUILDDIR}/src/.libs/curl" "${BIN_DIR}/curl"
else
  cp -f "${BUILDDIR}/src/curl" "${BIN_DIR}/curl"
fi
cp -f "${BUILDDIR}/lib/.libs/libcurl.4.dylib" "${LIB_DIR}/libcurl.4.dylib" 2>/dev/null || \
  cp -f "${BUILDDIR}/lib/.libs/libcurl.dylib" "${LIB_DIR}/libcurl.dylib" 2>/dev/null || true

echo "Built curl ${VERSION}: ${BIN_DIR}/curl"
echo "  $(${BIN_DIR}/curl --version 2>/dev/null | head -1 || echo '(cross-compiled, cannot run on host)')"
