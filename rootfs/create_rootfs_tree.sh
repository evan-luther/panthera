#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

ROOTFS_ETC_DIR="${SCRIPT_DIR}/etc"
ROOTFS_LAUNCH_DAEMONS_DIR="${SCRIPT_DIR}/System/Library/LaunchDaemons"
ROOTFS_BUILD_COMPONENTS_SCRIPT="${SCRIPT_DIR}/scripts/build_components.sh"
ROOTFS_VERIFY_INPUTS_SCRIPT="${SCRIPT_DIR}/scripts/verify_rootfs_inputs.sh"
ROOTFS_POPULATE_SKELETON_SCRIPT="${SCRIPT_DIR}/scripts/populate_skeleton.sh"
ROOTFS_STAGE_ETC_SCRIPT="${SCRIPT_DIR}/scripts/stage_etc.sh"
ROOTFS_STAGE_USERS_SCRIPT="${SCRIPT_DIR}/scripts/stage_users.sh"
ROOTFS_STAGE_LAUNCHDAEMONS_SCRIPT="${SCRIPT_DIR}/scripts/stage_launchdaemons.sh"
ROOTFS_STAGE_SHARED_CACHE_SCRIPT="${SCRIPT_DIR}/scripts/stage_shared_cache.sh"
ROOTFS_STAGE_USER_TEMPLATES_SCRIPT="${SCRIPT_DIR}/scripts/stage_user_templates.sh"
ROOTFS_STAGE_BASE_PAYLOAD_SCRIPT="${SCRIPT_DIR}/scripts/stage_base_payload.sh"
ROOTFS_STAGE_PACKAGE_PAYLOAD_SCRIPT="${SCRIPT_DIR}/scripts/stage_package_payload.sh"
ROOTFS_VERIFY_ROOTFS_SCRIPT="${SCRIPT_DIR}/scripts/verify_rootfs.sh"
TERMINFO_STAGE_SCRIPT="${SCRIPT_DIR}/stage_terminfo.sh"

SHARED_CACHE_BIN="${PANTHERA_ROOT}/images/shared_cache/dyld_shared_cache_x86_64"
ROOT_PASSWORD_HASH=""
PANTHERA_PASSWORD_HASH="ptYsYjAMS5dxQ"
ROOT_LOGIN_SHELL="${PANTHERA_ROOT_SHELL:-/bin/zsh}"
PANTHERA_LOGIN_SHELL="${PANTHERA_USER_SHELL:-/bin/zsh}"

OUTPUT_DIR="${PANTHERA_ROOTFS_TREE:-${PANTHERA_ROOT}/build/rootfs-tree}"
BUILD_COMPONENTS="${PANTHERA_ROOTFS_BUILD_COMPONENTS:-0}"
FORCE=0
POPULATE_EXISTING=0
ROOTFS_MANIFESTS=()
EXPLICIT_ROOTFS_MANIFESTS=()

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Assembles a Panthera root filesystem payload into a normal host directory.
This is filesystem-neutral: it does not create or mount an HFS image.

Options:
  --output DIR       Output rootfs directory (default: ${OUTPUT_DIR})
  --manifest PATH    Rootfs input manifest to verify/stage
                    (may be repeated; defaults to Panthera base manifests)
  PANTHERA_DEFAULT_NETWORK_OWNER=netbringup|ipconfiguration
                    Select default network owner (default: ipconfiguration)
  --build-components
                    Build component artifacts before assembly
  --no-build-components
                    Do not build component artifacts before assembly (default)
  --force           Replace existing output directory
  --populate-existing
                    Populate an existing directory without removing it
  --help            Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --output)
      OUTPUT_DIR="$2"
      shift 2
      ;;
    --manifest)
      EXPLICIT_ROOTFS_MANIFESTS+=("$2")
      shift 2
      ;;
    --build-components)
      BUILD_COMPONENTS=1
      shift
      ;;
    --no-build-components)
      BUILD_COMPONENTS=0
      shift
      ;;
    --force)
      FORCE=1
      shift
      ;;
    --populate-existing)
      POPULATE_EXISTING=1
      shift
      ;;
    --help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ -z "${OUTPUT_DIR}" ]]; then
  echo "Output directory must not be empty" >&2
  exit 2
