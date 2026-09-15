#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="20260508"
DISTFILE="${PANTHERA_ROOT}/build/distfiles/bmake-${VERSION}.tar.gz"
URL="https://ftp.netbsd.org/pub/NetBSD/misc/sjg/bmake-${VERSION}.tar.gz"
SRCDIR="${PANTHERA_ROOT}/src/bmake-${VERSION}"
OUT_ROOT="${PANTHERA_ROOT}/userland/bmake"
BIN_DIR="${OUT_ROOT}/bin"
SHARE_DIR="${OUT_ROOT}/share"
MK_DIR="${SHARE_DIR}/mk"

CC_BIN="${CC:-$(xcrun -find clang)}"
SDKROOT="${SDKROOT:-$(xcrun -sdk macosx --show-sdk-path)}"
TARGET="${TARGET:-x86_64-apple-darwin23.0}"
MINVER="${MINVER:-14.0}"

mkdir -p "${BIN_DIR}" "${MK_DIR}"

if [[ "${PANTHERA_FORCE_REBUILD:-0}" != "1" && -x "${BIN_DIR}/bmake" ]]; then
  echo "bmake already built"
  exit 0
fi

bash "${PANTHERA_ROOT}/tools/fetch_with_checksum.sh" "${URL}" "${DISTFILE}"

if [[ ! -d "${SRCDIR}" ]]; then
  mkdir -p "${SRCDIR}"
  tar -xzf "${DISTFILE}" -C "${SRCDIR}" --strip-components=1
fi

WORKDIR="$(mktemp -d /tmp/panthera-bmake-build.XXXXXX)"
trap 'rm -rf "${WORKDIR}"' EXIT

(
  cd "${WORKDIR}"
  CC="${CC_BIN} -target ${TARGET} -mmacosx-version-min=${MINVER} -isysroot ${SDKROOT}" \
    "${SRCDIR}/boot-strap" \
      --with-default-sys-path=/usr/share/mk \
      --without-filemon \
      --with-machine=x86_64 \
      --with-machine_arch=x86_64 \
      --with-force-machine=x86_64 \
      --with-force-machine-arch=x86_64 \
      --prefix=/usr \
      op=build
)

BMAKE_BIN="$(find "${WORKDIR}" -type f -name bmake -perm -111 | head -n 1)"
if [[ -z "${BMAKE_BIN}" ]]; then
  echo "Built bmake binary not found under ${WORKDIR}" >&2
  exit 1
fi

install -m 0755 "${BMAKE_BIN}" "${BIN_DIR}/bmake"
rm -rf "${MK_DIR}"
mkdir -p "${MK_DIR}"
cp -R "${SRCDIR}/mk/." "${MK_DIR}/"

bash "${PANTHERA_ROOT}/tools/audit_package.sh" "${BIN_DIR}/bmake"

echo "Built bmake: ${BIN_DIR}/bmake ($(wc -c < "${BIN_DIR}/bmake") bytes)"
