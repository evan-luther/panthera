#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="1030.6.3"
COMMIT="920a2b45080fb9badf31bf675f03b19973f0dd4f"
SHORT_COMMIT="${COMMIT:0:7}"
DISTFILE="${PANTHERA_ROOT}/build/distfiles/cctools-${VERSION}-${SHORT_COMMIT}.tar.gz"
URL="https://github.com/apple-oss-distributions/cctools/archive/${COMMIT}.tar.gz"
SRCDIR="${PANTHERA_ROOT}/src/cctools-${VERSION}"
OUT_ROOT="${PANTHERA_ROOT}/userland/cctools"
BIN_DIR="${OUT_ROOT}/bin"

CC_BIN="${CC:-$(xcrun -find clang)}"
SDKROOT="${SDKROOT:-$(xcrun -sdk macosx --show-sdk-path)}"
TARGET="${TARGET:-x86_64-apple-darwin23.0}"
MINVER="${MINVER:-14.0}"

mkdir -p "${BIN_DIR}"

if [[ "${PANTHERA_FORCE_REBUILD:-0}" != "1" \
   && -x "${BIN_DIR}/ar" \
   && -x "${BIN_DIR}/ranlib" ]]; then
  echo "cctools ar/ranlib already built"
  exit 0
fi

bash "${PANTHERA_ROOT}/tools/fetch_with_checksum.sh" "${URL}" "${DISTFILE}"

if [[ ! -d "${SRCDIR}" ]]; then
  mkdir -p "${SRCDIR}"
  tar -xzf "${DISTFILE}" -C "${SRCDIR}" --strip-components=1
fi

CFLAGS=(
  -target "${TARGET}"
  -mmacosx-version-min="${MINVER}"
  -isysroot "${SDKROOT}"
  -O2
  -I"${OUT_ROOT}/include"
  -I"${SRCDIR}/include"
  -I"${SRCDIR}/include/stuff"
  -I"${SRCDIR}"
  -D__LITTLE_ENDIAN__=1
  -DCPU_TYPE_RISCV32=23
  -U__OPENSTEP__
  -Wno-deprecated-non-prototype
  -Wno-implicit-function-declaration
  -Wno-int-conversion
  -Wno-incompatible-pointer-types
  -Wno-pointer-sign
  -Wno-macro-redefined
  -Wno-enum-compare
)

"${CC_BIN}" "${CFLAGS[@]}" \
  -I"${SRCDIR}/ar" \
  -o "${BIN_DIR}/ar" \
  "${SRCDIR}"/ar/*.c \
  "${SRCDIR}/libstuff/unix_standard_mode.c" \
  "${OUT_ROOT}/panthera_cctools_execute.c" \
  -lSystem

LIBTOOL_SRCS=("${SRCDIR}/misc/libtool.c")
for src in "${SRCDIR}"/libstuff/*.c; do
  case "$(basename "${src}")" in
    llvm.c|lto.c|xcode.c|vm_flush_cache.c)
      ;;
    *)
      LIBTOOL_SRCS+=("${src}")
      ;;
  esac
done

"${CC_BIN}" "${CFLAGS[@]}" \
  -I"${SRCDIR}/misc" \
  -o "${BIN_DIR}/ranlib" \
  "${LIBTOOL_SRCS[@]}" \
  "${OUT_ROOT}/panthera_cctools_stubs.c" \
  -lSystem

bash "${PANTHERA_ROOT}/tools/audit_package.sh" \
  "${BIN_DIR}/ar" \
  "${BIN_DIR}/ranlib"

echo "Built cctools ar: ${BIN_DIR}/ar ($(wc -c < "${BIN_DIR}/ar") bytes)"
echo "Built cctools ranlib: ${BIN_DIR}/ranlib ($(wc -c < "${BIN_DIR}/ranlib") bytes)"
