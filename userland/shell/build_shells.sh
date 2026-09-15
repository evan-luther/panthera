#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

SDKROOT="${SDKROOT:-$(xcrun -sdk macosx --show-sdk-path)}"
CC="${CC:-$(xcrun -find clang)}"
TARGET="${TARGET:-x86_64-apple-darwin23.0}"
MINVER="${MINVER:-14.0}"

mkdir -p "${SCRIPT_DIR}"

# 1. Build / stage canonical zsh
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"
NCURSES_LIB_DIR="${SYSROOT}/usr/lib"
ZSH_SRC_DIR="${PANTHERA_ROOT}/src/zsh-108/zsh"
if [[ ! -x "${SCRIPT_DIR}/zsh" || "${PANTHERA_FORCE_REBUILD:-0}" == "1" ]]; then
  if [[ ! -f "${ZSH_SRC_DIR}/configure" ]]; then
    echo "ERROR: Missing upstream zsh configure script at ${ZSH_SRC_DIR}/configure" >&2
    exit 1
  fi
  NCPU="${NCPU:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
  if [[ "${PANTHERA_FORCE_REBUILD:-0}" == "1" && -f "${ZSH_SRC_DIR}/config.status" ]]; then
    make -C "${ZSH_SRC_DIR}" distclean >/dev/null
  fi
  # Apply Panthera patches idempotently to zsh source
  PATCHDIR="${SCRIPT_DIR}/patches"
  if [[ -d "${PATCHDIR}" ]]; then
    shopt -s nullglob
    patchfiles=("${PATCHDIR}"/*.patch)
    shopt -u nullglob
    if [[ ${#patchfiles[@]} -gt 0 ]]; then
      IFS=$'\n' patchfiles=($(LC_ALL=C sort <<<"${patchfiles[*]}"))
      unset IFS
      for patchfile in "${patchfiles[@]}"; do
        [[ -f "${patchfile}" ]] || continue
        patchname="$(basename "${patchfile}")"
        if patch -p0 -d "${ZSH_SRC_DIR}" -N --dry-run < "${patchfile}" >/dev/null 2>&1; then
          echo "Applying ${patchname} to zsh source..."
          if ! patch -p0 -d "${ZSH_SRC_DIR}" -f -s < "${patchfile}"; then
            echo "ERROR: Failed to apply patch ${patchname} to zsh source at ${ZSH_SRC_DIR}" >&2
            exit 1
          fi
        elif patch -p0 -d "${ZSH_SRC_DIR}" -f -R --dry-run < "${patchfile}" >/dev/null 2>&1; then
          echo "Patch ${patchname} already applied to zsh source, skipping."
        else
          echo "ERROR: Patch ${patchname} does not apply cleanly to zsh source at ${ZSH_SRC_DIR} (partial or conflicting source detected)." >&2
          echo "Source tree is in an inconsistent partial patch state." >&2
          echo "To recover, reset zsh source before rebuilding:" >&2
          echo "  rm -rf src/zsh-108 && bash tools/fetch_world_sources.sh" >&2
          exit 1
        fi
      done
    fi
  fi

  ZSH_CFLAGS="-isysroot ${SDKROOT} -DUSE_GETCWD -O2"
  ZSH_CPPFLAGS="-isysroot ${SDKROOT} -DUSE_GETCWD"
  if [[ "${PANTHERA_ZSH_STAGE_TRACE:-0}" == "1" ]]; then
    ZSH_CFLAGS="${ZSH_CFLAGS} -DPANTHERA_ZSH_STAGE_TRACE"
    ZSH_CPPFLAGS="${ZSH_CPPFLAGS} -DPANTHERA_ZSH_STAGE_TRACE"
  fi

  (
    cd "${ZSH_SRC_DIR}"
    ./configure \
      --disable-dynamic \
      --disable-gdbm \
      --disable-pcre \
      --with-curses \
      --with-tcsetpgrp \
      --enable-multibyte \
      --enable-unicode9 \
      --enable-max-function-depth=700 \
      CC="${CC} -target ${TARGET} -mmacosx-version-min=${MINVER}" \
      CPP="${CC} -target ${TARGET} -mmacosx-version-min=${MINVER} -E" \
      CFLAGS="${ZSH_CFLAGS}" \
      CPPFLAGS="${ZSH_CPPFLAGS}" \
      LDFLAGS="-isysroot ${SDKROOT} -L${NCURSES_LIB_DIR} -Wl,-platform_version,macos,${MINVER},${MINVER}"
    make -j"${NCPU}"
  )
  cp "${ZSH_SRC_DIR}/Src/zsh" "${SCRIPT_DIR}/zsh"
fi

# 2. Compile mini_sh freestanding static Mach-O binary
if [[ ! -x "${SCRIPT_DIR}/mini_sh" || "${PANTHERA_FORCE_REBUILD:-0}" == "1" ]]; then
  "${CC}" \
    -target "${TARGET}" \
    -mmacosx-version-min="${MINVER}" \
    -isysroot "${SDKROOT}" \
    -static \
    -nostdlib \
    -ffreestanding \
    -fno-stack-protector \
    -fno-builtin \
    -O2 \
    -Wall \
    -Wextra \
    -Wno-unused-function \
    -Wl,-e,__start \
    -o "${SCRIPT_DIR}/mini_sh" \
    "${SCRIPT_DIR}/mini_sh.c"
fi

# 3. Acceptance: verify both required outputs are present
if [[ ! -f "${SCRIPT_DIR}/zsh" ]]; then
  echo "ERROR: Missing required artifact ${SCRIPT_DIR}/zsh" >&2
  exit 1
fi

if [[ ! -f "${SCRIPT_DIR}/mini_sh" ]]; then
  echo "ERROR: Missing required artifact ${SCRIPT_DIR}/mini_sh" >&2
  exit 1
fi

echo "Built shell artifacts:"
echo "  ${SCRIPT_DIR}/zsh"
echo "  ${SCRIPT_DIR}/mini_sh"
