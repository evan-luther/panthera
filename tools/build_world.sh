#!/usr/bin/env bash
# tools/build_world.sh — Canonical top-level build orchestrator for Panthera
#
# Orchestrates existing subsystem scripts in dependency order:
#   1. provenance  — Verify third-party checksums and tree hygiene
#   2. xnu         — Build XNU kernel
#   3. kexts       — Build boot and storage kernel extensions
#   4. efi         — Build EFI bootloader and stage EFI partition
#   5. runtime     — Build dyld, relink libSystem, build launchd, shared cache, verify exports
#   6. userland    — Build Panthera SDK, framework, daemon, and userland components
#   7. rootfs      — Assemble manifest-neutral rootfs tree and payload archive
#   8. image       — Create filesystem-specific root image (HFS and/or ZFS)
#   9. gate        — Run release gate verification (OPT-IN ONLY — launches QEMU)
#
# Preserves both the HFS recovery path and ZFS development path.
# Fail-fast: exits immediately on any step failure.
# Never launches QEMU or release gates unless explicitly requested.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# ─────────────────────────────────────────────────────────────────────────────
# Default Configuration
# ─────────────────────────────────────────────────────────────────────────────

TAG="${PANTHERA_WORLD_TAG:-world-$(date +%Y%m%d-%H%M%S)}"
ARTIFACT_DIR="${PANTHERA_WORLD_ARTIFACTS:-${PANTHERA_ROOT}/artifacts/build}"
TARGET="${PANTHERA_WORLD_TARGET:-hfs}" # hfs, zfs, all
DRY_RUN=0
LIST_ONLY=0
VERBOSE=0
FORCE_REBUILD=0
ENABLE_GATE=0
ENABLE_QEMU=0

# Network and rootfs defaults
NETWORK_OWNER="${PANTHERA_DEFAULT_NETWORK_OWNER:-ipconfiguration}"
STAGE_CONFIGD=1
STAGE_IPCONFIGURATION=1
ROOT_SHELL="${PANTHERA_ROOT_SHELL:-/bin/zsh}"
USER_SHELL="${PANTHERA_USER_SHELL:-/bin/zsh}"
ZFS_BUILDER_MODE="${PANTHERA_ZFS_ROOT_BUILDER_MODE:-zfs-control}"

# Bounded phase selection state
FROM_PHASE=""
TO_PHASE=""
SELECTED_PHASES=()
SKIPPED_PHASES=()

# Canonical ordered phase registry
ALL_PHASES=(
  "provenance"
  "xnu"
  "kexts"
  "efi"
  "runtime"
  "userland"
  "rootfs"
  "image"
  "gate"
)

# ─────────────────────────────────────────────────────────────────────────────
# Usage / Help
# ─────────────────────────────────────────────────────────────────────────────

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Panthera canonical top-level build orchestrator.
Orchestrates existing subsystem build scripts in dependency order without
duplicating build logic or launching QEMU unless explicitly requested.

Logical Phase Order:
  1. provenance   Verify third-party tarball checksums & source hygiene
  2. xnu          Build XNU kernel (xnu-10002.41.9)
  3. kexts        Build OpenIOKit, corecrypto, and storage kexts (SPL/ZFS)
  4. efi          Build BOOTX64.EFI bootloader & stage EFI boot tree
  5. runtime      Build dyld, relink libSystem, build launchd & shared cache
  6. userland     Build Panthera SDK, system daemons, core utilities
  7. rootfs       Assemble manifest-neutral rootfs tree & payload archive
  8. image        Assemble HFS and/or ZFS root disk images (no QEMU)
  9. gate         Run release gate verification (OPT-IN ONLY — launches QEMU)

Target Profiles:
  --target hfs    HFS alpha recovery path (default)
  --target zfs    ZFS active development path (includes OpenZFS & ZFS root)
  --target all    Build both HFS recovery and ZFS development targets
  --hfs, --zfs    Convenience aliases for --target hfs / --target zfs

