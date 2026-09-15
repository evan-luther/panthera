#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
LLVM_VERSION="18.1.8"
DIST_DIR="${PANTHERA_ROOT}/build/distfiles"
DEFAULT_SRC_ROOT="${PANTHERA_ROOT}/build/llvm-project-${LLVM_VERSION}"
BUILD_DIR="${PANTHERA_ROOT}/build/panthera-libunwind"
OUT_ROOT="${PANTHERA_ROOT}/userland/libunwind"
LIB_DIR="${OUT_ROOT}/lib"
OBJ_DIR="${OUT_ROOT}/obj"
TARGET="${TARGET:-x86_64-apple-darwin23.0}"
MINVER="${MINVER:-14.0}"
SDKROOT="${SDKROOT:-$(xcrun -sdk macosx --show-sdk-path)}"
export CMAKE_APPLE_SILICON_PROCESSOR="x86_64"

LIBUNWIND_TARBALL="${DIST_DIR}/libunwind-${LLVM_VERSION}.src.tar.xz"
CMAKE_TARBALL="${DIST_DIR}/cmake-${LLVM_VERSION}.src.tar.xz"
RUNTIMES_TARBALL="${DIST_DIR}/runtimes-${LLVM_VERSION}.src.tar.xz"

LLVM_BASE_URL="https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}"
LIBUNWIND_URL="${LLVM_BASE_URL}/libunwind-${LLVM_VERSION}.src.tar.xz"
CMAKE_URL="${LLVM_BASE_URL}/cmake-${LLVM_VERSION}.src.tar.xz"
RUNTIMES_URL="${LLVM_BASE_URL}/runtimes-${LLVM_VERSION}.src.tar.xz"

if [[ -n "${PANTHERA_LLVM_ROOT:-}" ]]; then
  SRC="${PANTHERA_LLVM_ROOT}/libunwind"
  if [[ ! -d "${SRC}" ]]; then
    echo "libunwind source not found in override path: ${SRC}" >&2
    echo "PANTHERA_LLVM_ROOT must contain a libunwind directory." >&2
    exit 1
  fi
else
  "${PANTHERA_ROOT}/tools/fetch_with_checksum.sh" "${LIBUNWIND_URL}" "${LIBUNWIND_TARBALL}"
  "${PANTHERA_ROOT}/tools/fetch_with_checksum.sh" "${CMAKE_URL}" "${CMAKE_TARBALL}"
  "${PANTHERA_ROOT}/tools/fetch_with_checksum.sh" "${RUNTIMES_URL}" "${RUNTIMES_TARBALL}"

  SRC="${DEFAULT_SRC_ROOT}/libunwind"
  MARKER="${DEFAULT_SRC_ROOT}/.panthera-extracted-version"

  needs_extract=0
  if [[ ! -d "${DEFAULT_SRC_ROOT}/libunwind" || ! -d "${DEFAULT_SRC_ROOT}/cmake" || ! -d "${DEFAULT_SRC_ROOT}/runtimes" || ! -f "${MARKER}" ]]; then
    needs_extract=1
  elif [[ "$(cat "${MARKER}" 2>/dev/null || true)" != "${LLVM_VERSION}" ]]; then
    needs_extract=1
  else
    for archive in "${LIBUNWIND_TARBALL}" "${CMAKE_TARBALL}" "${RUNTIMES_TARBALL}"; do
      if [[ "${archive}" -nt "${MARKER}" ]]; then
        needs_extract=1
        break
      fi
    done
  fi

  if [[ "${needs_extract}" == "1" ]]; then
    rm -rf "${DEFAULT_SRC_ROOT}"
    mkdir -p "${DEFAULT_SRC_ROOT}/libunwind" "${DEFAULT_SRC_ROOT}/cmake" "${DEFAULT_SRC_ROOT}/runtimes"
    tar -xf "${LIBUNWIND_TARBALL}" -C "${DEFAULT_SRC_ROOT}/libunwind" --strip-components=1
    tar -xf "${CMAKE_TARBALL}" -C "${DEFAULT_SRC_ROOT}/cmake" --strip-components=1
    tar -xf "${RUNTIMES_TARBALL}" -C "${DEFAULT_SRC_ROOT}/runtimes" --strip-components=1
    printf '%s\n' "${LLVM_VERSION}" > "${MARKER}"
  fi
