#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="264.3.102"
TAG="ld64-${VERSION}"
DISTFILE="${PANTHERA_ROOT}/build/distfiles/${TAG}.tar.gz"
URL="https://github.com/apple-oss-distributions/ld64/archive/refs/tags/${TAG}.tar.gz"
SRCDIR="${PANTHERA_ROOT}/src/${TAG}"
OUT_ROOT="${PANTHERA_ROOT}/userland/ld64"
BIN_DIR="${OUT_ROOT}/bin"
OBJDIR="${OUT_ROOT}/obj"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"

CC_BIN="${CC:-$(xcrun -find clang)}"
CXX_BIN="${CXX:-$(xcrun -find clang++)}"
SDKROOT="${SDKROOT:-$(xcrun -sdk macosx --show-sdk-path)}"
TARGET="${TARGET:-x86_64-apple-darwin23.0}"
MINVER="${MINVER:-14.0}"

mkdir -p "${BIN_DIR}" "${OBJDIR}"

if [[ "${PANTHERA_FORCE_REBUILD:-0}" != "1" && -x "${BIN_DIR}/ld" ]]; then
  echo "ld64 already built"
  exit 0
fi

bash "${PANTHERA_ROOT}/tools/fetch_with_checksum.sh" "${URL}" "${DISTFILE}"

if [[ ! -d "${SRCDIR}" ]]; then
  mkdir -p "${SRCDIR}"
  tar -xzf "${DISTFILE}" -C "${SRCDIR}" --strip-components=1
fi

