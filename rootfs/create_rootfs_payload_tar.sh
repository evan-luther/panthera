#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
TREE_SCRIPT="${SCRIPT_DIR}/create_rootfs_tree.sh"

OUTPUT_TAR="${PANTHERA_ROOTFS_PAYLOAD_TAR:-${PANTHERA_ROOT}/build/rootfs-payload.tar.gz}"
TREE_DIR="${PANTHERA_ROOTFS_PAYLOAD_TREE:-${PANTHERA_ROOT}/build/rootfs-payload-tree}"
FORCE=0
KEEP_TREE=0
TREE_ARGS=()

usage() {
  cat <<EOF
Usage: $(basename "$0") [options] [-- create_rootfs_tree options]

Creates a filesystem-neutral Panthera rootfs payload archive. The archive is
intended for installer/bootstrap flows that create a target filesystem
separately, then extract this payload into it.

Options:
  --output PATH      Output payload archive (default: ${OUTPUT_TAR})
  --tree DIR         Temporary rootfs tree directory (default: ${TREE_DIR})
  --keep-tree        Keep the assembled rootfs tree after archiving
  --force            Replace existing archive/tree
  --help             Show this help

Options after -- are forwarded to create_rootfs_tree.sh.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --output)
      OUTPUT_TAR="$2"
      shift 2
      ;;
    --tree)
      TREE_DIR="$2"
      shift 2
      ;;
    --keep-tree)
      KEEP_TREE=1
      shift
      ;;
    --force)
      FORCE=1
      shift
      ;;
    --help)
      usage
      exit 0
      ;;
    --)
      shift
      TREE_ARGS+=("$@")
      break
      ;;
    *)
      TREE_ARGS+=("$1")
      shift
      ;;
  esac
done

if ! command -v bsdtar >/dev/null 2>&1; then
  echo "bsdtar is required to create the rootfs payload archive" >&2
  exit 1
fi

if [[ -z "${OUTPUT_TAR}" || -z "${TREE_DIR}" ]]; then
  echo "Output archive and tree directory must not be empty" >&2
  exit 2
fi
if [[ "${OUTPUT_TAR}" != /* ]]; then
  OUTPUT_TAR="${PWD}/${OUTPUT_TAR}"
fi
if [[ "${TREE_DIR}" != /* ]]; then
  TREE_DIR="${PWD}/${TREE_DIR}"
fi

mkdir -p "$(dirname "${OUTPUT_TAR}")"
if [[ -e "${OUTPUT_TAR}" && "${FORCE}" != "1" ]]; then
  echo "Payload archive already exists: ${OUTPUT_TAR}" >&2
  echo "Use --force to replace it." >&2
  exit 1
fi

"${TREE_SCRIPT}" \
  --output "${TREE_DIR}" \
  --force \
  "${TREE_ARGS[@]}"

# Normalize metadata in the temporary payload tree so repeated archive builds
# do not inherit host user IDs, group names, flags, xattrs, or build-time mtimes.
while IFS= read -r -d '' entry; do
  touch -h -t 202001010000.00 "${entry}"
done < <(find "${TREE_DIR}" -depth -print0)

file_list="$(mktemp "${TMPDIR:-/tmp}/panthera-rootfs-payload.XXXXXX")"
cleanup() {
  rm -f "${file_list}"
  if [[ "${KEEP_TREE}" != "1" ]]; then
    rm -rf "${TREE_DIR}"
  fi
}
trap cleanup EXIT

(
  cd "${TREE_DIR}"
  find . -print0 | LC_ALL=C sort -z > "${file_list}"
  bsdtar \
    -czf "${OUTPUT_TAR}" \
    --format pax \
    --no-recursion \
    --uid 0 \
    --gid 0 \
    --uname root \
    --gname wheel \
    --numeric-owner \
    --no-xattrs \
    --no-acls \
    --no-mac-metadata \
    --no-fflags \
    --null \
    -T "${file_list}"
)

echo "Created Panthera rootfs payload archive: ${OUTPUT_TAR}"
