#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="9.7p1"
DISTDIR="${PANTHERA_ROOT}/build/distfiles"
TARBALL="${DISTDIR}/openssh-${VERSION}.tar.gz"
URL="https://cdn.openbsd.org/pub/OpenBSD/OpenSSH/portable/openssh-${VERSION}.tar.gz"

OUT_ROOT="${PANTHERA_ROOT}/userland/openssh"
BIN_DIR="${OUT_ROOT}/bin"
SBIN_DIR="${OUT_ROOT}/sbin"
LIBEXEC_DIR="${OUT_ROOT}/libexec"

SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"
SYSROOT_LIB_DIR="${SYSROOT}/usr/lib"
OPENSSL_STAGE="${PANTHERA_ROOT}/userland/openssl/stage/usr"
ZLIB_STAGE="${PANTHERA_ROOT}/userland/zlib/stage"
LIBEDIT_DIR="${PANTHERA_ROOT}/userland/libedit"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
CC="$(xcrun -find clang)"
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

mkdir -p "${DISTDIR}" "${BIN_DIR}" "${SBIN_DIR}" "${LIBEXEC_DIR}"

if [[ "${PANTHERA_FORCE_REBUILD:-0}" != "1" \
   && -x "${SBIN_DIR}/sshd" ]]; then
  echo "openssh ${VERSION} already staged"
  exit 0
fi

"${PANTHERA_ROOT}/tools/fetch_with_checksum.sh" "${URL}" "${TARBALL}"

WORKDIR="$(mktemp -d /tmp/panthera-openssh-build.XXXXXX)"
cleanup() { if [[ "${OPENSSH_BUILD_OK:-0}" == "1" ]]; then rm -rf "${WORKDIR}"; fi; }
trap cleanup EXIT

tar -C "${WORKDIR}" -xf "${TARBALL}"
SRCDIR="${WORKDIR}/openssh-${VERSION}"

TARGET_CFLAGS="-target ${TARGET} -mmacosx-version-min=${MINVER} -isysroot ${SDKROOT}"

cd "${SRCDIR}"
BUILD_TRIPLE="$(uname -m)-apple-darwin$(uname -r | cut -d. -f1).0"

# Configure portable OpenSSH for Panthera
./configure \
  --host="${TARGET}" \
  --build="${BUILD_TRIPLE}" \
  --prefix=/usr \
  --sysconfdir=/etc/ssh \
  --with-ssl-dir="${OPENSSL_STAGE}" \
  --with-zlib="${ZLIB_STAGE}" \
  --with-libedit="${LIBEDIT_DIR}" \
  --with-privsep-user=nobody \
  --with-privsep-path=/var/empty \
  --without-pam \
  --without-kerberos5 \
  --with-sandbox=no \
  --without-selinux \
  --without-audit \
  --without-bsm \
  --disable-strip \
  --disable-utmpx \
  --disable-wtmpx \
  --disable-lastlog \
  --disable-security-key \
  --without-security-key-builtin \
  ac_cv_lib_sandbox_sandbox_apply=no \
  ac_cv_search_res_query=no \
  ac_cv_search_dn_expand=no \
  ac_cv_lib_resolv_res_query=no \
  ac_cv_search_getrrsetbyname=no \
  CC="${CC}" \
  CFLAGS="${TARGET_CFLAGS} -DPANTHERA -O2 -I${OPENSSL_STAGE}/include -I${ZLIB_STAGE}/include -I${LIBEDIT_DIR}/include" \
  CPPFLAGS="${TARGET_CFLAGS} -DPANTHERA -I${OPENSSL_STAGE}/include -I${ZLIB_STAGE}/include -I${LIBEDIT_DIR}/include" \
  LDFLAGS="${TARGET_CFLAGS} -L${SYSROOT_LIB_DIR} -L${SYSROOT_LIB_DIR}/system -L${OPENSSL_STAGE}/lib -L${ZLIB_STAGE}/lib -L${LIBEDIT_DIR}/lib -Wl,-syslibroot,${SYSROOT} -nodefaultlibs -Wl,-not_for_dyld_shared_cache -Wl,-dead_strip_dylibs" \
  LIBS="${SYSROOT_LIB_DIR}/libSystem.B.dylib ${CLANG_RT}"

# Fix cross-compilation detection issues in config.h.
# These functions exist on macOS but cross-compile can't run test programs.
for sym in HAVE_RRESVPORT_AF HAVE_DEV_PTMX HAVE_REALPATH HAVE_PSELECT; do
  sed -i '' "s|/\* #undef ${sym} \*/|#define ${sym} 1|" config.h
done
patch -p0 < "${PANTHERA_ROOT}/userland/openssh/patches/panthera-sshd-privsep-sigchld.patch"
patch -p0 < "${PANTHERA_ROOT}/userland/openssh/patches/panthera-openssl-no-config-init.patch"
patch -p0 < "${PANTHERA_ROOT}/userland/openssh/patches/panthera-getrrsetbyname-nodata.patch"
patch -p0 < "${PANTHERA_ROOT}/userland/openssh/patches/panthera-uidswap-no-apple-initgroups.patch"
patch -p0 < "${PANTHERA_ROOT}/userland/openssh/patches/panthera-session-no-initgroups.patch"
patch -p0 < "${PANTHERA_ROOT}/userland/openssh/patches/panthera-sshd-reexec-no-listen-defaults.patch"
if [[ "${PANTHERA_OPENSSH_ACCEPT_TRACE:-0}" == "1" ]]; then
  patch -p0 < "${PANTHERA_ROOT}/userland/openssh/patches/panthera-sshd-startup-trace.patch"
  patch -p0 < "${PANTHERA_ROOT}/userland/openssh/patches/panthera-sshd-accept-child-trace.patch"
  patch -p0 < "${PANTHERA_ROOT}/userland/openssh/patches/panthera-sshd-child-flow-trace.patch"
  patch -p0 < "${PANTHERA_ROOT}/userland/openssh/patches/panthera-sshd-early-main-trace.patch"
  patch -p0 < "${PANTHERA_ROOT}/userland/openssh/patches/panthera-sshd-seed-rng-trace.patch"
  patch -p0 < "${PANTHERA_ROOT}/userland/openssh/patches/panthera-monitor-trace.patch"