fi
if [[ "${OUTPUT_DIR}" != /* ]]; then
  OUTPUT_DIR="${PWD}/${OUTPUT_DIR}"
fi

output_parent="$(dirname "${OUTPUT_DIR}")"
output_base="$(basename "${OUTPUT_DIR}")"
mkdir -p "${output_parent}"
output_parent="$(cd "${output_parent}" && pwd -P)"
OUTPUT_DIR="${output_parent}/${output_base}"

case "${OUTPUT_DIR}" in
  /|/Users|/Users/admin|"${PANTHERA_ROOT}"|"${PANTHERA_ROOT}/rootfs")
    echo "Refusing dangerous rootfs tree output path: ${OUTPUT_DIR}" >&2
    exit 2
    ;;
esac

append_csv_unique() {
  local var_name="$1"
  local value="$2"
  local current="${!var_name:-}"
  local entry

  IFS=',' read -r -a entries <<< "${current}"
  for entry in "${entries[@]}"; do
    entry="${entry## }"
    entry="${entry%% }"
    if [[ "${entry}" == "${value}" ]]; then
      printf -v "${var_name}" '%s' "${current}"
      return
    fi
  done
  if [[ -n "${current}" ]]; then
    printf -v "${var_name}" '%s,%s' "${current}" "${value}"
  else
    printf -v "${var_name}" '%s' "${value}"
  fi
}

case "${PANTHERA_DEFAULT_NETWORK_OWNER:-ipconfiguration}" in
  netbringup)
    ;;
  ipconfiguration)
    PANTHERA_STAGE_CONFIGD=1
    PANTHERA_STAGE_IPCONFIGURATION=1
    PANTHERA_CONFIGD_IPCONFIGURATION=1
    append_csv_unique PANTHERA_SKIP_LAUNCHDAEMONS "com.panthera.netbringup.plist"
    append_csv_unique PANTHERA_SKIP_LAUNCHDAEMONS "com.apple.IPConfiguration.plist"
    export PANTHERA_STAGE_CONFIGD
    export PANTHERA_STAGE_IPCONFIGURATION
    export PANTHERA_CONFIGD_IPCONFIGURATION
    export PANTHERA_SKIP_LAUNCHDAEMONS
    ;;
  *)
    echo "Unsupported PANTHERA_DEFAULT_NETWORK_OWNER=${PANTHERA_DEFAULT_NETWORK_OWNER}" >&2
    echo "Supported values: netbringup, ipconfiguration" >&2
    exit 2
    ;;
esac

add_manifest() {
  local manifest="$1"

  if [[ "${manifest}" = /* ]]; then
    ROOTFS_MANIFESTS+=("${manifest}")
  else
    ROOTFS_MANIFESTS+=("${PANTHERA_ROOT}/${manifest}")
  fi
}

if [[ "${#EXPLICIT_ROOTFS_MANIFESTS[@]}" -gt 0 ]]; then
  for manifest_entry in "${EXPLICIT_ROOTFS_MANIFESTS[@]}"; do
    add_manifest "${manifest_entry}"
  done
elif [[ -n "${PANTHERA_ROOTFS_MANIFESTS:-}" ]]; then
  IFS=',' read -r -a manifest_entries <<< "${PANTHERA_ROOTFS_MANIFESTS}"
  for manifest_entry in "${manifest_entries[@]}"; do
    manifest_entry="${manifest_entry## }"
    manifest_entry="${manifest_entry%% }"
    [[ -z "${manifest_entry}" ]] && continue
    add_manifest "${manifest_entry}"
  done
else
  add_manifest "manifests/base.system"
  add_manifest "manifests/networking.system"
  add_manifest "manifests/ssh.system"
  add_manifest "manifests/mdnsresponder.system"
  add_manifest "manifests/debug.system"
  if [[ "${PANTHERA_STAGE_CONFIGD:-0}" == "1" \
     || "${PANTHERA_STAGE_KERNEL_EVENT_MONITOR:-0}" == "1" \
     || "${PANTHERA_STAGE_IPCONFIGURATION:-0}" == "1" \
     || "${PANTHERA_STAGE_SC_PROBE:-0}" == "1" \
     || "${PANTHERA_STAGE_SC_NETWORK_STATE:-0}" == "1" \
     || "${PANTHERA_STAGE_SC_NETWORK_PROBE:-0}" == "1" ]]; then
    add_manifest "manifests/experimental-configd.system"
  fi
  if [[ "${PANTHERA_STAGE_ZFS:-0}" == "1" ]]; then
    add_manifest "manifests/zfs.system"
  fi
fi

if [[ -e "${OUTPUT_DIR}" ]]; then
  if [[ "${POPULATE_EXISTING}" == "1" ]]; then
    if [[ ! -d "${OUTPUT_DIR}" ]]; then
      echo "Existing rootfs output is not a directory: ${OUTPUT_DIR}" >&2
      exit 1
    fi
    # Freshly formatted HFS volumes may already contain host-created metadata
    # such as .Trashes or .fseventsd. The explicit flag means the caller owns
    # the existing directory and wants payload files staged in place.
  elif [[ "${FORCE}" != "1" ]]; then
    echo "Output directory already exists: ${OUTPUT_DIR}" >&2
    echo "Use --force to replace it." >&2
    exit 1
  else
    rm -rf "${OUTPUT_DIR}"
  fi
fi
mkdir -p "${OUTPUT_DIR}"

if [[ "${BUILD_COMPONENTS}" == "1" ]]; then
  "${ROOTFS_BUILD_COMPONENTS_SCRIPT}"
else
  echo ">>> Skipping component builds; pass --build-components or set PANTHERA_ROOTFS_BUILD_COMPONENTS=1 to rebuild inputs"
fi

"${ROOTFS_VERIFY_INPUTS_SCRIPT}" "${ROOTFS_MANIFESTS[@]}"

"${ROOTFS_POPULATE_SKELETON_SCRIPT}" "${OUTPUT_DIR}"
"${ROOTFS_STAGE_USERS_SCRIPT}" \
  "${OUTPUT_DIR}" \
  "${ROOT_PASSWORD_HASH}" \
  "${PANTHERA_PASSWORD_HASH}" \
  "${ROOT_LOGIN_SHELL}" \
  "${PANTHERA_LOGIN_SHELL}"
"${ROOTFS_STAGE_ETC_SCRIPT}" \
  "${OUTPUT_DIR}" \
  "${ROOTFS_ETC_DIR}" \
  "${SCRIPT_DIR}/System/Library/CoreServices/SystemVersion.plist" \
  "${ROOTFS_MANIFESTS[@]}"
"${ROOTFS_STAGE_LAUNCHDAEMONS_SCRIPT}" \
  "${OUTPUT_DIR}" \
  "${ROOTFS_LAUNCH_DAEMONS_DIR}" \
  "${ROOTFS_MANIFESTS[@]}"
"${ROOTFS_STAGE_BASE_PAYLOAD_SCRIPT}" "${OUTPUT_DIR}" "${ROOTFS_MANIFESTS[@]}"
"${ROOTFS_STAGE_SHARED_CACHE_SCRIPT}" "${OUTPUT_DIR}" "${SHARED_CACHE_BIN}" "${ROOTFS_MANIFESTS[@]}" >/dev/null
"${ROOTFS_STAGE_USER_TEMPLATES_SCRIPT}" "${OUTPUT_DIR}" "${PANTHERA_ROOT}/vendor/usertemplate-109" "${ROOTFS_MANIFESTS[@]}"
if [[ -x "${TERMINFO_STAGE_SCRIPT}" ]]; then
  "${TERMINFO_STAGE_SCRIPT}" "${OUTPUT_DIR}" "${ROOTFS_MANIFESTS[@]}"
fi
"${ROOTFS_STAGE_PACKAGE_PAYLOAD_SCRIPT}" "${OUTPUT_DIR}" "${ROOTFS_MANIFESTS[@]}"
"${ROOTFS_VERIFY_ROOTFS_SCRIPT}" "${OUTPUT_DIR}" "${ROOTFS_MANIFESTS[@]}"

echo "Created Panthera rootfs tree: ${OUTPUT_DIR}"
