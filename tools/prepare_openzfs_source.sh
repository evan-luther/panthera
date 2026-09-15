#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TAG="zfs-macOS-2.3.1p1"
TARBALL="${ROOT}/build/distfiles/openzfs-fork-${TAG}.tar.gz"
URL="https://github.com/openzfsonosx/openzfs-fork/archive/refs/tags/${TAG}.tar.gz"
OUT_ROOT="${ROOT}/build/openzfs"
SRC_DIR="${OUT_ROOT}/openzfs-fork-${TAG}"
PATCH_FILE="${ROOT}/kexts/zfs/patches/openzfs-spl-panthera.patch"
PATCH_MARKER="${SRC_DIR}/.panthera-spl-patches-applied"
PATCH_FILES=(
  "${PATCH_FILE}"
  "${ROOT}/kexts/zfs/patches/openzfs-mountroot-requires-zfs-boot.patch"
  "${ROOT}/kexts/zfs/patches/openzfs-register-mountroot-only-for-zfs-boot.patch"
  "${ROOT}/kexts/zfs/patches/openzfs-boot-import-batch.patch"
  "${ROOT}/kexts/zfs/patches/openzfs-async-rmnode.patch"
)
PATCH_MODE="base"
if [[ "${PANTHERA_OPENZFS_COMPOUND_OPEN:-0}" == "1" ]]; then
  PATCH_FILES+=("${ROOT}/kexts/zfs/patches/openzfs-compound-open.patch")
  PATCH_MODE="${PATCH_MODE}+compound-open"
fi
if [[ "${PANTHERA_OPENZFS_VNOPS_PHASE_TRACE:-0}" == "1" ||
    "${PANTHERA_OPENZFS_VNOPS_DETAIL_TRACE:-0}" == "1" ]]; then
  PATCH_FILES+=("${ROOT}/kexts/zfs/patches/openzfs-vnops-phase-trace.patch")
  PATCH_MODE="${PATCH_MODE}+vnops-phase-trace"
fi
if [[ "${PANTHERA_OPENZFS_VNOPS_DETAIL_TRACE:-0}" == "1" ]]; then
  PATCH_FILES+=("${ROOT}/kexts/zfs/patches/openzfs-vnops-detail-trace.patch")
  PATCH_FILES+=("${ROOT}/kexts/zfs/patches/openzfs-vnops-reclaim-detail-trace.patch")
  PATCH_FILES+=("${ROOT}/kexts/zfs/patches/openzfs-zinactive-rmnode-detail-trace.patch")
  PATCH_MODE="${PATCH_MODE}+vnops-detail-trace"
fi

"${ROOT}/tools/fetch_with_checksum.sh" "${URL}" "${TARBALL}" >&2

needs_extract=0
if [[ ! -d "${SRC_DIR}" || ! -f "${PATCH_MARKER}" ]]; then
  needs_extract=1
elif [[ "$(cat "${PATCH_MARKER}")" != "${PATCH_MODE}" ]]; then
  needs_extract=1
else
  for patch_file in "${PATCH_FILES[@]}"; do
    if [[ "${patch_file}" -nt "${PATCH_MARKER}" ]]; then
      needs_extract=1
      break
    fi
  done
fi

if [[ "${needs_extract}" == "1" ]]; then
  rm -rf "${OUT_ROOT}"
  mkdir -p "${OUT_ROOT}"
  tar -xzf "${TARBALL}" -C "${OUT_ROOT}"
fi

if [[ ! -f "${SRC_DIR}/META" || ! -d "${SRC_DIR}/module/os/macos/spl" ]]; then
  echo "OpenZFS source extraction is incomplete: ${SRC_DIR}" >&2
  exit 1
fi

if [[ ! -f "${PATCH_MARKER}" ]]; then
  for patch_file in "${PATCH_FILES[@]}"; do
    if [[ ! -f "${patch_file}" ]]; then
      echo "Missing Panthera OpenZFS patch file: ${patch_file}" >&2
      exit 1
    fi
    (cd "${SRC_DIR}" && patch -p1 < "${patch_file}") >&2
  done
  printf '%s\n' "${PATCH_MODE}" > "${PATCH_MARKER}"
fi

printf '%s\n' "${SRC_DIR}"
