#!/usr/bin/env bash
# tools/clean_world_proof.sh — Clean-world build & release proof entrypoint (R1.3)
#
# Proves a pristine committed checkout end-to-end:
#   1. Verifies the parent worktree is completely clean (uncommitted changes refused)
#   2. Clones the repository into an isolated temporary directory with git clone --local --no-hardlinks
#   3. Bootstraps external world sources via tools/fetch_world_sources.sh
#   4. Runs the canonical full HFS build graph with forced rebuilds
#   5. Runs the canonical HFS alpha release gate (boot, OpenSSH, mDNS, SDK, toolchain)
#   6. Runs the canonical full ZFS build graph with installer mode & QEMU enabled
#   7. Preserves the generated ZFS root image as an immutable seed (computes SHA-256)
#   8. Runs the canonical ZFS release gate with distinct seed, control, and output paths
#   9. Verifies immutable seed integrity
#  10. Writes an atomic Markdown summary & latest copy in parent artifacts/release/
#  11. Cleans up temporary clone only on PASS (retains on failure or explicit option)

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# ─────────────────────────────────────────────────────────────────────────────
# Default Configuration
# ─────────────────────────────────────────────────────────────────────────────

TAG="${PANTHERA_CLEAN_WORLD_TAG:-clean-world-$(date +%Y%m%d-%H%M%S)}"
ARTIFACT_DIR="${PANTHERA_CLEAN_WORLD_ARTIFACTS:-${PANTHERA_ROOT}/artifacts/release}"
BOOT_TIMEOUT="${PANTHERA_CLEAN_WORLD_BOOT_TIMEOUT:-420}"
REMOTE_TIMEOUT="${PANTHERA_CLEAN_WORLD_REMOTE_TIMEOUT:-900}"
PORT="${PANTHERA_CLEAN_WORLD_SSH_PORT:-}"
RETAINED_TEMP_DIR="${PANTHERA_CLEAN_WORLD_TEMP_DIR:-}"
KEEP_TEMP=0

# ─────────────────────────────────────────────────────────────────────────────
# Usage / Help
# ─────────────────────────────────────────────────────────────────────────────

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Panthera clean-world proof entrypoint:
  Proves that a fresh, isolated clone of committed source state builds
  both full HFS and ZFS world graphs and passes all release gates without
  relying on host-dirty worktree state, cached build products, or uncommitted files.

Sequence:
  1. Refuse dirty worktree at source repository
  2. Clone committed state into an isolated temporary directory via git clone --local --no-hardlinks
  3. Bootstrap world sources (tools/fetch_world_sources.sh)
  4. Full HFS build (tools/build_world.sh --target hfs --force)
  5. HFS release gate (tools/alpha_release_gate.sh --root-kind hfs)
  6. Full ZFS bootstrap build (tools/build_world.sh --target zfs --force --zfs-builder-mode installer --enable-qemu)
  7. Preserve generated ZFS root as immutable seed (record SHA-256)
  8. ZFS release gate (tools/zfs_release_gate.sh with isolated seed/control/output paths)
  9. Verify seed immutability
 10. Emit atomic summary to parent artifacts/release/

Options:
  --tag NAME               Artifact tag prefix (default: ${TAG})
  --artifacts DIR          Parent release artifact directory (default: ${ARTIFACT_DIR})
  --retained-temp-dir DIR  Use/retain specific directory for temp clone (default: auto mktemp)
  --temp-dir DIR           Alias for --retained-temp-dir
  --keep-temp              Do not delete temporary clone on successful completion
  --timeout SECONDS        Per-boot timeout forwarded to gates (default: ${BOOT_TIMEOUT})
  --boot-timeout SECONDS   Alias for --timeout
  --remote-timeout SECONDS Per-SSH-command timeout forwarded to gates (default: ${REMOTE_TIMEOUT})
  --port PORT              Host SSH forward port override (default: gate defaults)
  --help, -h               Show this help message
EOF
}

# ─────────────────────────────────────────────────────────────────────────────
# Argument Parsing
# ─────────────────────────────────────────────────────────────────────────────

