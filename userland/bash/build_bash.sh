#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="5.2.37"
DISTDIR="${PANTHERA_ROOT}/build/distfiles"
TARBALL="${DISTDIR}/bash-${VERSION}.tar.gz"
URL="https://ftp.gnu.org/gnu/bash/bash-${VERSION}.tar.gz"

OUT_ROOT="${PANTHERA_ROOT}/userland/bash"
BIN_DIR="${OUT_ROOT}/bin"

SYSROOT_LIB_DIR="${PANTHERA_ROOT}/userland/libsystem/build/sysroot/usr/lib"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
CC="$(xcrun -find clang)"
TARGET="x86_64-apple-darwin23.0"
MINVER="14.0"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

mkdir -p "${DISTDIR}" "${BIN_DIR}"

if [[ "${PANTHERA_FORCE_REBUILD:-0}" != "1" \
   && -x "${BIN_DIR}/bash" ]]; then
  echo "bash ${VERSION} already staged"
  exit 0
fi

"${PANTHERA_ROOT}/tools/fetch_with_checksum.sh" "${URL}" "${TARBALL}"

WORKDIR="$(mktemp -d /tmp/panthera-bash-build.XXXXXX)"
trap 'rm -rf "${WORKDIR}"' EXIT

tar -C "${WORKDIR}" -xf "${TARBALL}"
SRCDIR="${WORKDIR}/bash-${VERSION}"
BUILDDIR="${WORKDIR}/build"
mkdir -p "${BUILDDIR}"

TARGET_CFLAGS="-target ${TARGET} -mmacosx-version-min=${MINVER} -isysroot ${SDKROOT}"

cd "${BUILDDIR}"

"${SRCDIR}/configure" \
  --host="${TARGET}" \
  --build="${TARGET}" \
  --prefix=/usr \
  --without-bash-malloc \
  --disable-nls \
  --without-installed-readline \
  --enable-readline=no \
  --disable-debugger \
  bash_cv_have_mbstate_t=yes \
  bash_cv_func_sigsetjmp=present \
  bash_cv_job_control_missing=present \
  bash_cv_sys_named_pipes=present \
  bash_cv_ulimit_maxfds=yes \
  bash_cv_getcwd_malloc=yes \
  bash_cv_func_ctype_nonascii=yes \
  bash_cv_printf_a_format=yes \
  bash_cv_opendir_not_robust=no \
  bash_cv_func_snprintf=yes \
  bash_cv_dup2_broken=no \
  bash_cv_pgrp_pipe=no \
  bash_cv_sys_siglist=yes \
  bash_cv_must_reinstall_sighandlers=no \
  bash_cv_getenv_redef=yes \
  bash_cv_unusable_rtsigs=no \
  ac_cv_sizeof_long=8 \
  ac_cv_sizeof_char_p=8 \
  ac_cv_sizeof_size_t=8 \
  ac_cv_sizeof_int=4 \
  ac_cv_sizeof_double=8 \
  ac_cv_sizeof_wchar_t=4 \
  ac_cv_c_long_double=yes \
  CC="${CC} ${TARGET_CFLAGS}" \
  CFLAGS="-O2" \
  LDFLAGS="-isysroot ${SDKROOT} -lSystem"

make -j"${JOBS}"

cp -f "${BUILDDIR}/bash" "${BIN_DIR}/bash"

echo "Built bash ${VERSION}: ${BIN_DIR}/bash"
echo "  $(file "${BIN_DIR}/bash")"

# Run package audit
bash "${PANTHERA_ROOT}/tools/audit_package.sh" "${BIN_DIR}/bash"
