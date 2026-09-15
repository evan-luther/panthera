#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

SRCDIR="${PANTHERA_ROOT}/src/less/less"
OUT_ROOT="${PANTHERA_ROOT}/userland/less"
BIN_DIR="${OUT_ROOT}/bin"

SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
CC="$(xcrun -find clang)"
TARGET="x86_64-apple-darwin23.0"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

mkdir -p "${BIN_DIR}"

if [[ "${PANTHERA_FORCE_REBUILD:-0}" != "1" \
   && -x "${BIN_DIR}/less" ]]; then
  echo "less already staged"
  exit 0
fi

WORKDIR="$(mktemp -d /tmp/panthera-less-build.XXXXXX)"
trap 'rm -rf "${WORKDIR}"' EXIT

cd "${WORKDIR}"

"${SRCDIR}/configure" \
  --host="${TARGET}" \
  --with-regex=posix \
  CC="${CC} -target ${TARGET} -mmacosx-version-min=14.0 -isysroot ${SDKROOT}" \
  CFLAGS="-O2" \
  LDFLAGS="-isysroot ${SDKROOT} -L${SYSROOT}/usr/lib -lncurses.5.4 -lSystem"

make -j"${JOBS}"

cp -f less      "${BIN_DIR}/less"
cp -f lesskey   "${BIN_DIR}/lesskey"
cp -f lessecho  "${BIN_DIR}/lessecho"

echo "Built less: ${BIN_DIR}/less"

bash "${PANTHERA_ROOT}/tools/audit_package.sh" \
  "${BIN_DIR}/less" \
  "${BIN_DIR}/lesskey" \
  "${BIN_DIR}/lessecho"