while [[ $# -gt 0 ]]; do
  case "$1" in
    --tag)
      if [[ $# -lt 2 || -z "${2:-}" ]]; then
        echo "Error: --tag requires a value" >&2
        usage >&2
        exit 2
      fi
      TAG="$2"
      shift 2
      ;;
    --tag=*)
      TAG="${1#*=}"
      if [[ -z "${TAG}" ]]; then
        echo "Error: --tag requires a value" >&2
        usage >&2
        exit 2
      fi
      shift
      ;;
    --artifacts|--artifact-dir)
      if [[ $# -lt 2 || -z "${2:-}" ]]; then
        echo "Error: $1 requires a directory path" >&2
        usage >&2
        exit 2
      fi
      ARTIFACT_DIR="$2"
      shift 2
      ;;
    --artifacts=*|--artifact-dir=*)
      ARTIFACT_DIR="${1#*=}"
      if [[ -z "${ARTIFACT_DIR}" ]]; then
        echo "Error: $1 requires a directory path" >&2
        usage >&2
        exit 2
      fi
      shift
      ;;
    --retained-temp-dir|--retain-temp-dir|--temp-dir)
      if [[ $# -lt 2 || -z "${2:-}" ]]; then
        echo "Error: $1 requires a directory path" >&2
        usage >&2
        exit 2
      fi
      RETAINED_TEMP_DIR="$2"
      KEEP_TEMP=1
      shift 2
      ;;
    --retained-temp-dir=*|--retain-temp-dir=*|--temp-dir=*)
      RETAINED_TEMP_DIR="${1#*=}"
      if [[ -z "${RETAINED_TEMP_DIR}" ]]; then
        echo "Error: $1 requires a directory path" >&2
        usage >&2
        exit 2
      fi
      KEEP_TEMP=1
      shift
      ;;
    --keep-temp)
      KEEP_TEMP=1
      shift
      ;;
    --timeout|--boot-timeout)
      if [[ $# -lt 2 || -z "${2:-}" ]]; then
        echo "Error: $1 requires a seconds argument" >&2
        usage >&2
        exit 2
      fi
      BOOT_TIMEOUT="$2"
      shift 2
      ;;
    --timeout=*|--boot-timeout=*)
      BOOT_TIMEOUT="${1#*=}"
      if [[ -z "${BOOT_TIMEOUT}" ]]; then
        echo "Error: $1 requires a seconds argument" >&2
        usage >&2
        exit 2
      fi
      shift
      ;;
    --remote-timeout)
      if [[ $# -lt 2 || -z "${2:-}" ]]; then
        echo "Error: --remote-timeout requires a seconds argument" >&2
        usage >&2
        exit 2
      fi
      REMOTE_TIMEOUT="$2"
      shift 2
      ;;
    --remote-timeout=*)
      REMOTE_TIMEOUT="${1#*=}"
      if [[ -z "${REMOTE_TIMEOUT}" ]]; then
        echo "Error: --remote-timeout requires a seconds argument" >&2
        usage >&2
        exit 2
      fi
      shift
      ;;
    --port)
      if [[ $# -lt 2 || -z "${2:-}" ]]; then
        echo "Error: --port requires a port argument" >&2
        usage >&2
        exit 2
      fi
      PORT="$2"
      shift 2
      ;;
    --port=*)
      PORT="${1#*=}"
      if [[ -z "${PORT}" ]]; then
        echo "Error: --port requires a port argument" >&2
        usage >&2
        exit 2
      fi
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "Error: Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if ! [[ "${BOOT_TIMEOUT}" =~ ^[1-9][0-9]*$ ]]; then
  echo "Error: --timeout/--boot-timeout must be a positive integer (got '${BOOT_TIMEOUT}')" >&2
  exit 2
fi

if ! [[ "${REMOTE_TIMEOUT}" =~ ^[1-9][0-9]*$ ]]; then
  echo "Error: --remote-timeout must be a positive integer (got '${REMOTE_TIMEOUT}')" >&2
  exit 2
fi

if [[ -n "${PORT}" ]] && ! [[ "${PORT}" =~ ^[1-9][0-9]*$ ]]; then
  echo "Error: --port must be a valid positive port number (got '${PORT}')" >&2
  exit 2
fi

# Normalize artifact directory to absolute path
if [[ "${ARTIFACT_DIR}" != /* ]]; then
  ARTIFACT_DIR="${PANTHERA_ROOT}/${ARTIFACT_DIR}"
fi
mkdir -p "${ARTIFACT_DIR}"

# ─────────────────────────────────────────────────────────────────────────────
# SHA-256 Helper
# ─────────────────────────────────────────────────────────────────────────────

compute_sha256() {
  local target="$1"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "${target}" | awk '{print $1}'
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "${target}" | awk '{print $1}'
  elif command -v openssl >/dev/null 2>&1; then
    openssl dgst -sha256 "${target}" | awk '{print $NF}'
  else
    return 1
  fi
}

# ─────────────────────────────────────────────────────────────────────────────
# State & Trap Management
# ─────────────────────────────────────────────────────────────────────────────

SUMMARY_PATH="${ARTIFACT_DIR}/${TAG}.summary.md"
LATEST_SUMMARY_PATH="${ARTIFACT_DIR}/latest-clean-world.summary.md"

TEMP_CLONE_DIR=""
CLEANUP_TEMP=$(( KEEP_TEMP == 0 ? 1 : 0 ))
OVERALL_RESULT="FAIL"
SUMMARY_WRITTEN=0
EXECUTION_BEGUN=0
CURRENT_STEP=""
START_TIME_EPOCH="$(date +%s)"
END_TIME_EPOCH=""

GIT_COMMIT="unknown"
GIT_BRANCH="unknown"
SEED_SHA256="N/A"
FINAL_SEED_SHA256="N/A"

declare -a STEP_NAMES=()
declare -a STEP_RESULTS=()
declare -a STEP_DURATIONS=()
declare -a STEP_COMMANDS=()
declare -a STEP_LOGS=()
declare -a EXTRA_ARTIFACTS=()

cleanup_tmp() {
  rm -f "${SUMMARY_PATH}.tmp.$$" "${LATEST_SUMMARY_PATH}.tmp.$$"
}

write_summary() {
  local result="$1"
  local tmp_summary="${SUMMARY_PATH}.tmp.$$"
  local tmp_latest="${LATEST_SUMMARY_PATH}.tmp.$$"

  local total_steps=${#STEP_NAMES[@]}
  local passed_steps=0
  local failed_steps=0

  for res in "${STEP_RESULTS[@]}"; do
    if [[ "${res}" == "PASS" ]]; then
      ((passed_steps++)) || true
    else
      ((failed_steps++)) || true
    fi
  done

  local total_elapsed=0
  if [[ -n "${END_TIME_EPOCH}" && "${END_TIME_EPOCH}" -ge "${START_TIME_EPOCH}" ]]; then
    total_elapsed=$(( END_TIME_EPOCH - START_TIME_EPOCH ))
  else
    local now_epoch
    now_epoch="$(date +%s)"
    total_elapsed=$(( now_epoch - START_TIME_EPOCH ))
  fi

  {
    echo "# Panthera Clean World Build Proof"
    echo
    echo "**Result: ${result}**"
    echo
    echo "## Metadata"
    echo
    echo "- **Tag:** \`${TAG}\`"
    echo "- **Commit:** \`${GIT_COMMIT}\`"
    echo "- **Branch / Ref:** \`${GIT_BRANCH}\`"
    echo "- **Source Root:** \`${PANTHERA_ROOT}\`"
    echo "- **Clean Clone Directory:** \`${TEMP_CLONE_DIR:-none}\`"
    echo "- **Temp Clone Retained:** $([[ "${CLEANUP_TEMP}" -eq 0 || "${result}" != "PASS" ]] && echo "Yes (\`${TEMP_CLONE_DIR:-none}\`)" || echo "No (cleaned up on PASS)")"
    echo "- **ZFS Seed SHA-256:** \`${SEED_SHA256}\`"
    if [[ "${SEED_SHA256}" != "N/A" && "${FINAL_SEED_SHA256}" != "N/A" ]]; then
      echo "- **Post-Gate Seed SHA-256:** \`${FINAL_SEED_SHA256}\`"
    fi
    echo "- **Boot Timeout:** \`${BOOT_TIMEOUT}s\`"
    echo "- **Remote Timeout:** \`${REMOTE_TIMEOUT}s\`"
    echo "- **Total Duration:** \`${total_elapsed}s\`"
    echo "- **Start Time:** \`$(date -u -r "${START_TIME_EPOCH}" +"%Y-%m-%d %H:%M:%SZ" 2>/dev/null || date -u +"%Y-%m-%d %H:%M:%SZ")\`"
    if [[ -n "${END_TIME_EPOCH}" ]]; then
      echo "- **End Time:** \`$(date -u -r "${END_TIME_EPOCH}" +"%Y-%m-%d %H:%M:%SZ" 2>/dev/null || date -u +"%Y-%m-%d %H:%M:%SZ")\`"
    fi
    echo
    echo "## Steps"
    echo
    echo "| Step | Result | Duration | Command | Log |"
    echo "|---|---|---|---|---|"
    if [[ ${total_steps} -eq 0 ]]; then
      echo "| initial_setup | ${result} | 0s | pre-flight | none |"
    else
      for ((i = 0; i < total_steps; i++)); do
        local s_name="${STEP_NAMES[$i]}"
        local s_res="${STEP_RESULTS[$i]}"
        local s_dur="${STEP_DURATIONS[$i]}"
        local s_cmd="${STEP_COMMANDS[$i]}"
        local s_log="${STEP_LOGS[$i]}"
        local rel_log="${s_log}"
        if [[ "${rel_log}" == "${PANTHERA_ROOT}/"* ]]; then
          rel_log="${rel_log#${PANTHERA_ROOT}/}"
        fi
        echo "| ${s_name} | ${s_res} | ${s_dur} | \`${s_cmd}\` | \`${rel_log}\` |"
      done
    fi
    echo
    echo "## Step Summary"
    echo
    echo "- **Total Executed Steps:** ${total_steps}"
    echo "- **Passed Steps:** ${passed_steps}"
    echo "- **Failed Steps:** ${failed_steps}"
    echo
    echo "## Artifacts"
    echo
    local any_artifacts=0
    for ((i = 0; i < total_steps; i++)); do
      local s_log="${STEP_LOGS[$i]}"
      if [[ -f "${s_log}" ]]; then
        local rel_log="${s_log}"
        if [[ "${rel_log}" == "${PANTHERA_ROOT}/"* ]]; then
          rel_log="${rel_log#${PANTHERA_ROOT}/}"
        fi
        echo "- Log (${STEP_NAMES[$i]}): \`${rel_log}\`"
        any_artifacts=1
      fi
    done
    for extra in "${EXTRA_ARTIFACTS[@]}"; do
      if [[ -f "${extra}" ]]; then
        local rel_extra="${extra}"
        if [[ "${rel_extra}" == "${PANTHERA_ROOT}/"* ]]; then
          rel_extra="${rel_extra#${PANTHERA_ROOT}/}"
        fi
        echo "- Gate Summary: \`${rel_extra}\`"
        any_artifacts=1
      fi
    done
    if [[ ${any_artifacts} -eq 0 ]]; then
      echo "- None."
    fi
  } > "${tmp_summary}"

  cp -f "${tmp_summary}" "${tmp_latest}"
  mv -f "${tmp_summary}" "${SUMMARY_PATH}"
  mv -f "${tmp_latest}" "${LATEST_SUMMARY_PATH}"
  SUMMARY_WRITTEN=1
}

cleanup_on_signal() {
  local sig="$1"
  trap - INT TERM EXIT
  END_TIME_EPOCH="$(date +%s)"
  if [[ -n "${CURRENT_STEP}" ]]; then
    STEP_NAMES+=("${CURRENT_STEP}")
    STEP_RESULTS+=("INTERRUPTED")
    STEP_DURATIONS+=("unknown")
    STEP_COMMANDS+=("interrupted by signal SIG${sig}")
    STEP_LOGS+=("${ARTIFACT_DIR}/${TAG}.${CURRENT_STEP}.log")
  fi
  OVERALL_RESULT="FAIL"
  write_summary "FAIL"
  cleanup_tmp
  if [[ -n "${TEMP_CLONE_DIR}" && -d "${TEMP_CLONE_DIR}" ]]; then
    echo "Retaining temporary clone directory due to signal interrupt: ${TEMP_CLONE_DIR}" >&2
  fi
  case "${sig}" in
    INT)  exit 130 ;;
    TERM) exit 143 ;;
    *)    exit 1 ;;
  esac
}

cleanup_on_exit() {
  local exit_code=$?
  trap - INT TERM EXIT
  if [[ -z "${END_TIME_EPOCH}" ]]; then
    END_TIME_EPOCH="$(date +%s)"
  fi
  if [[ "${EXECUTION_BEGUN}" -eq 1 && "${SUMMARY_WRITTEN}" -eq 0 ]]; then
    write_summary "${OVERALL_RESULT}"
  fi
  cleanup_tmp
  if [[ "${OVERALL_RESULT}" == "PASS" && "${CLEANUP_TEMP}" -eq 1 ]]; then
    if [[ -n "${TEMP_CLONE_DIR}" && -d "${TEMP_CLONE_DIR}" ]]; then
      rm -rf "${TEMP_CLONE_DIR}"
    fi
  else
    if [[ -n "${TEMP_CLONE_DIR}" && -d "${TEMP_CLONE_DIR}" ]]; then
      echo "Retaining clean-world temporary clone directory: ${TEMP_CLONE_DIR}" >&2
    fi
  fi
  exit "${exit_code}"
}

trap 'cleanup_on_signal INT' INT
trap 'cleanup_on_signal TERM' TERM
trap cleanup_on_exit EXIT

# ─────────────────────────────────────────────────────────────────────────────
# Step Execution Helper
# ─────────────────────────────────────────────────────────────────────────────

run_step() {
  local step_name="$1"
  local log_file="$2"
  local work_dir="$3"
  shift 3
  local cmd=("$@")

  CURRENT_STEP="${step_name}"
  echo "================================================================================"
  echo "==> [${step_name}] Starting: ${cmd[*]}"
  echo "    Directory: ${work_dir}"
  echo "    Log:       ${log_file}"
  echo "================================================================================"

  local step_start
  step_start="$(date +%s)"
  local exit_code=0

  mkdir -p "$(dirname "${log_file}")"

  {
    echo "================================================================================"
    echo "Step:      ${step_name}"
    echo "Timestamp: $(date -u +"%Y-%m-%d %H:%M:%SZ")"
    echo "Directory: ${work_dir}"
    echo "Command:   ${cmd[*]}"
    echo "================================================================================"
    echo
  } > "${log_file}"

  (
    cd "${work_dir}"
    "${cmd[@]}"
  ) >> "${log_file}" 2>&1 || exit_code=$?

  local step_end
  step_end="$(date +%s)"
  local duration=$(( step_end - step_start ))

  local step_result="PASS"
  if [[ ${exit_code} -ne 0 ]]; then
    step_result="FAIL"
  fi

  STEP_NAMES+=("${step_name}")
  STEP_RESULTS+=("${step_result}")
  STEP_DURATIONS+=("${duration}s")
  STEP_COMMANDS+=("${cmd[*]}")
  STEP_LOGS+=("${log_file}")

  if [[ ${exit_code} -ne 0 ]]; then
    echo "==> [${step_name}] FAILED with exit code ${exit_code} after ${duration}s" >&2
    echo "    Log captured at: ${log_file}" >&2
    CURRENT_STEP=""
    OVERALL_RESULT="FAIL"
    END_TIME_EPOCH="$(date +%s)"
    write_summary "FAIL"
    return "${exit_code}"
  fi

  echo "==> [${step_name}] PASSED (${duration}s)"
  CURRENT_STEP=""
  return 0
}

# ─────────────────────────────────────────────────────────────────────────────
# Step 1: Pre-flight & Clean Worktree Check
# ─────────────────────────────────────────────────────────────────────────────

EXECUTION_BEGUN=1

echo "================================================================================"
echo "Panthera Clean World Build & Gate Proof (R1.3)"
echo "================================================================================"
echo "Tag:            ${TAG}"
echo "Source Root:    ${PANTHERA_ROOT}"
echo "Artifacts:      ${ARTIFACT_DIR}"
echo "Boot Timeout:   ${BOOT_TIMEOUT}s"
echo "Remote Timeout: ${REMOTE_TIMEOUT}s"
echo "================================================================================"

if ! command -v git >/dev/null 2>&1; then
  echo "Error: git command not found in PATH" >&2
  exit 1
fi

if ! git -C "${PANTHERA_ROOT}" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  echo "Error: Source root is not a valid git repository: ${PANTHERA_ROOT}" >&2
  exit 1
fi

DIRTY_STATUS="$(git -C "${PANTHERA_ROOT}" status --porcelain 2>/dev/null || true)"
if [[ -n "${DIRTY_STATUS}" ]]; then
  echo "Error: Source worktree at ${PANTHERA_ROOT} is dirty." >&2
  echo "Clean-world proof requires a pristine committed state because local clone only tests committed state." >&2
  echo "Uncommitted changes detected:" >&2
  echo "${DIRTY_STATUS}" >&2
  exit 1
fi

GIT_COMMIT="$(git -C "${PANTHERA_ROOT}" rev-parse HEAD 2>/dev/null || echo "unknown")"
GIT_BRANCH="$(git -C "${PANTHERA_ROOT}" rev-parse --abbrev-ref HEAD 2>/dev/null || echo "HEAD")"

# ─────────────────────────────────────────────────────────────────────────────
# Step 2: Isolated Local Clone
# ─────────────────────────────────────────────────────────────────────────────

if [[ -n "${RETAINED_TEMP_DIR}" ]]; then
  if [[ "${RETAINED_TEMP_DIR}" != /* ]]; then
    TEMP_CLONE_DIR="${PANTHERA_ROOT}/${RETAINED_TEMP_DIR}"
  else
    TEMP_CLONE_DIR="${RETAINED_TEMP_DIR}"
  fi
  if [[ -d "${TEMP_CLONE_DIR}" ]]; then
    if [[ -n "$(ls -A "${TEMP_CLONE_DIR}" 2>/dev/null)" ]]; then
      echo "Error: Specified temp clone directory is not empty: ${TEMP_CLONE_DIR}" >&2
      exit 2
    fi
  else
    mkdir -p "${TEMP_CLONE_DIR}"
  fi
else
  TEMP_CLONE_DIR="$(mktemp -d "${TMPDIR:-/tmp}/panthera-clean-world-XXXXXX")"
fi

CLONE_LOG="${ARTIFACT_DIR}/${TAG}.01_clone.log"
run_step clone_repository \
  "${CLONE_LOG}" \
  "${PANTHERA_ROOT}" \
  git clone --local --no-hardlinks "${PANTHERA_ROOT}" "${TEMP_CLONE_DIR}" || exit 1

# Setup artifact subdirectories inside the isolated clone
CLONE_ARTIFACT_DIR="${TEMP_CLONE_DIR}/artifacts"
CLONE_BUILD_ARTIFACTS="${CLONE_ARTIFACT_DIR}/build"
CLONE_RELEASE_ARTIFACTS="${CLONE_ARTIFACT_DIR}/release"
CLONE_BOOT_ARTIFACTS="${CLONE_ARTIFACT_DIR}/boot"
mkdir -p "${CLONE_BUILD_ARTIFACTS}" "${CLONE_RELEASE_ARTIFACTS}" "${CLONE_BOOT_ARTIFACTS}"

# ─────────────────────────────────────────────────────────────────────────────
# Step 3: Bootstrap World Sources
# ─────────────────────────────────────────────────────────────────────────────

BOOTSTRAP_LOG="${ARTIFACT_DIR}/${TAG}.02_bootstrap_world_sources.log"
run_step bootstrap_world_sources \
  "${BOOTSTRAP_LOG}" \
  "${TEMP_CLONE_DIR}" \
  bash tools/fetch_world_sources.sh || exit 1

# ─────────────────────────────────────────────────────────────────────────────
# Step 4: Full Canonical HFS World Build
# ─────────────────────────────────────────────────────────────────────────────

HFS_BUILD_LOG="${ARTIFACT_DIR}/${TAG}.03_build_world_hfs.log"
run_step build_world_hfs \
  "${HFS_BUILD_LOG}" \
  "${TEMP_CLONE_DIR}" \
  bash tools/build_world.sh \
    --target hfs \
    --force \
    --artifacts "${CLONE_BUILD_ARTIFACTS}" \
    --tag "${TAG}-build-hfs" || exit 1

# ─────────────────────────────────────────────────────────────────────────────
# Step 5: Canonical HFS Alpha Release Gate
# ─────────────────────────────────────────────────────────────────────────────

HFS_GATE_LOG="${ARTIFACT_DIR}/${TAG}.04_alpha_release_gate.log"
HFS_GATE_ARGS=(
  bash tools/alpha_release_gate.sh
  --tag "${TAG}-gate-hfs"
  --artifacts "${CLONE_RELEASE_ARTIFACTS}"
  --root-kind hfs
  --timeout "${BOOT_TIMEOUT}"
)
if [[ -n "${PORT}" ]]; then
  HFS_GATE_ARGS+=(--port "${PORT}")
fi

run_step alpha_release_gate_hfs \
  "${HFS_GATE_LOG}" \
  "${TEMP_CLONE_DIR}" \
  "${HFS_GATE_ARGS[@]}" || exit 1

# Preserve HFS gate summary in parent artifacts directory if available
if [[ -f "${CLONE_RELEASE_ARTIFACTS}/${TAG}-gate-hfs.summary.md" ]]; then
  cp -f "${CLONE_RELEASE_ARTIFACTS}/${TAG}-gate-hfs.summary.md" "${ARTIFACT_DIR}/${TAG}-gate-hfs.summary.md"
  EXTRA_ARTIFACTS+=("${ARTIFACT_DIR}/${TAG}-gate-hfs.summary.md")
fi

# ─────────────────────────────────────────────────────────────────────────────
# Step 6: Full Canonical ZFS World Build (Clean Installer Bootstrap Mode)
# ─────────────────────────────────────────────────────────────────────────────

ZFS_BUILD_LOG="${ARTIFACT_DIR}/${TAG}.05_build_world_zfs.log"
run_step build_world_zfs \
  "${ZFS_BUILD_LOG}" \
  "${TEMP_CLONE_DIR}" \
  bash tools/build_world.sh \
    --target zfs \
    --force \
    --zfs-builder-mode installer \
    --enable-qemu \
    --artifacts "${CLONE_BUILD_ARTIFACTS}" \
    --tag "${TAG}-build-zfs" || exit 1

# ─────────────────────────────────────────────────────────────────────────────
# Step 7: Preserve Generated ZFS Root as Immutable Seed
# ─────────────────────────────────────────────────────────────────────────────

INITIAL_ZFS_IMAGE="${TEMP_CLONE_DIR}/images/qemu/panthera-zfs-root.img"
SEED_IMAGE="${TEMP_CLONE_DIR}/images/qemu/panthera-zfs-root-seed.img"
PRESERVE_SEED_LOG="${ARTIFACT_DIR}/${TAG}.06_preserve_seed.log"

preserve_seed_action() {
  if [[ ! -f "${INITIAL_ZFS_IMAGE}" ]]; then
    echo "Error: Generated ZFS root image not found at: ${INITIAL_ZFS_IMAGE}" >&2
    return 1
  fi
  echo "Copying ${INITIAL_ZFS_IMAGE} to seed image: ${SEED_IMAGE}"
  cp -f "${INITIAL_ZFS_IMAGE}" "${SEED_IMAGE}"
  chmod 444 "${SEED_IMAGE}"
  local calculated_hash
  calculated_hash="$(compute_sha256 "${SEED_IMAGE}")"
  if [[ -z "${calculated_hash}" ]]; then
    echo "Error: Failed to compute SHA-256 for ZFS seed image" >&2
    return 1
  fi
  echo "${calculated_hash}" > "${SEED_IMAGE}.sha256"
  echo "ZFS Seed SHA-256: ${calculated_hash}"
}

run_step preserve_zfs_seed \
  "${PRESERVE_SEED_LOG}" \
  "${TEMP_CLONE_DIR}" \
  preserve_seed_action || exit 1

if [[ -f "${SEED_IMAGE}.sha256" ]]; then
  SEED_SHA256="$(cat "${SEED_IMAGE}.sha256" | awk '{print $1}')"
else
  SEED_SHA256="$(compute_sha256 "${SEED_IMAGE}")"
fi
# ─────────────────────────────────────────────────────────────────────────────
# Step 8: Canonical ZFS Release Gate (Distinct Seed / Control / Output Paths)
# ─────────────────────────────────────────────────────────────────────────────

GATE_CONTROL_IMAGE="${TEMP_CLONE_DIR}/images/qemu/panthera-zfs-gate-control.img"
GATE_ROOT_IMAGE="${TEMP_CLONE_DIR}/images/qemu/panthera-zfs-gate-root.img"
ZFS_GATE_LOG="${ARTIFACT_DIR}/${TAG}.07_zfs_release_gate.log"

ZFS_GATE_ARGS=(
  bash tools/zfs_release_gate.sh
  --tag "${TAG}-gate-zfs"
  --artifacts "${CLONE_RELEASE_ARTIFACTS}"
  --source-root-disk "${SEED_IMAGE}"
  --installer-disk "${GATE_CONTROL_IMAGE}"
  --root-disk "${GATE_ROOT_IMAGE}"
  --timeout "${BOOT_TIMEOUT}"
  --remote-timeout "${REMOTE_TIMEOUT}"
)
if [[ -n "${PORT}" ]]; then
  ZFS_GATE_ARGS+=(--port "${PORT}")
fi

run_step zfs_release_gate \
  "${ZFS_GATE_LOG}" \
  "${TEMP_CLONE_DIR}" \
  "${ZFS_GATE_ARGS[@]}" || exit 1

# Preserve ZFS gate summary in parent artifacts directory if available
if [[ -f "${CLONE_RELEASE_ARTIFACTS}/${TAG}-gate-zfs.summary.md" ]]; then
  cp -f "${CLONE_RELEASE_ARTIFACTS}/${TAG}-gate-zfs.summary.md" "${ARTIFACT_DIR}/${TAG}-gate-zfs.summary.md"
  EXTRA_ARTIFACTS+=("${ARTIFACT_DIR}/${TAG}-gate-zfs.summary.md")
fi

# ─────────────────────────────────────────────────────────────────────────────
# Step 9: Verify Immutable Seed Integrity
# ─────────────────────────────────────────────────────────────────────────────

VERIFY_SEED_LOG="${ARTIFACT_DIR}/${TAG}.08_verify_seed.log"

verify_seed_action() {
  local expected_hash="$1"
  local actual_hash
  actual_hash="$(compute_sha256 "${SEED_IMAGE}")"
  echo "Expected Baseline Seed SHA-256: ${expected_hash}"
  echo "Post-Gate Actual Seed SHA-256:  ${actual_hash}"
  if [[ -z "${actual_hash}" || "${actual_hash}" != "${expected_hash}" ]]; then
    echo "Error: Immutable seed image was modified or unreadable during ZFS release gate!" >&2
    return 1
  fi
  echo "Seed integrity verified: SHA-256 matches baseline."
}

run_step verify_seed_integrity \
  "${VERIFY_SEED_LOG}" \
  "${TEMP_CLONE_DIR}" \
  verify_seed_action "${SEED_SHA256}" || exit 1

FINAL_SEED_SHA256="$(compute_sha256 "${SEED_IMAGE}")"

# ─────────────────────────────────────────────────────────────────────────────
# Final Summary Generation & Clean Exit
# ─────────────────────────────────────────────────────────────────────────────

OVERALL_RESULT="PASS"
END_TIME_EPOCH="$(date +%s)"
write_summary "PASS"

echo "================================================================================"
echo "Panthera Clean World Build Proof Complete: PASS"
echo "Summary written to: ${SUMMARY_PATH}"
echo "Latest summary at:  ${LATEST_SUMMARY_PATH}"
echo "================================================================================"
cat "${SUMMARY_PATH}"

exit 0
