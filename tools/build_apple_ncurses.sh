#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SRC_DIR="${PANTHERA_ROOT}/src/apple-ncurses-71.100.2/ncurses"
HOST_BUILD_DIR="${PANTHERA_ROOT}/build/apple-ncurses-host-tools"
HOST_OUT_DIR="${HOST_BUILD_DIR}/out"
TARGET_BUILD_DIR="${PANTHERA_ROOT}/build/apple-ncurses-target"
SYSROOT_LIB_DIR="${PANTHERA_ROOT}/userland/libsystem/build/sysroot/usr/lib"
SDKROOT="${SDKROOT:-$(xcrun -sdk macosx --show-sdk-path)}"
HOST_CC="${HOST_CC:-$(xcrun -find clang)}"
TARGET_CC="${TARGET_CC:-$(xcrun -find clang)}"
TARGET_TRIPLE="${TARGET_TRIPLE:-x86_64-apple-darwin23.0}"
MINVER="${MINVER:-14.0}"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
FORCE_REBUILD="${PANTHERA_FORCE_REBUILD:-0}"
DARWIN_VARIANT_ALIASES=(
  -Wl,-alias,_putp,_putp\$NCURSES60
)
DARWIN_VARIANT_ALIAS_FLAGS='-Wl,-alias,_putp,_putp\$$NCURSES60'

BUILD_HOST_TOOLS=0
BUILD_TARGET_LIB=0

usage() {
  cat <<'EOF'
Usage: build_apple_ncurses.sh [--host-tools] [--target-lib]

Builds Panthera's Apple-sourced ncurses artifacts:
  --host-tools   Build host-side tic/infocmp for terminfo staging
  --target-lib   Build the guest x86_64 ncurses dylib and stage it into the sysroot

If no flags are provided, both outputs are built.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --host-tools)
      BUILD_HOST_TOOLS=1
      shift
      ;;
    --target-lib)
      BUILD_TARGET_LIB=1
      shift
      ;;
    --help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ "${BUILD_HOST_TOOLS}" != "1" && "${BUILD_TARGET_LIB}" != "1" ]]; then
  BUILD_HOST_TOOLS=1
  BUILD_TARGET_LIB=1
fi

if [[ ! -d "${SRC_DIR}" ]]; then
  echo "Apple ncurses source not found: ${SRC_DIR}" >&2
  exit 1
fi

common_configure_flags=(
  --with-shared
  --without-normal
  --without-debug
  --enable-termcap
  --enable-widec
  --with-abi-version=5.4
  --without-cxx-binding
  --without-cxx
  --without-ada
  --without-tests
  --mandir=/usr/share/man
  --datarootdir=/usr/share
  --with-default-terminfo-dir=/usr/share/terminfo
  --with-terminfo-dirs=/usr/share/terminfo
)

build_host_tools() {
  if [[ "${FORCE_REBUILD}" != "1" \
     && -x "${HOST_OUT_DIR}/bin/tic" \
     && -x "${HOST_OUT_DIR}/bin/infocmp" ]]; then
    return
  fi

  mkdir -p "${HOST_BUILD_DIR}" "${HOST_OUT_DIR}"

  if [[ "${FORCE_REBUILD}" == "1" && -f "${HOST_BUILD_DIR}/Makefile" ]]; then
    make -C "${HOST_BUILD_DIR}" distclean >/dev/null 2>&1 || true
  fi

  if [[ ! -f "${HOST_BUILD_DIR}/Makefile" ]]; then
    (
      cd "${HOST_BUILD_DIR}"
      /bin/sh "${SRC_DIR}/configure" \
        "${common_configure_flags[@]}" \
        --prefix="${HOST_OUT_DIR}" \
        CC="${HOST_CC}" \
        CFLAGS="-O2" \
        CPPFLAGS="-isysroot ${SDKROOT}" \
        LDFLAGS="-isysroot ${SDKROOT}"
    )
  fi

  mkdir -p "${HOST_BUILD_DIR}/obj_s"
  ln -sfn ../ncurses/init_keytry.h "${HOST_BUILD_DIR}/obj_s/init_keytry.h"
  ln -sfn ../progs/termsort.c "${HOST_BUILD_DIR}/obj_s/termsort.c"
  ln -sfn ../progs/transform.h "${HOST_BUILD_DIR}/obj_s/transform.h"

  make -C "${HOST_BUILD_DIR}" -j1
  make -C "${HOST_BUILD_DIR}" install.progs
  mkdir -p "${HOST_OUT_DIR}/lib"
  cp -f "${HOST_BUILD_DIR}/lib/libncursesw.5.4.dylib" \
    "${HOST_OUT_DIR}/lib/libncursesw.5.4.dylib"
  ln -sfn libncursesw.5.4.dylib "${HOST_OUT_DIR}/lib/libncursesw.dylib"
}