patch_sources() {
  local blob_h="${SRCDIR}/src/ld/code-sign-blobs/blob.h"
  local address_h="${SRCDIR}/src/ld/parsers/libunwind/AddressSpace.hpp"
  local macho_reloc="${SRCDIR}/src/ld/parsers/macho_relocatable_file.cpp"
  local options_cpp="${SRCDIR}/src/ld/Options.cpp"

  if ! grep -q "PANTHERA_LD64_BLOBCORE_CLONE" "${blob_h}"; then
    perl -0pi -e 's/#include <cstdio>/#include <cstdio>\n#include <cstdlib>\n#include <cstring>/g' "${blob_h}"
    perl -0pi -e 's/(const void \*data\(\) const\s*\{ return this; \}\n\s*void length\(size_t size\)\s*\{ mLength = size; \})/$1\n\t\/\/ PANTHERA_LD64_BLOBCORE_CLONE: newer clang requires the base clone helper used below.\n\tBlobCore *clone() const { BlobCore *copy = (BlobCore *)::malloc(this->length()); if (copy != NULL) ::memcpy(copy, this, this->length()); return copy; }/' "${blob_h}"
  fi

  if ! grep -q "PANTHERA_LD64_DYLD_UNWIND_SECTIONS" "${address_h}"; then
    perl -0pi -e 's/#include "dwarf2.h"/#include "dwarf2.h"\n\n#ifndef PANTHERA_LD64_DYLD_UNWIND_SECTIONS\n#define PANTHERA_LD64_DYLD_UNWIND_SECTIONS\nstruct dyld_unwind_sections { const struct mach_header* mh; const void* dwarf_section; uintptr_t dwarf_section_length; const void* compact_unwind_section; uintptr_t compact_unwind_section_length; };\nstatic inline bool _dyld_find_unwind_sections(void*, dyld_unwind_sections*) { return false; }\n#endif/' "${address_h}"
  fi

  perl -0pi \
    -e 's/libunwind::CFI_Atom_Info<CFISection<x86_64>::OAS>::CFI_Atom_Info/libunwind::CFI_Atom_Info<CFISection<x86_64>::OAS>/g;' \
    -e 's/libunwind::CFI_Atom_Info<CFISection<x86>::OAS>::CFI_Atom_Info/libunwind::CFI_Atom_Info<CFISection<x86>::OAS>/g;' \
    -e 's/libunwind::CFI_Atom_Info<CFISection<arm>::OAS>::CFI_Atom_Info/libunwind::CFI_Atom_Info<CFISection<arm>::OAS>/g;' \
    -e 's/libunwind::CFI_Atom_Info<CFISection<arm64>::OAS>::CFI_Atom_Info/libunwind::CFI_Atom_Info<CFISection<arm64>::OAS>/g;' \
    "${macho_reloc}"

  if ! grep -q "PANTHERA_LD64_PLATFORM_VERSION" "${options_cpp}"; then
    PANTHERA_LD64_OPTIONS_CPP="${options_cpp}" python3 <<'PY'
from pathlib import Path
import os

path = Path(os.environ["PANTHERA_LD64_OPTIONS_CPP"])
text = path.read_text()
needle = '''\t\t\telse if ( strcmp(arg, "-twolevel_namespace_hints") == 0 ) {
\t\t\t\t// FIX FIX
\t\t\t}
\t\t\t// Use this flag to set default behavior for deployement targets.
'''
insert = '''\t\t\telse if ( strcmp(arg, "-twolevel_namespace_hints") == 0 ) {
\t\t\t\t// FIX FIX
\t\t\t}
\t\t\telse if ( strcmp(arg, "-platform_version") == 0 ) {
\t\t\t\t// PANTHERA_LD64_PLATFORM_VERSION: accept the modern Darwin driver spelling.
\t\t\t\tconst char* platform = argv[++i];
\t\t\t\tconst char* minVers = argv[++i];
\t\t\t\tconst char* sdkVers = argv[++i];
\t\t\t\tif ( (platform == NULL) || (minVers == NULL) || (sdkVers == NULL) )
\t\t\t\t\tthrow "-platform_version missing <platform> <min_version> <sdk_version>";
\t\t\t\tif ( strcmp(platform, "macos") == 0 ) {
\t\t\t\t\tsetMacOSXVersionMin(minVers);
\t\t\t\t}
\t\t\t\telse if ( strcmp(platform, "ios") == 0 ) {
\t\t\t\t\tsetIOSVersionMin(minVers);
\t\t\t\t}
\t\t\t\telse if ( strcmp(platform, "ios-simulator") == 0 ) {
\t\t\t\t\tsetIOSVersionMin(minVers);
\t\t\t\t\tfTargetIOSSimulator = true;
\t\t\t\t}
\t\t\t\telse if ( strcmp(platform, "watchos") == 0 ) {
\t\t\t\t\tsetWatchOSVersionMin(minVers);
\t\t\t\t}
\t\t\t\telse if ( strcmp(platform, "watchos-simulator") == 0 ) {
\t\t\t\t\tsetWatchOSVersionMin(minVers);
\t\t\t\t\tfTargetIOSSimulator = true;
\t\t\t\t}
\t#if SUPPORT_APPLE_TV
\t\t\t\telse if ( strcmp(platform, "tvos") == 0 ) {
\t\t\t\t\tsetIOSVersionMin(minVers);
\t\t\t\t\tfPlatform = kPlatform_tvOS;
\t\t\t\t}
\t\t\t\telse if ( strcmp(platform, "tvos-simulator") == 0 ) {
\t\t\t\t\tsetIOSVersionMin(minVers);
\t\t\t\t\tfPlatform = kPlatform_tvOS;
\t\t\t\t\tfTargetIOSSimulator = true;
\t\t\t\t}
\t#endif
\t\t\t\telse {
\t\t\t\t\tthrowf("unsupported -platform_version platform: %s", platform);
\t\t\t\t}
\t\t\t\tfSDKVersion = parseVersionNumber32(sdkVers);
\t\t\t}
\t\t\t// Use this flag to set default behavior for deployement targets.
'''
if needle not in text:
    raise SystemExit("ld64 Options.cpp insertion point not found")
path.write_text(text.replace(needle, insert))
PY
  fi
}

patch_sources

rm -rf "${OBJDIR}"
mkdir -p "${OBJDIR}"

cat > "${OBJDIR}/configure.h" <<EOF
#define DEFAULT_MACOSX_MIN_VERSION "${MINVER}"
#define SUPPORT_ARCH_i386 0
#define SUPPORT_ARCH_x86_64 1
#define SUPPORT_ARCH_x86_64h 0
#define SUPPORT_ARCH_armv6 0
#define SUPPORT_ARCH_armv7 0
#define SUPPORT_ARCH_armv7s 0
#define SUPPORT_ARCH_armv7m 0
#define SUPPORT_ARCH_armv7em 0
#define SUPPORT_ARCH_armv7k 0
#define SUPPORT_ARCH_arm64 0
#define SUPPORT_ARCH_arm64e 0
#define SUPPORT_ARCH_arm64_32 0
#define SUPPORT_ARCH_riscv32 0
#define SUPPORT_APPLE_TV 0
#define ALL_SUPPORTED_ARCHS "x86_64"
#define BITCODE_XAR_VERSION "1.0"
#define LD64_VERSION_NUM 264.3
#define LD_PAGE_SIZE 0x1000
EOF

