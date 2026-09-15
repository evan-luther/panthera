#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="8.7.1"
DISTDIR="${PANTHERA_ROOT}/build/distfiles"
TARBALL="${DISTDIR}/nano-${VERSION}.tar.xz"
URL="https://www.nano-editor.org/dist/v8/nano-${VERSION}.tar.xz"

OUT_ROOT="${PANTHERA_ROOT}/userland/nano"
BIN_DIR="${OUT_ROOT}/bin"
ETC_DIR="${OUT_ROOT}/etc"
SHARE_DIR="${OUT_ROOT}/share/nano"

NCURSES_SRC="${PANTHERA_ROOT}/src/apple-ncurses-71.100.2/ncurses"
NCURSES_BUILD_DIR="${PANTHERA_ROOT}/build/apple-ncurses-target"
NCURSES_BUILD_SCRIPT="${PANTHERA_ROOT}/tools/build_apple_ncurses.sh"
NCURSES_LIBDIR="${PANTHERA_ROOT}/userland/libsystem/build/sysroot/usr/lib"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
CC="$(xcrun -find clang)"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

mkdir -p "${DISTDIR}" "${BIN_DIR}" "${ETC_DIR}" "${SHARE_DIR}"

if [[ -x "${NCURSES_BUILD_SCRIPT}" ]]; then
  bash "${NCURSES_BUILD_SCRIPT}" --target-lib
fi

if [[ "${PANTHERA_FORCE_REBUILD:-0}" != "1" \
   && -x "${BIN_DIR}/nano" \
   && -f "${ETC_DIR}/nanorc" ]]; then
  echo "nano ${VERSION} already staged"
  exit 0
fi

"${PANTHERA_ROOT}/tools/fetch_with_checksum.sh" "${URL}" "${TARBALL}"

WORKDIR="$(mktemp -d /tmp/panthera-nano-build.XXXXXX)"
trap 'rm -rf "${WORKDIR}"' EXIT

tar -C "${WORKDIR}" -xf "${TARBALL}"
SRCDIR="${WORKDIR}/nano-${VERSION}"
BUILDDIR="${WORKDIR}/build"
mkdir -p "${BUILDDIR}"

cd "${BUILDDIR}"
"${SRCDIR}/configure" \
  --host=x86_64-apple-darwin23.0 \
  --prefix=/usr \
  --sysconfdir=/etc \
  --disable-nls \
  --disable-libmagic \
  --disable-utf8 \
  CC="${CC} -target x86_64-apple-darwin23.0 -mmacosx-version-min=14.0 -isysroot ${SDKROOT}" \
  CPPFLAGS="-I${NCURSES_BUILD_DIR}/include -I${NCURSES_SRC}/include -I${NCURSES_SRC}/ncurses" \
  LDFLAGS="-L${NCURSES_LIBDIR}" \
  LIBS="-lncurses.5.4 -lSystem"

make -j"${JOBS}"

cp -f "${BUILDDIR}/src/nano" "${BIN_DIR}/nano"
rm -f "${BIN_DIR}/nano.bin"
cp -f "${BUILDDIR}/doc/sample.nanorc" "${ETC_DIR}/nanorc"

rm -f "${SHARE_DIR}"/*.nanorc
cp -f "${SRCDIR}"/syntax/*.nanorc "${SHARE_DIR}/"

echo "Built nano ${VERSION}: ${BIN_DIR}/nano"