Phase Selection & Recovery:
  --list, -l           List all phases, dependencies, and default run state
  --dry-run, -n        Print delegated commands in dependency order without building
  --phase, --only P    Run only specified phase(s) (comma-separated or repeated)
  --from-phase, --from P
                       Start build sequence from phase P
  --to-phase, --to P   Stop build sequence after phase P
  --skip-phase, --skip P
                       Exclude phase P from the build sequence
  --skip-image         Skip root image creation (stops at rootfs tree/payload)
  --skip-gate          Explicitly skip release gate (default behavior)

Gate & QEMU Controls:
  --gate, --run-gate   Opt-in to release gate verification (runs QEMU)
  --enable-qemu        Allow phases/modes that launch QEMU
  --zfs-builder-mode M Mode for ZFS root builder: zfs-control (default), direct, installer

Build Options:
  --tag NAME           Artifact tag (default: ${TAG})
  --artifacts DIR      Artifact directory (default: ${ARTIFACT_DIR})
  --force, -f          Force rebuild of all intermediate components
  --network-owner OWN  Default network owner: ipconfiguration (default) or netbringup
  --verbose, -v        Enable verbose logging
  --help, -h           Show this help message

Examples:
  # Inspect default build graph and delegated commands:
  $(basename "$0") --dry-run

  # List all available build phases:
  $(basename "$0") --list

  # Standard full build of the HFS recovery baseline (no QEMU):
  $(basename "$0") --target hfs

  # Full build for ZFS development with forced component rebuilds:
  $(basename "$0") --target zfs --force

  # Partial recovery: rebuild runtime and shared cache only:
  $(basename "$0") --phase runtime

  # Resume build from rootfs assembly through image creation:
  $(basename "$0") --from rootfs

  # Run full build including release gate verification (launches QEMU):
  $(basename "$0") --target hfs --gate --enable-qemu
EOF
}

# ─────────────────────────────────────────────────────────────────────────────
# Argument Parsing
# ─────────────────────────────────────────────────────────────────────────────

while [[ $# -gt 0 ]]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    --list|-l|--list-phases)
      LIST_ONLY=1
      shift
      ;;
    --dry-run|-n)
      DRY_RUN=1
      shift
      ;;
    --verbose|-v)
      VERBOSE=1
      shift
      ;;
    --force|-f)
      FORCE_REBUILD=1
      shift
      ;;
    --tag)
      TAG="$2"
      shift 2
      ;;
    --artifacts)
      ARTIFACT_DIR="$2"
      shift 2
      ;;
    --target)
      TARGET="$2"
      shift 2
      ;;
    --hfs)
      TARGET="hfs"
      shift
      ;;
    --zfs)
      TARGET="zfs"
      shift
      ;;
    --all|--all-targets)
      TARGET="all"
      shift
      ;;
    --phase|--only)
      IFS=',' read -ra ADDS <<< "$2"
      for p in "${ADDS[@]}"; do
        SELECTED_PHASES+=("$p")
      done
      shift 2
      ;;
    --from|--from-phase)
      FROM_PHASE="$2"
      shift 2
      ;;
    --to|--to-phase)
      TO_PHASE="$2"
      shift 2
      ;;
    --skip|--skip-phase)
      IFS=',' read -ra SKIPS <<< "$2"
      for p in "${SKIPS[@]}"; do
        SKIPPED_PHASES+=("$p")
      done
      shift 2
      ;;
    --skip-image|--no-image)
      SKIPPED_PHASES+=("image")
      shift
      ;;
    --skip-gate|--no-gate)
      ENABLE_GATE=0
      SKIPPED_PHASES+=("gate")
      shift
      ;;
    --gate|--run-gate|--with-gate)
      ENABLE_GATE=1
      ENABLE_QEMU=1
      shift
      ;;
    --enable-qemu|--with-qemu)
      ENABLE_QEMU=1
      shift
      ;;
    --network-owner)
      NETWORK_OWNER="$2"
      shift 2
      ;;
    --zfs-builder-mode)
      ZFS_BUILDER_MODE="$2"
      shift 2
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

