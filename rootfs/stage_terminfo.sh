#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ROOT_MOUNT="${1:-/Volumes/PantheraRoot}"
if [[ $# -gt 0 ]]; then
  shift
fi
MANIFESTS=("$@")
TERMINFO_DST="${ROOT_MOUNT}/usr/share/terminfo"
TERMINFO_SRC="${PANTHERA_ROOT}/src/apple-ncurses-71.100.2/ncurses/misc/terminfo.src"
TERMINFO_ENTRIES="${PANTHERA_TERMINFO_ENTRIES:-dumb,screen,vt100}"
HOST_TOOLS_BUILD_SCRIPT="${PANTHERA_ROOT}/tools/build_ncurses_host_tools.sh"
HOST_TIC_BIN="${PANTHERA_ROOT}/build/apple-ncurses-host-tools/out/bin/tic"
TIC_BIN="${TIC_BIN:-${HOST_TIC_BIN}}"

# shellcheck source=/dev/null
source "${PANTHERA_ROOT}/rootfs/scripts/manifest_lib.sh"

if ! manifest_source_selected "src/apple-ncurses-71.100.2/ncurses/misc/terminfo.src" "${MANIFESTS[@]}"; then
  exit 0
fi

if [[ ! -d "${ROOT_MOUNT}" ]]; then
  echo "Root mount not found: ${ROOT_MOUNT}" >&2
  exit 1
fi

if [[ ! -x "${TIC_BIN}" && -x "${HOST_TOOLS_BUILD_SCRIPT}" ]]; then
  bash "${HOST_TOOLS_BUILD_SCRIPT}"
fi

if [[ ! -x "${TIC_BIN}" ]]; then
  echo "tic not found: ${TIC_BIN}" >&2
  exit 1
fi

if [[ ! -f "${TERMINFO_SRC}" ]]; then
  echo "terminfo source not found: ${TERMINFO_SRC}" >&2
  exit 1
fi

rm -rf "${TERMINFO_DST}"
mkdir -p "${TERMINFO_DST}"
"${TIC_BIN}" -x -o "${TERMINFO_DST}" -e "${TERMINFO_ENTRIES}" "${TERMINFO_SRC}"
mkdir -p "${TERMINFO_DST}/x"
ln -sfn ../76/vt100 "${TERMINFO_DST}/x/xterm"
ln -sfn ../76/vt100 "${TERMINFO_DST}/x/xterm-256color"

echo "Staged terminfo into ${ROOT_MOUNT} using ${TIC_BIN}"