build_target_lib() {
  local target_cc_flags
  target_cc_flags="-target ${TARGET_TRIPLE} -mmacosx-version-min=${MINVER} -isysroot ${SDKROOT}"

  if [[ "${FORCE_REBUILD}" != "1" \
     && -f "${SYSROOT_LIB_DIR}/libncurses.5.4.dylib" \
     && -f "${TARGET_BUILD_DIR}/include/curses.h" \
     && -f "${TARGET_BUILD_DIR}/include/term.h" \
     && "$(nm -gU "${SYSROOT_LIB_DIR}/libncurses.5.4.dylib" 2>/dev/null | awk '{print $NF}' | grep -Fx '_putp$NCURSES60' || true)" != "" ]]; then
    return
  fi

  mkdir -p "${TARGET_BUILD_DIR}" "${SYSROOT_LIB_DIR}"

  if [[ -f "${TARGET_BUILD_DIR}/Makefile" ]]; then
    make -C "${TARGET_BUILD_DIR}" distclean >/dev/null 2>&1 || true
  fi

  if [[ ! -f "${TARGET_BUILD_DIR}/Makefile" ]]; then
    (
      cd "${TARGET_BUILD_DIR}"
      /bin/sh "${SRC_DIR}/configure" \
        "${common_configure_flags[@]}" \
        --host="${TARGET_TRIPLE}" \
        CC="${TARGET_CC} ${target_cc_flags}" \
        CFLAGS="-O2" \
        CPPFLAGS="-isysroot ${SDKROOT}" \
        LDFLAGS="${target_cc_flags}"
    )
  fi

  DARWIN_VARIANT_ALIAS_FLAGS="${DARWIN_VARIANT_ALIAS_FLAGS}" perl -0pi -e \
    's/\s+-Wl,-alias,_putp,_putp(?:\\?\$\$|\\?\$|\$\$)NCURSES60//g' \
    "${TARGET_BUILD_DIR}/ncurses/Makefile"
  DARWIN_VARIANT_ALIAS_FLAGS="${DARWIN_VARIANT_ALIAS_FLAGS}" perl -0pi -e \
    's/(^MK_SHARED_LIB\s*=.*?)(\s+-o \$@)/$1 $ENV{DARWIN_VARIANT_ALIAS_FLAGS}$2/m' \
    "${TARGET_BUILD_DIR}/ncurses/Makefile"

  make -C "${TARGET_BUILD_DIR}" -j1

  cp -f "${TARGET_BUILD_DIR}/lib/libncursesw.5.4.dylib" \
    "${SYSROOT_LIB_DIR}/libncurses.5.4.dylib"
  install_name_tool -id /usr/lib/libncurses.5.dylib \
    "${SYSROOT_LIB_DIR}/libncurses.5.4.dylib"
  ln -sfn libncurses.5.4.dylib "${SYSROOT_LIB_DIR}/libncurses.5.dylib"
  ln -sfn libncurses.5.dylib "${SYSROOT_LIB_DIR}/libncurses.dylib"
  ln -sfn libncurses.5.dylib "${SYSROOT_LIB_DIR}/libcurses.dylib"
  ln -sfn libncurses.5.dylib "${SYSROOT_LIB_DIR}/libtermcap.dylib"
}

if [[ "${BUILD_HOST_TOOLS}" == "1" ]]; then
  build_host_tools
fi

if [[ "${BUILD_TARGET_LIB}" == "1" ]]; then
  build_target_lib
fi