# Validate target selection
case "${TARGET}" in
  hfs|zfs|all) ;;
  *)
    echo "Invalid target: '${TARGET}'. Must be one of: hfs, zfs, all." >&2
    exit 2
    ;;
esac

# ─────────────────────────────────────────────────────────────────────────────
# Phase Normalization & Validation
# ─────────────────────────────────────────────────────────────────────────────

normalize_phase() {
  local p="$1"
  case "$p" in
    1|provenance|checksums|provenance-checks) echo "provenance" ;;
    2|xnu|kernel) echo "xnu" ;;
    3|kexts|drivers|openiokit) echo "kexts" ;;
    4|efi|bootloader|stage-efi) echo "efi" ;;
    5|runtime|libsystem|dyld|launchd|shared-cache) echo "runtime" ;;
    6|userland|sdk|components) echo "userland" ;;
    7|rootfs|payload|tree) echo "rootfs" ;;
    8|image|images|root-image) echo "image" ;;
    9|gate|release-gate|verify|smokes) echo "gate" ;;
    *)
      echo "Unknown phase: '$p'" >&2
      exit 2
      ;;
  esac
}

phase_index() {
  local target_phase="$1"
  local idx=0
  for p in "${ALL_PHASES[@]}"; do
    if [[ "$p" == "$target_phase" ]]; then
      echo "$idx"
      return 0
    fi
    idx=$((idx + 1))
  done
  echo "-1"
}

phase_description() {
  case "$1" in
    provenance) echo "Verify third-party tarball checksums and source hygiene" ;;
    xnu)        echo "Build XNU kernel (xnu-10002.41.9)" ;;
    kexts)      echo "Build OpenIOKit suite, corecrypto, and storage kexts" ;;
    efi)        echo "Build BOOTX64.EFI bootloader and stage EFI partition tree" ;;
    runtime)    echo "Build dyld, relink libSystem, build launchd & shared cache" ;;
    userland)   echo "Build Panthera SDK, system daemons, and userland utilities" ;;
    rootfs)     echo "Assemble manifest-neutral rootfs tree and payload archive" ;;
    image)      echo "Create filesystem-specific root disk images (HFS/ZFS)" ;;
    gate)       echo "Execute release gate verification suite (launches QEMU)" ;;
  esac
}

# ─────────────────────────────────────────────────────────────────────────────
# List Mode
# ─────────────────────────────────────────────────────────────────────────────

if [[ "${LIST_ONLY}" == "1" ]]; then
  cat <<EOF
Panthera Build Phases (Dependency Order):
-----------------------------------------------------------------------------------------------------
#  Phase       Default   QEMU?  Description
-----------------------------------------------------------------------------------------------------
1  provenance  ENABLED   No     Verify third-party tarball checksums and source hygiene
2  xnu         ENABLED   No     Build XNU kernel (xnu-10002.41.9)
3  kexts       ENABLED   No     Build OpenIOKit suite, corecrypto, and storage kexts
4  efi         ENABLED   No     Build BOOTX64.EFI bootloader and stage EFI partition tree
5  runtime     ENABLED   No     Build dyld, relink libSystem, build launchd & shared cache
6  userland    ENABLED   No     Build Panthera SDK, system daemons, and userland utilities
7  rootfs      ENABLED   No     Assemble manifest-neutral rootfs tree and payload archive
8  image       ENABLED   No*    Create filesystem-specific root disk images (HFS/ZFS)
9  gate        OPT-IN    YES    Execute release gate verification suite (launches QEMU)
-----------------------------------------------------------------------------------------------------
* ZFS image creation defaults to zfs-control mode and requires --enable-qemu.

Target Profiles:
  - hfs: Builds HFS root image; runs alpha release gate when gate is opted-in.
  - zfs: Prepares OpenZFS, builds SPL/ZFS kexts & userland, builds ZFS root image; runs ZFS gate.
  - all: Builds both HFS and ZFS images and drivers.
EOF
  exit 0
