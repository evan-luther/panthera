#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRCDIR="${PANTHERA_ROOT}/src/libedit-65/src"
CONFDIR="${PANTHERA_ROOT}/src/libedit-65"
OUT_LIB="${PANTHERA_ROOT}/userland/libedit/lib"
OUT_INC="${PANTHERA_ROOT}/userland/libedit/include"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"

CC="$(xcrun -find clang)"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
TARGET="x86_64-apple-darwin23.0"
MINVER="14.0"

# Locate Darwin compiler runtime (libclang_rt.osx.a)
CLANG_RT="$("${CC}" -target "${TARGET}" -print-libgcc-file-name 2>/dev/null || true)"
if [ -z "${CLANG_RT}" ] || [ ! -f "${CLANG_RT}" ]; then
  CLANG_RT="$("${CC}" -print-file-name=libclang_rt.osx.a 2>/dev/null || true)"
fi
if [ -z "${CLANG_RT}" ] || [ ! -f "${CLANG_RT}" ]; then
  CLANG_RESOURCE_DIR="$("${CC}" -print-resource-dir 2>/dev/null || true)"
  if [ -n "${CLANG_RESOURCE_DIR}" ] && [ -f "${CLANG_RESOURCE_DIR}/lib/darwin/libclang_rt.osx.a" ]; then
    CLANG_RT="${CLANG_RESOURCE_DIR}/lib/darwin/libclang_rt.osx.a"
  fi
fi

if [ -z "${CLANG_RT}" ] || [ ! -f "${CLANG_RT}" ]; then
  echo "ERROR: Unable to locate Darwin compiler runtime archive (libclang_rt.osx.a) using ${CC}" >&2
  exit 1
fi

mkdir -p "${OUT_LIB}" "${OUT_INC}" "${OUT_INC}/editline"

if [[ "${PANTHERA_FORCE_REBUILD:-0}" != "1" && -f "${OUT_LIB}/libedit.3.dylib" ]]; then
    echo "libedit already built"
    exit 0
fi

WORKDIR="$(mktemp -d /tmp/panthera-libedit-build.XXXXXX)"
trap 'rm -rf "${WORKDIR}"' EXIT

CFLAGS="-target ${TARGET} -mmacosx-version-min=${MINVER} -isysroot ${SDKROOT} -O2"
CFLAGS="${CFLAGS} -I${CONFDIR} -I${CONFDIR}/local -I${SRCDIR} -I${SRCDIR}/editline -DHAVE_CONFIG_H"
CFLAGS="${CFLAGS} -Wno-shorten-64-to-32 -Wno-sign-conversion -Wno-format"
CFLAGS="${CFLAGS} -fPIC"

# All library source files (exclude wcsdup.c — it's a compat shim we don't need)
LIB_SRCS=(
    chared.c chartype.c common.c el.c eln.c emacs.c filecomplete.c
    hist.c history.c keymacro.c map.c parse.c prompt.c read.c
    readline.c refresh.c search.c sig.c terminal.c tokenizer.c
    tty.c unvis.c vi.c vis.c
)

# Generated source files from Apple's pre-build
GENERATED_SRCS=( fcns.c help.c historyn.c tokenizern.c )

echo "Compiling libedit sources..."
OBJS=()
for src in "${LIB_SRCS[@]}"; do
    obj="${WORKDIR}/${src%.c}.o"
    "${CC}" ${CFLAGS} -c "${SRCDIR}/${src}" -o "${obj}" &
    OBJS+=("${obj}")
done
for src in "${GENERATED_SRCS[@]}"; do
    obj="${WORKDIR}/gen_${src%.c}.o"
    "${CC}" ${CFLAGS} -c "${CONFDIR}/local/${src}" -o "${obj}" &
    OBJS+=("${obj}")
done
wait

echo "Linking libedit.3.dylib..."
"${CC}" -target "${TARGET}" -mmacosx-version-min="${MINVER}" -isysroot "${SDKROOT}" \
    -Wl,-syslibroot,"${SYSROOT}" \
    -dynamiclib \
    -nodefaultlibs \
    -install_name /usr/lib/libedit.3.dylib \
    -compatibility_version 2.0.0 -current_version 3.0.0 \
    -Wl,-not_for_dyld_shared_cache \
    -o "${WORKDIR}/libedit.3.dylib" \
    "${OBJS[@]}" \
    "${SYSROOT}/usr/lib/libncurses.5.4.dylib" \
    "${SYSROOT}/usr/lib/libSystem.B.dylib" \
    "${CLANG_RT}"
# Stage outputs
cp "${WORKDIR}/libedit.3.dylib" "${OUT_LIB}/libedit.3.dylib"
ln -sf libedit.3.dylib "${OUT_LIB}/libedit.dylib"

# Copy headers
cp "${SRCDIR}/histedit.h" "${OUT_INC}/"
cp "${SRCDIR}/editline/"*.h "${OUT_INC}/editline/" 2>/dev/null || true
# readline compatibility header
if [[ -f "${SRCDIR}/editline/readline.h" ]]; then
    cp "${SRCDIR}/editline/readline.h" "${OUT_INC}/editline/"
fi

# Stage to sysroot
cp "${OUT_LIB}/libedit.3.dylib" "${SYSROOT}/usr/lib/libedit.3.dylib"
ln -sf libedit.3.dylib "${SYSROOT}/usr/lib/libedit.dylib"
cp "${OUT_INC}/histedit.h" "${SYSROOT}/usr/include/"
mkdir -p "${SYSROOT}/usr/include/editline"
cp "${OUT_INC}/editline/"*.h "${SYSROOT}/usr/include/editline/" 2>/dev/null || true

# Audit
bash "${PANTHERA_ROOT}/tools/audit_package.sh" "${OUT_LIB}/libedit.3.dylib"

echo "Built libedit: ${OUT_LIB}/libedit.3.dylib ($(wc -c < "${OUT_LIB}/libedit.3.dylib") bytes)"