fi
if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
  cached_src="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "${BUILD_DIR}/CMakeCache.txt" 2>/dev/null || true)"
  cached_arch="$(sed -n 's/^CMAKE_OSX_ARCHITECTURES:STRING=//p' "${BUILD_DIR}/CMakeCache.txt" 2>/dev/null || true)"
  cached_proc="$(sed -n 's/^CMAKE_APPLE_SILICON_PROCESSOR:STRING=//p' "${BUILD_DIR}/CMakeCache.txt" 2>/dev/null || true)"
  current_src="$(cd "${SRC}" 2>/dev/null && pwd -P || echo "${SRC}")"
  cached_src_resolved="$(cd "${cached_src}" 2>/dev/null && pwd -P || echo "${cached_src}")"
  if [[ -z "${cached_src}" || ( "${cached_src}" != "${SRC}" && "${cached_src_resolved}" != "${current_src}" ) || "${cached_arch}" != "x86_64" || "${cached_proc}" != "x86_64" ]]; then
    rm -rf "${BUILD_DIR}"
  fi
fi

mkdir -p "${BUILD_DIR}" "${LIB_DIR}" "${OBJ_DIR}"

cmake -G Ninja \
  -S "${SRC}" \
  -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DCMAKE_APPLE_SILICON_PROCESSOR=x86_64 \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 \
  -DCMAKE_SYSTEM_PROCESSOR=x86_64 \
  -DCMAKE_C_COMPILER_TARGET="${TARGET}" \
  -DCMAKE_CXX_COMPILER_TARGET="${TARGET}" \
  -DCMAKE_ASM_COMPILER_TARGET="${TARGET}" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="${MINVER}" \
  -DCMAKE_OSX_SYSROOT="${SDKROOT}" \
  -DCMAKE_C_COMPILER="$(xcrun -find clang)" \
  -DCMAKE_CXX_COMPILER="$(xcrun -find clang++)" \
  -DLIBUNWIND_ENABLE_SHARED=OFF \
  -DLIBUNWIND_ENABLE_STATIC=ON \
  -DLIBUNWIND_ENABLE_THREADS=OFF \
  -DLIBUNWIND_ENABLE_ASSERTIONS=OFF \
  -DLIBUNWIND_ENABLE_PEDANTIC=OFF \
  -DLIBUNWIND_INCLUDE_TESTS=OFF \
  -DLIBUNWIND_INCLUDE_DOCS=OFF \
  -DLIBUNWIND_INSTALL_HEADERS=OFF

ninja -C "${BUILD_DIR}" unwind_static

cp "${BUILD_DIR}/lib/libunwind.a" "${LIB_DIR}/libunwind.a"

COMMON_ASFLAGS=(
  -target "${TARGET}"
  -mmacosx-version-min="${MINVER}"
  -isysroot "${SDKROOT}"
  -I"${SRC}/include"
  -I"${SRC}/src"
  -fPIC
)

xcrun -sdk macosx clang "${COMMON_ASFLAGS[@]}" \
  -c "${SRC}/src/UnwindRegistersSave.S" \
  -o "${OBJ_DIR}/UnwindRegistersSave.o"
xcrun -sdk macosx clang "${COMMON_ASFLAGS[@]}" \
  -c "${SRC}/src/UnwindRegistersRestore.S" \
  -o "${OBJ_DIR}/UnwindRegistersRestore.o"

echo "Verifying architecture for ${LIB_DIR}/libunwind.a..."
archive_archs="$(otool -hv "${LIB_DIR}/libunwind.a" 2>/dev/null | awk '/MH_MAGIC/ { print $2 }' | sort -u || true)"
if [[ -z "${archive_archs}" ]]; then
  echo "ERROR: ${LIB_DIR}/libunwind.a contains no Mach-O object members." >&2
  exit 1
fi
if [[ "${archive_archs}" != "X86_64" ]]; then
  echo "ERROR: ${LIB_DIR}/libunwind.a contains non-x86_64 member(s): ${archive_archs}" >&2
  otool -hv "${LIB_DIR}/libunwind.a" >&2
  exit 1
fi

for obj in "${OBJ_DIR}/UnwindRegistersSave.o" "${OBJ_DIR}/UnwindRegistersRestore.o"; do
  obj_arch="$(otool -hv "${obj}" 2>/dev/null | awk '/MH_MAGIC/ { print $2 }' | sort -u || true)"
  if [[ "${obj_arch}" != "X86_64" ]]; then
    echo "ERROR: ${obj} is not x86_64 (got: ${obj_arch:-unknown})" >&2
    exit 1
  fi
done

echo "Built and verified x86_64 libunwind archive: ${LIB_DIR}/libunwind.a"
echo "Built and verified x86_64 libunwind asm: ${OBJ_DIR}/UnwindRegistersSave.o ${OBJ_DIR}/UnwindRegistersRestore.o"