fi

# ─────────────────────────────────────────────────────────────────────────────
# Compute Active Phases
# ─────────────────────────────────────────────────────────────────────────────

NORMALIZED_SELECTED=()
for p in "${SELECTED_PHASES[@]}"; do
  NORMALIZED_SELECTED+=("$(normalize_phase "$p")")
done

NORMALIZED_SKIPPED=()
for p in "${SKIPPED_PHASES[@]}"; do
  NORMALIZED_SKIPPED+=("$(normalize_phase "$p")")
done

FROM_IDX=0
if [[ -n "${FROM_PHASE}" ]]; then
  NORM_FROM="$(normalize_phase "${FROM_PHASE}")"
  FROM_IDX="$(phase_index "${NORM_FROM}")"
fi

TO_IDX=$(( ${#ALL_PHASES[@]} - 1 ))
if [[ -n "${TO_PHASE}" ]]; then
  NORM_TO="$(normalize_phase "${TO_PHASE}")"
  TO_IDX="$(phase_index "${NORM_TO}")"
fi

if (( FROM_IDX > TO_IDX )); then
  echo "Error: --from-phase (${FROM_PHASE}) occurs after --to-phase (${TO_PHASE}) in dependency order." >&2
  exit 2
fi

is_phase_selected() {
  local p="$1"
  local p_idx
  p_idx="$(phase_index "$p")"

  # Check bounds
  if (( p_idx < FROM_IDX || p_idx > TO_IDX )); then
    return 1
  fi

  # Check explicit skip
  for skipped in "${NORMALIZED_SKIPPED[@]}"; do
    if [[ "$skipped" == "$p" ]]; then
      return 1
    fi
  done

  # If explicit phases are specified, only include those
  if [[ ${#NORMALIZED_SELECTED[@]} -gt 0 ]]; then
    for sel in "${NORMALIZED_SELECTED[@]}"; do
      if [[ "$sel" == "$p" ]]; then
        return 0
      fi
    done
    return 1
  fi

  # Gate phase requires explicit opt-in unless selected directly
  if [[ "$p" == "gate" && "${ENABLE_GATE}" != "1" ]]; then
    return 1
  fi

  return 0
}

ACTIVE_PHASES=()
for p in "${ALL_PHASES[@]}"; do
  if is_phase_selected "$p"; then
    ACTIVE_PHASES+=("$p")
  fi
done

if [[ ${#ACTIVE_PHASES[@]} -eq 0 ]]; then
  echo "No build phases selected to run." >&2
  exit 0
fi

# ─────────────────────────────────────────────────────────────────────────────
# Xcode Toolchain Resolution (Required for XNU)
# ─────────────────────────────────────────────────────────────────────────────

resolve_xcode_developer_dir() {
  local required_tools=("clang" "mig" "migcom" "iig")
  local dev_candidates=()

  if [[ -n "${PANTHERA_DEVELOPER_DIR:-}" ]]; then
    dev_candidates+=("${PANTHERA_DEVELOPER_DIR}")
  fi

  if [[ -n "${DEVELOPER_DIR:-}" ]]; then
    dev_candidates+=("${DEVELOPER_DIR}")
  fi

  local current_dir
  if current_dir="$(xcode-select -p 2>/dev/null)" && [[ -n "${current_dir}" ]]; then
    dev_candidates+=("${current_dir}")
  fi

  shopt -s nullglob
  local xcode_apps=(/Applications/Xcode*.app)
  shopt -u nullglob
  if [[ ${#xcode_apps[@]} -gt 0 ]]; then
    while IFS= read -r app; do
      if [[ -n "${app}" && -d "${app}/Contents/Developer" ]]; then
        dev_candidates+=("${app}/Contents/Developer")
      fi
    done < <(printf "%s\n" "${xcode_apps[@]}" | LC_ALL=C sort)
  fi

  local unique_candidates=()
  local seen_reals=()
  local cand cand_real already_seen seen_r
  for cand in "${dev_candidates[@]}"; do
    cand_real="$(cd "${cand}" 2>/dev/null && pwd -P || echo "${cand}")"
    already_seen=0
    for seen_r in "${seen_reals[@]}"; do
      if [[ "${seen_r}" == "${cand_real}" ]]; then
        already_seen=1
        break
      fi
    done
    if [[ ${already_seen} -eq 0 ]]; then
      seen_reals+=("${cand_real}")
      unique_candidates+=("${cand}")
    fi
  done

  local selected_dev_dir=""
  local tool all_tools_found
  for cand in "${unique_candidates[@]}"; do
    if [[ ! -d "${cand}" ]]; then
      continue
    fi
    all_tools_found=1
    for tool in "${required_tools[@]}"; do
      if ! DEVELOPER_DIR="${cand}" xcrun -find "${tool}" >/dev/null 2>&1; then
        all_tools_found=0
        break
      fi
    done
    if [[ ${all_tools_found} -eq 1 ]]; then
      selected_dev_dir="${cand}"
      break
    fi
  done

  if [[ -z "${selected_dev_dir}" ]]; then
    echo "ERROR: No complete Xcode developer directory found containing required tools: ${required_tools[*]}" >&2
    if [[ ${#unique_candidates[@]} -eq 0 ]]; then
      echo "  No developer directory candidates found." >&2
    else
      echo "  Checked candidate developer directories:" >&2
      for cand in "${unique_candidates[@]}"; do
        if [[ ! -d "${cand}" ]]; then
          echo "    - ${cand}: directory does not exist" >&2
        else
          local missing=()
          for tool in "${required_tools[@]}"; do
            if ! DEVELOPER_DIR="${cand}" xcrun -find "${tool}" >/dev/null 2>&1; then
              missing+=("${tool}")
            fi
          done
          echo "    - ${cand}: missing required tools (${missing[*]})" >&2
        fi
      done
    fi
    echo "Set PANTHERA_DEVELOPER_DIR to an Xcode developer directory with full toolchain support." >&2
    exit 1
  fi

  export DEVELOPER_DIR="${selected_dev_dir}"
}

# Resolve complete Xcode toolchain when XNU is in the active build phases
for p in "${ACTIVE_PHASES[@]}"; do
  if [[ "${p}" == "xnu" ]]; then
    resolve_xcode_developer_dir
    break
  fi
done

# Propagate force rebuild state to child build scripts
if [[ "${FORCE_REBUILD}" == "1" ]]; then
  export PANTHERA_FORCE_REBUILD=1
fi

# ─────────────────────────────────────────────────────────────────────────────
# Delegated Command Definitions
# ─────────────────────────────────────────────────────────────────────────────

# Helper to execute or dry-run a command step
# Usage: run_cmd "<description>" <cmd> [args...]
run_cmd() {
  local desc="$1"
  shift

  if [[ "${DRY_RUN}" == "1" ]]; then
    echo "  [command] $*"
    return 0
  fi

  echo ">>> [Step] ${desc}"
  if [[ "${VERBOSE}" == "1" ]]; then
    echo "    $*"
  fi

  "$@"
}

# Helper to execute or dry-run with environment variables
# Usage: run_cmd_env "<description>" "ENV1=VAL1 ENV2=VAL2" <cmd> [args...]
run_cmd_env() {
  local desc="$1"
  local env_str="$2"
  shift 2

  if [[ "${DRY_RUN}" == "1" ]]; then
    echo "  [command] (env ${env_str}) $*"
    return 0
  fi

  echo ">>> [Step] ${desc}"
  if [[ "${VERBOSE}" == "1" ]]; then
    echo "    (env ${env_str}) $*"
  fi

  env ${env_str} "$@"
}

# ─────────────────────────────────────────────────────────────────────────────
# Phase Implementations
# ─────────────────────────────────────────────────────────────────────────────

phase_provenance() {
  echo "--- Phase 1: Provenance Checks ---"
  run_cmd "Verify third-party tarball SHA256 checksums" \
    bash "${PANTHERA_ROOT}/tools/verify_third_party_checksums.sh"

  if [[ "${TARGET}" == "zfs" || "${TARGET}" == "all" ]]; then
    run_cmd "Audit diagnostic cleanliness" \
      bash "${PANTHERA_ROOT}/tools/audit_diagnostics.sh"
    run_cmd "Audit ZFS staging hygiene" \
      bash "${PANTHERA_ROOT}/tools/audit_zfs_staging.sh"
  fi
}

phase_xnu() {
  echo "--- Phase 2: XNU Kernel ---"
  run_cmd "Build XNU kernel (x86_64 RELEASE)" \
    bash "${PANTHERA_ROOT}/build/build_xnu.sh"
}

phase_kexts() {
  echo "--- Phase 3: Kernel Extensions (kexts) ---"
  run_cmd "Build corecrypto kext" \
    bash "${PANTHERA_ROOT}/kexts/corecrypto/build_corecrypto.sh"

  run_cmd "Build OpenIOKit kext suite (platform, bus, storage, network, HID)" \
    bash "${PANTHERA_ROOT}/kexts/OpenIOKit/build_all.sh"

  if [[ "${TARGET}" == "zfs" || "${TARGET}" == "all" ]]; then
    run_cmd "Prepare OpenZFS source tree" \
      bash "${PANTHERA_ROOT}/tools/prepare_openzfs_source.sh"
    run_cmd "Build SPL kext (Solaris Porting Layer)" \
      bash "${PANTHERA_ROOT}/kexts/zfs/build_spl.sh"
    run_cmd "Build ZFS kext" \
      bash "${PANTHERA_ROOT}/kexts/zfs/build_zfs.sh"
  fi
}

phase_efi() {
  echo "--- Phase 4: EFI Bootloader & Staging ---"
  run_cmd "Build BOOTX64.EFI bootloader" \
    bash "${PANTHERA_ROOT}/boot/efi/build_bootx64.sh"

  local efi_stage_args=()
  if [[ "${TARGET}" == "zfs" || "${TARGET}" == "all" ]]; then
    efi_stage_args+=(--enable-zfs-spl --enable-zfs)
  fi

  run_cmd "Stage EFI boot tree (kernel, bootloader, boot-critical kexts)" \
    bash "${PANTHERA_ROOT}/boot/efi/stage_phase2_efi.sh" "${efi_stage_args[@]}"
}

phase_runtime() {
  echo "--- Phase 5: Runtime, libSystem, dyld, launchd ---"
  run_cmd "Build libSystem constituent libraries from source" \
    bash "${PANTHERA_ROOT}/userland/libsystem/build_libsystem.sh"

  run_cmd "Build libunwind runtime support" \
    bash "${PANTHERA_ROOT}/userland/libunwind/build_libunwind.sh"

  run_cmd "Build custom dyld dynamic loader" \
    bash "${PANTHERA_ROOT}/userland/dyld/build_dyld.sh"

  run_cmd "Relink libpanthera_extra.dylib shim provider" \
    bash "${PANTHERA_ROOT}/userland/libsystem/build/relink_libpanthera_extra.sh"

  run_cmd "Relink libSystem.B.dylib umbrella library" \
    bash "${PANTHERA_ROOT}/userland/libsystem/build/relink_libSystem.sh"

  run_cmd "Build libbsm audit library from source" \
    bash "${PANTHERA_ROOT}/userland/libbsm/build_libbsm.sh"

  run_cmd "Build launchd PID 1 runtime" \
    bash "${PANTHERA_ROOT}/userland/launchd/build_launchd.sh"

  run_cmd "Build dyld shared cache" \
    bash "${PANTHERA_ROOT}/tools/build_shared_cache.sh"

  run_cmd "Verify runtime symbol exports and runtime-owned binaries" \
    bash "${PANTHERA_ROOT}/userland/libsystem/verify_exports.sh" --runtime-only
}

phase_userland() {
  echo "--- Phase 6: SDK & Userland Components ---"
  run_cmd "Build Panthera SDK (Panthera.sdk sysroot image)" \
    bash "${PANTHERA_ROOT}/userland/panthera_sdk/build_panthera_sdk.sh"

  local component_env="PANTHERA_STAGE_CONFIGD=${STAGE_CONFIGD} PANTHERA_STAGE_IPCONFIGURATION=${STAGE_IPCONFIGURATION}"
  if [[ "${FORCE_REBUILD}" == "1" ]]; then
    component_env="${component_env} PANTHERA_FORCE_REBUILD=1"
  fi
  if [[ "${TARGET}" == "zfs" || "${TARGET}" == "all" ]]; then
    component_env="${component_env} PANTHERA_STAGE_ZFS=1"
  fi

  run_cmd_env "Build system userland components via manifest-driven scripts" \
    "${component_env}" \
    bash "${PANTHERA_ROOT}/rootfs/scripts/build_components.sh"

  run_cmd "Build file management utilities (install/file_cmds)" \
    bash "${PANTHERA_ROOT}/userland/file_cmds/build_file_cmds.sh"

  run_cmd "Build sudo utility" \
    bash "${PANTHERA_ROOT}/userland/sudo/build_sudo.sh"

  run_cmd "Rebuild dyld shared cache with completed userland libraries" \
    bash "${PANTHERA_ROOT}/tools/build_shared_cache.sh"

  run_cmd "Verify full runtime and userland symbol closure" \
    bash "${PANTHERA_ROOT}/userland/libsystem/verify_exports.sh"
}

phase_rootfs() {
  echo "--- Phase 7: Manifest-Neutral Rootfs Tree & Payload ---"
  local tree_env="PANTHERA_DEFAULT_NETWORK_OWNER=${NETWORK_OWNER} PANTHERA_ROOT_SHELL=${ROOT_SHELL} PANTHERA_USER_SHELL=${USER_SHELL}"
  local tree_args=(--no-build-components)
  local payload_args=()
  if [[ "${TARGET}" == "zfs" || "${TARGET}" == "all" ]]; then
    tree_env="${tree_env} PANTHERA_STAGE_ZFS=1"
  fi
  if [[ "${FORCE_REBUILD}" == "1" ]]; then
    tree_args+=(--force)
    payload_args+=(--force)
  fi

  run_cmd_env "Assemble manifest-neutral rootfs directory tree" \
    "${tree_env}" \
    bash "${PANTHERA_ROOT}/rootfs/create_rootfs_tree.sh" "${tree_args[@]}"

  run_cmd_env "Create manifest-neutral rootfs payload tarball archive" \
    "${tree_env}" \
    bash "${PANTHERA_ROOT}/rootfs/create_rootfs_payload_tar.sh" "${payload_args[@]}"
}

phase_image() {
  echo "--- Phase 8: Root Disk Images ---"
  local img_env="PANTHERA_DEFAULT_NETWORK_OWNER=${NETWORK_OWNER} PANTHERA_ROOT_SHELL=${ROOT_SHELL} PANTHERA_USER_SHELL=${USER_SHELL}"

  if [[ "${TARGET}" == "hfs" || "${TARGET}" == "all" ]]; then
    local hfs_args=(--no-build-components)
    if [[ "${FORCE_REBUILD}" == "1" ]]; then
      hfs_args+=(--force)
    fi
    run_cmd_env "Create HFS root disk image (images/qemu/panthera-root.img)" \
      "${img_env}" \
      bash "${PANTHERA_ROOT}/rootfs/create_hfs_root_image.sh" "${hfs_args[@]}"
  fi

  if [[ "${TARGET}" == "zfs" || "${TARGET}" == "all" ]]; then
    local zfs_builder="${ZFS_BUILDER_MODE}"
    local zfs_args=(--builder-mode "${zfs_builder}")
    if [[ "${zfs_builder}" != "direct" && "${ENABLE_QEMU}" != "1" && "${DRY_RUN}" != "1" ]]; then
      echo "ZFS builder mode '${zfs_builder}' launches QEMU; pass --enable-qemu or select --zfs-builder-mode direct." >&2
      exit 2
    fi
    if [[ "${FORCE_REBUILD}" == "1" ]]; then
      zfs_args+=(--force)
    fi

    run_cmd_env "Create ZFS root disk image (images/qemu/panthera-zfs-root.img)" \
      "${img_env}" \
      bash "${PANTHERA_ROOT}/rootfs/create_zfs_root_image.sh" "${zfs_args[@]}"
  fi
}

phase_gate() {
  echo "--- Phase 9: Release Gate Verification (QEMU) ---"
  if [[ "${ENABLE_QEMU}" != "1" && "${ENABLE_GATE}" != "1" ]]; then
    echo "Error: Release gate requires QEMU. Re-run with --gate or --enable-qemu to execute." >&2
    exit 1
  fi

  mkdir -p "${ARTIFACT_DIR}"

  if [[ "${TARGET}" == "hfs" || "${TARGET}" == "all" ]]; then
    run_cmd "Run Panthera alpha release gate (HFS root boot & smoke suite)" \
      bash "${PANTHERA_ROOT}/tools/alpha_release_gate.sh" \
      --tag "${TAG}-alpha" \
      --artifacts "${ARTIFACT_DIR}" \
      --root-kind hfs
  fi

  if [[ "${TARGET}" == "zfs" || "${TARGET}" == "all" ]]; then
    run_cmd "Run Panthera ZFS release gate (ZFS pool/root boot & smoke suite)" \
      bash "${PANTHERA_ROOT}/tools/zfs_release_gate.sh" \
      --tag "${TAG}-zfs" \
      --artifacts "${ARTIFACT_DIR}"
  fi
}

# ─────────────────────────────────────────────────────────────────────────────
# Execution Dispatcher
# ─────────────────────────────────────────────────────────────────────────────

echo "======================================================================"
echo "Panthera World Build Orchestrator"
echo "======================================================================"
echo "Tag:        ${TAG}"
echo "Target:     ${TARGET}"
echo "Artifacts:  ${ARTIFACT_DIR}"
echo "Network:    ${NETWORK_OWNER}"
echo "QEMU Opt-In: $([ "${ENABLE_QEMU}" = "1" ] && echo "YES" || echo "NO (safe dry/build mode)")"
echo "Dry Run:    $([ "${DRY_RUN}" = "1" ] && echo "YES (no commands executed)" || echo "NO")"
if [[ -n "${DEVELOPER_DIR:-}" ]]; then
  echo "Developer:  ${DEVELOPER_DIR}"
fi
echo "Phases:     ${ACTIVE_PHASES[*]}"
echo "======================================================================"
echo ""

TOTAL_START=$(date +%s)
FAILED_PHASE=""

for current_phase in "${ACTIVE_PHASES[@]}"; do
  PHASE_START=$(date +%s)
  echo "======================================================================"
  echo ">>> Entering Phase: ${current_phase} — $(phase_description "${current_phase}")"
  echo "======================================================================"

  case "${current_phase}" in
    provenance) phase_provenance ;;
    xnu)        phase_xnu ;;
    kexts)      phase_kexts ;;
    efi)        phase_efi ;;
    runtime)    phase_runtime ;;
    userland)   phase_userland ;;
    rootfs)     phase_rootfs ;;
    image)      phase_image ;;
    gate)       phase_gate ;;
  esac

  PHASE_END=$(date +%s)
  PHASE_DURATION=$(( PHASE_END - PHASE_START ))
  echo ">>> Completed Phase: ${current_phase} (${PHASE_DURATION}s)"
  echo ""
done

TOTAL_END=$(date +%s)
TOTAL_DURATION=$(( TOTAL_END - TOTAL_START ))

echo "======================================================================"
echo "Panthera World Build Complete: SUCCESS (${TOTAL_DURATION}s total)"
echo "Active Phases Executed: ${ACTIVE_PHASES[*]}"
echo "======================================================================"