fi
if [[ "${PANTHERA_OPENSSH_CONSOLE_TRACE:-0}" == "1" ]]; then
  patch -p0 < "${PANTHERA_ROOT}/userland/openssh/patches/panthera-session-serverloop-console-trace.patch"
else
  patch -p0 < "${PANTHERA_ROOT}/userland/openssh/patches/panthera-serverloop-flush-after-child-collect.patch"
fi
if [[ "${PANTHERA_OPENSSH_CHILD_CONSOLE_TRACE:-0}" == "1" ]]; then
  patch -p0 < "${PANTHERA_ROOT}/userland/openssh/patches/panthera-session-child-console-trace.patch"
fi
if [[ "${PANTHERA_OPENSSH_CHANNEL_CONSOLE_TRACE:-0}" == "1" ]]; then
  patch -p0 < "${PANTHERA_ROOT}/userland/openssh/patches/panthera-channel-console-trace.patch"
fi
if [[ "${PANTHERA_OPENSSH_PACKET_CONSOLE_TRACE:-0}" == "1" ]]; then
  patch -p0 < "${PANTHERA_ROOT}/userland/openssh/patches/panthera-packet-console-trace.patch"
fi
if [[ "${PANTHERA_OPENSSH_SFTP_SERVER_TRACE:-0}" == "1" ]]; then
  patch -p0 < "${PANTHERA_ROOT}/userland/openssh/patches/panthera-sftp-server-trace.patch"
  printf '\n#define PANTHERA_SFTP_SERVER_TRACE 1\n' >> config.h
fi
if [[ "${PANTHERA_OPENSSH_NATIVE_POLL:-0}" == "1" ]]; then
  patch -p0 < "${PANTHERA_ROOT}/userland/openssh/patches/panthera-ppoll-native-poll.patch"
  sed -i '' 's|/\* #undef HAVE_POLL \*/|#define HAVE_POLL 1|' config.h
  sed -i '' 's|#define BROKEN_POLL 1|/* #undef BROKEN_POLL */|' config.h
  if grep -q '^#define BROKEN_POLL 1' config.h; then
    echo "error: BROKEN_POLL remained enabled after Panthera poll configuration" >&2
    exit 1
  fi
fi
# statvfs/fstatvfs: macOS has the header but the functions are in libc, not our sysroot.
# The SDK header conflicts with OpenSSH's compat struct. Remove the system header detection
# so OpenSSH uses its own struct AND its own implementation.
sed -i '' 's|#define HAVE_SYS_STATVFS_H 1|/* #undef HAVE_SYS_STATVFS_H */|' config.h
sed -i '' 's|#define HAVE_STATVFS 1|/* #undef HAVE_STATVFS */|' config.h
sed -i '' 's|#define HAVE_FSTATVFS 1|/* #undef HAVE_FSTATVFS */|' config.h
# ptrace: sysroot has __ptrace but not _ptrace. Disable the sys/ptrace.h detection
# so OpenSSH doesn't try to use PT_DENY_ATTACH (not needed on Panthera anyway)
sed -i '' 's|#define HAVE_SYS_PTRACE_H 1|/* #undef HAVE_SYS_PTRACE_H */|' config.h
# Disable setproctitle (SPT_REUSEARGV crashes on Panthera — argv buffer issue)
sed -i '' 's|#define SPT_TYPE SPT_REUSEARGV|#define SPT_TYPE SPT_NONE|' config.h

make -j"${JOBS}"

# Stage outputs
cp -f sshd "${SBIN_DIR}/sshd"
for bin in ssh scp sftp ssh-keygen ssh-agent ssh-add ssh-keyscan; do
  if [[ -f "${bin}" ]]; then
    cp -f "${bin}" "${BIN_DIR}/${bin}"
  fi
done
for libexec_bin in sftp-server sshd-session ssh-keysign ssh-pkcs11-helper ssh-sk-helper; do
  if [[ -f "${libexec_bin}" ]]; then
    cp -f "${libexec_bin}" "${LIBEXEC_DIR}/${libexec_bin}"
  fi
done

# Fix install names: replace build-host paths with runtime paths
for f in "${SBIN_DIR}"/* "${BIN_DIR}"/* "${LIBEXEC_DIR}"/*; do
  [[ -f "$f" ]] || continue
  install_name_tool -change "${ZLIB_STAGE}/lib/libz.1.dylib" /usr/lib/libz.1.dylib "$f" 2>/dev/null || true
  install_name_tool -change "${LIBEDIT_DIR}/lib/libedit.3.dylib" /usr/lib/libedit.3.dylib "$f" 2>/dev/null || true
done

echo "Built openssh ${VERSION}:"
echo "  sshd:       ${SBIN_DIR}/sshd"
echo "  ssh:        ${BIN_DIR}/ssh"
echo "  scp:        ${BIN_DIR}/scp"
echo "  sftp:       ${BIN_DIR}/sftp"
echo "  ssh-keygen: ${BIN_DIR}/ssh-keygen"
echo "  sftp-server: ${LIBEXEC_DIR}/sftp-server"
OPENSSH_BUILD_OK=1
