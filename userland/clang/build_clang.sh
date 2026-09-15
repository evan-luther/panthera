#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT_ROOT="${PANTHERA_ROOT}/userland/clang"
BIN_DIR="${OUT_ROOT}/bin"
LIB_DIR="${OUT_ROOT}/lib"
BUILD_DIR="${PANTHERA_ROOT}/build/panthera-clang-x86_64"
DEFAULT_LLVM_SOURCE="${PANTHERA_ROOT}/src/llvm-project-17.0.6/llvm"
LLVM_SOURCE="${PANTHERA_LLVM_SOURCE:-${DEFAULT_LLVM_SOURCE}}"
if [[ -d "${LLVM_SOURCE}/llvm" && -f "${LLVM_SOURCE}/llvm/CMakeLists.txt" ]]; then
  LLVM_SOURCE="${LLVM_SOURCE}/llvm"
fi
TARGET="${TARGET:-x86_64-apple-darwin23.0}"
MINVER="${MINVER:-14.0}"
CLANG_VERSION="17"
PANTHERA_SDK="${PANTHERA_ROOT}/userland/panthera_sdk/Panthera.sdk"

if [[ ! -d "${LLVM_SOURCE}" ]]; then
  echo "LLVM source not found: ${LLVM_SOURCE}" >&2
  echo "Set PANTHERA_LLVM_SOURCE to an LLVM source tree containing clang." >&2
  exit 1
fi

mkdir -p "${BIN_DIR}" "${LIB_DIR}" "${BUILD_DIR}"

cmake -G Ninja \
  -S "${LLVM_SOURCE}" \
  -B "${BUILD_DIR}" \
  -DLLVM_ENABLE_PROJECTS=clang \
  -DLLVM_TARGETS_TO_BUILD=X86 \
  -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="${MINVER}" \
  -DLLVM_HOST_TRIPLE="${TARGET}" \
  -DLLVM_DEFAULT_TARGET_TRIPLE="${TARGET}" \
  -DCLANG_VENDOR=Panthera \
  -DLLVM_ENABLE_ASSERTIONS=OFF \
  -DLLVM_ENABLE_THREADS=OFF \
  -DLLVM_ENABLE_BACKTRACES=OFF \
  -DLLVM_ENABLE_CRASH_OVERRIDES=OFF \
  -DLLVM_INCLUDE_TESTS=OFF \
  -DLLVM_INCLUDE_EXAMPLES=OFF \
  -DLLVM_INCLUDE_BENCHMARKS=OFF \
  -DLLVM_INCLUDE_DOCS=OFF \
  -DLLVM_ENABLE_TERMINFO=OFF \
  -DLLVM_ENABLE_ZLIB=OFF \
  -DLLVM_ENABLE_ZSTD=OFF \
  -DLLVM_ENABLE_LIBXML2=OFF \
  -DLLVM_ENABLE_CURL=OFF \
  -DLLVM_ENABLE_BINDINGS=OFF \
  -DLLVM_ENABLE_LIBEDIT=OFF \
  -DLLVM_BUILD_LLVM_DYLIB=OFF \
  -DLLVM_LINK_LLVM_DYLIB=OFF \
  -DCLANG_BUILD_TOOLS=ON \
  -DCLANG_INCLUDE_TESTS=OFF \
  -DCLANG_INCLUDE_DOCS=OFF

ninja -C "${BUILD_DIR}" clang libLTO.dylib

cp "${BUILD_DIR}/bin/clang-17" "${BIN_DIR}/clang-17"
ln -sf clang-17 "${BIN_DIR}/clang"

rm -rf "${LIB_DIR}/clang/${CLANG_VERSION}"
mkdir -p "${LIB_DIR}/clang"
cp -R "${BUILD_DIR}/lib/clang/${CLANG_VERSION}" "${LIB_DIR}/clang/${CLANG_VERSION}"
cp "${BUILD_DIR}/lib/libLTO.dylib" "${LIB_DIR}/libLTO.dylib"

"${BUILD_DIR}/bin/clang" \
  -target "${TARGET}" \
  -mmacosx-version-min="${MINVER}" \
  -isysroot "${PANTHERA_SDK}" \
  -resource-dir "${BUILD_DIR}/lib/clang/${CLANG_VERSION}" \
  -Wno-macro-redefined \
  -Wno-nullability-completeness \
  -fuse-ld="${PANTHERA_ROOT}/userland/ld64/bin/ld" \
  "${OUT_ROOT}/panthera_cc.c" \
  -o "${BIN_DIR}/cc"

bash "${PANTHERA_ROOT}/tools/audit_package.sh" \
  "${BIN_DIR}/clang-17" \
  "${BIN_DIR}/cc" \
  "${LIB_DIR}/libLTO.dylib"

"${BIN_DIR}/clang" --version | head -3
echo "Built clang: ${BIN_DIR}/clang-17 ($(wc -c < "${BIN_DIR}/clang-17") bytes)"
echo "Built cc: ${BIN_DIR}/cc ($(wc -c < "${BIN_DIR}/cc") bytes)"
echo "Built libLTO: ${LIB_DIR}/libLTO.dylib ($(wc -c < "${LIB_DIR}/libLTO.dylib") bytes)"