cat > "${OBJDIR}/CrashReporterClient.h" <<'EOF'
#ifndef PANTHERA_LD64_CRASHREPORTERCLIENT_H
#define PANTHERA_LD64_CRASHREPORTERCLIENT_H
#include <stdint.h>
#define CRASHREPORTER_ANNOTATIONS_SECTION "__crash_info"
#define CRASHREPORTER_ANNOTATIONS_VERSION 5
#define CRSetCrashLogMessage(msg) ((void)(msg))
struct crashreporter_annotations_t {
  uint64_t version;
  uint64_t message;
  uint64_t signature_string;
  uint64_t backtrace;
  uint64_t message2;
  uint64_t thread;
  uint64_t dialog_mode;
};
#endif
EOF

{
  printf 'static const char *compile_stubs = '
  awk '{ gsub(/\\/, "\\\\"); gsub(/"/, "\\\""); printf "\"%s\\n\"\n", $0 }' "${SRCDIR}/compile_stubs"
  printf ';\n'
} > "${OBJDIR}/compile_stubs.h"

CXXFLAGS=(
  -target "${TARGET}"
  -mmacosx-version-min="${MINVER}"
  -isysroot "${SDKROOT}"
  -O2
  -fno-stack-protector
  -std=c++11
  -stdlib=libc++
  -nostdinc++
  -I"${OBJDIR}"
  -I"${SRCDIR}/src/ld"
  -I"${SRCDIR}/src/ld/parsers"
  -I"${SRCDIR}/src/ld/passes"
  -I"${SRCDIR}/src/abstraction"
  -I"${PANTHERA_ROOT}/userland/libcxx/shims"
  -I"${PANTHERA_ROOT}/userland/libcxxabi/shims"
  -I"${PANTHERA_ROOT}/src/llvm-libcxx-19/libcxx/include"
  -I"${PANTHERA_ROOT}/src/libcppabi-26/include"
  -I"${PANTHERA_ROOT}/src/cctools-1030.6.3/include"
  -Wno-c++11-narrowing
  -Wno-deprecated-builtins
  -Wno-deprecated-declarations
  -Wno-deprecated-writable-strings
  -Wno-invalid-source-encoding
  -Wno-macro-redefined
  -Wno-switch
)

CFLAGS=(
  -target "${TARGET}"
  -mmacosx-version-min="${MINVER}"
  -isysroot "${SDKROOT}"
  -O2
  -fno-stack-protector
  -I"${OBJDIR}"
  -I"${SRCDIR}/src/ld"
  -I"${SRCDIR}/src/abstraction"
  -I"${PANTHERA_ROOT}/src/cctools-1030.6.3/include"
)

while IFS= read -r src; do
  rel="${src#"${SRCDIR}/src/"}"
  case "${rel}" in
    ld/parsers/lto_file.cpp|ld/passes/bitcode_bundle.cpp)
      continue
      ;;
  esac
  obj="${OBJDIR}/${rel//\//_}.o"
  echo "CXX ${rel}"
  "${CXX_BIN}" "${CXXFLAGS[@]}" -c "${src}" -o "${obj}"
done < <(find "${SRCDIR}/src/ld" -name '*.cpp' -type f | sort -u)

echo "CXX panthera_ld64_no_lto.cpp"
"${CXX_BIN}" "${CXXFLAGS[@]}" -c "${OUT_ROOT}/panthera_ld64_no_lto.cpp" -o "${OBJDIR}/panthera_ld64_no_lto.o"

echo "CC panthera_ld64_support.c"
"${CC_BIN}" "${CFLAGS[@]}" -c "${OUT_ROOT}/panthera_ld64_support.c" -o "${OBJDIR}/panthera_ld64_support.o"

while IFS= read -r src; do
  rel="${src#"${SRCDIR}/src/"}"
  obj="${OBJDIR}/${rel//\//_}.o"
  echo "CC ${rel}"
  "${CC_BIN}" "${CFLAGS[@]}" -c "${src}" -o "${obj}"
done < <(find "${SRCDIR}/src/ld" -name '*.c' -type f | sort -u)

"${CXX_BIN}" \
  -target "${TARGET}" \
  -mmacosx-version-min="${MINVER}" \
  -isysroot "${SDKROOT}" \
  -nodefaultlibs \
  -L"${SYSROOT}/usr/lib" \
  -L"${SYSROOT}/usr/lib/system" \
  -lc++ \
  "${SYSROOT}/usr/lib/libc++abi.dylib" \
  -lSystem \
  $(find "${OBJDIR}" -name '*.o' -type f | sort) \
  -o "${BIN_DIR}/ld"

bash "${PANTHERA_ROOT}/tools/audit_package.sh" "${BIN_DIR}/ld"

"${BIN_DIR}/ld" -v 2>&1 | head -3
echo "Built ld64: ${BIN_DIR}/ld ($(wc -c < "${BIN_DIR}/ld") bytes)"
