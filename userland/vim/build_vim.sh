#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

OUT_ROOT="${PANTHERA_ROOT}/userland/vim"
BIN_DIR="${OUT_ROOT}/bin"

SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
CC="$(xcrun -find clang)"
TARGET="x86_64-apple-darwin23.0"
MINVER="14.0"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
CLANG_RT="$("${CC}" -target "${TARGET}" -print-libgcc-file-name)"
if [[ ! -f "${CLANG_RT}" ]]; then
  echo "Compiler runtime archive not found: ${CLANG_RT}" >&2
  exit 1
fi

mkdir -p "${BIN_DIR}"

if [[ "${PANTHERA_FORCE_REBUILD:-0}" != "1" \
   && -x "${BIN_DIR}/vim" ]]; then
  echo "vim already staged at ${BIN_DIR}/vim"
  exit 0
fi

# ---------------------------------------------------------------------------
# Copy source to a temp build directory (vim builds in-tree)
# ---------------------------------------------------------------------------
WORKDIR="$(mktemp -d /tmp/panthera-vim-build.XXXXXX)"
trap 'rm -rf "${WORKDIR}"' EXIT

SRCDIR="${PANTHERA_ROOT}/src/vim/src"
BUILDDIR="${WORKDIR}/src"
cp -a "${SRCDIR}" "${BUILDDIR}"

# ---------------------------------------------------------------------------
# Fetch xdiff/ from upstream vim (not included in Apple's source drop)
# ---------------------------------------------------------------------------
XDIFF_DIR="${BUILDDIR}/xdiff"
if [[ ! -d "${XDIFF_DIR}" ]]; then
  echo ">>> Fetching xdiff/ from upstream vim..."
  XDIFF_TMP="$(mktemp -d /tmp/panthera-vim-xdiff.XXXXXX)"
  (
    cd "${XDIFF_TMP}"
    git clone --depth 1 --filter=blob:none --sparse \
      https://github.com/vim/vim.git .
    git sparse-checkout set src/xdiff
  )
  cp -a "${XDIFF_TMP}/src/xdiff" "${XDIFF_DIR}"
  rm -rf "${XDIFF_TMP}"
fi

# ---------------------------------------------------------------------------
# Patch getchar.c — unguarded terminal_is_active() call (Apple block)
# ---------------------------------------------------------------------------
GETCHAR="${BUILDDIR}/getchar.c"
if grep -q 'if (terminal_is_active())' "${GETCHAR}" 2>/dev/null; then
  echo ">>> Patching getchar.c: disabling unguarded terminal_is_active() call"
  # The unguarded call lives inside #ifdef __APPLE__ (NOT #ifdef FEAT_TERMINAL).
  # Replace only the first occurrence — the second is properly guarded.
  awk '
    /if \(terminal_is_active\(\)\)/ && !done {
      sub(/if \(terminal_is_active\(\)\)/, "if (0)")
      done = 1
    }
    { print }
  ' "${GETCHAR}" > "${GETCHAR}.patched"
  mv "${GETCHAR}.patched" "${GETCHAR}"
fi

# ---------------------------------------------------------------------------
# Configure
# Math is supplied by native libSystem, not the host SDK's separate libm.
# ---------------------------------------------------------------------------
cd "${BUILDDIR}"

./configure \
  --host="${TARGET}" \
  --build="${TARGET}" \
  --prefix=/usr \
  --with-features=normal \
  --disable-gui \
  --without-x \
  --disable-nls \
  --disable-darwin \
  --enable-multibyte \
  --disable-gpm \
  --disable-sysmouse \
  --disable-canberra \
  --with-tlib=ncurses \
  vim_cv_toupper_broken=no \
  vim_cv_terminfo=yes \
  vim_cv_tgetent=zero \
  vim_cv_getcwd_broken=no \
  vim_cv_stat_ignores_slash=no \
  vim_cv_memmove_handles_overlap=yes \
  vim_cv_bcopy_handles_overlap=yes \
  vim_cv_memcpy_handles_overlap=yes \
  ac_cv_sizeof_int=4 \
  ac_cv_sizeof_long=8 \
  ac_cv_sizeof_off_t=8 \
  ac_cv_lib_m_strtod=no \
  CC="${CC} -target ${TARGET} -mmacosx-version-min=${MINVER} -isysroot ${SYSROOT} -isystem ${SDKROOT}/usr/include" \
  CFLAGS="-O2" \
  LDFLAGS="-Wl,-syslibroot,${SYSROOT} -nodefaultlibs -L${SYSROOT}/usr/lib -L${SYSROOT}/usr/lib/system -lncurses.5.4" \
  LIBS="-liconv ${SYSROOT}/usr/lib/libSystem.B.dylib ${CLANG_RT}"

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
# Keep the explicit iconv provider instead of pruning it by symbol presence.
make -j"${JOBS}" LINK_AS_NEEDED=yes vim

# ---------------------------------------------------------------------------
# Stage the binary
# ---------------------------------------------------------------------------
cp -f "${BUILDDIR}/vim" "${BIN_DIR}/vim"
echo "Built vim: ${BIN_DIR}/vim"

# ---------------------------------------------------------------------------
# Audit
# ---------------------------------------------------------------------------
bash "${PANTHERA_ROOT}/tools/audit_package.sh" "${BIN_DIR}/vim"
