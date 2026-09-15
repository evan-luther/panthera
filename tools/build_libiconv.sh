#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SRC_DIR="${PANTHERA_ROOT}/src/libiconv-1.17"
SYSROOT_LIB_DIR="${PANTHERA_ROOT}/userland/libsystem/build/sysroot/usr/lib"
SDKROOT="${SDKROOT:-$(xcrun -sdk macosx --show-sdk-path)}"
TARGET_CC="${TARGET_CC:-$(xcrun -find clang)}"
TARGET_TRIPLE="${TARGET_TRIPLE:-x86_64-apple-darwin23.0}"
MINVER="${MINVER:-14.0}"
FORCE_REBUILD="${PANTHERA_FORCE_REBUILD:-0}"

DARWIN_COMPAT_ALIASES=(
  -Wl,-alias,_libiconv,_iconv
  -Wl,-alias,_libiconv_open,_iconv_open
  -Wl,-alias,_libiconv_close,_iconv_close
)

has_iconv_aliases() {
  local dylib="$1"

  [[ -f "${dylib}" ]] || return 1
  for sym in _iconv _iconv_open _iconv_close; do
    if ! nm -gU "${dylib}" 2>/dev/null | awk '{print $NF}' | grep -Fx "${sym}" >/dev/null; then
      return 1
    fi
  done
}

if [[ ! -d "${SRC_DIR}" ]]; then
  echo "libiconv source not found: ${SRC_DIR}" >&2
  exit 1
fi

mkdir -p "${SYSROOT_LIB_DIR}"

if [[ "${FORCE_REBUILD}" != "1" ]] && has_iconv_aliases "${SYSROOT_LIB_DIR}/libiconv.2.dylib"; then
  exit 0
fi

target_cc_flags="-target ${TARGET_TRIPLE} -mmacosx-version-min=${MINVER} -isysroot ${SDKROOT}"

# Configure artifacts embed the compiler and SDK paths. Recreate them whenever
# an actual build is required so partial and forced builds cannot reuse a
# different Xcode/CommandLineTools tuple.
if [[ -f "${SRC_DIR}/Makefile" ]]; then
  make -C "${SRC_DIR}" distclean >/dev/null 2>&1 || true
fi
rm -f \
  "${SRC_DIR}/Makefile" \
  "${SRC_DIR}/lib/Makefile" \
  "${SRC_DIR}/libtool" \
  "${SRC_DIR}/config.status" \
  "${SRC_DIR}/config.log"
(
  cd "${SRC_DIR}"
  ./configure \
    --host="${TARGET_TRIPLE}" \
    --prefix=/usr \
    CC="${TARGET_CC} ${target_cc_flags}" \
    CFLAGS="-O2" \
    CPPFLAGS="-isysroot ${SDKROOT}" \
    LDFLAGS="${target_cc_flags}"
)

make -C "${SRC_DIR}/lib" clean >/dev/null 2>&1 || true
make -C "${SRC_DIR}/lib" \
  CC="${TARGET_CC} ${target_cc_flags}" \
  LDFLAGS="${target_cc_flags} ${DARWIN_COMPAT_ALIASES[*]}"

cp -f "${SRC_DIR}/lib/.libs/libiconv.2.dylib" "${SYSROOT_LIB_DIR}/libiconv.2.dylib"
install_name_tool -id /usr/lib/libiconv.2.dylib "${SYSROOT_LIB_DIR}/libiconv.2.dylib"
ln -sfn libiconv.2.dylib "${SYSROOT_LIB_DIR}/libiconv.dylib"
