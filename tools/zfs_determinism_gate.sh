#!/usr/bin/env bash
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ARTIFACT_DIR="${PANTHERA_ROOT}/artifacts/release"
TAG_PREFIX="zfs-determinism-$(date +%Y%m%d-%H%M%S)"
ATTEMPTS=3
TIMEOUT_SECONDS=""
REMOTE_TIMEOUT_SECONDS=""
PORT=""
SOURCE_ROOT_DISK="${PANTHERA_ZFS_SOURCE_ROOT_DISK:-${PANTHERA_ZFS_ROOT_SOURCE_DISK:-}}"
DETERMINISM_DIR="${PANTHERA_ZFS_DETERMINISM_DIR:-${PANTHERA_ROOT}/images/qemu/determinism}"

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Runs consecutive fresh ZFS root acceptance runs using tools/zfs_release_gate.sh
to verify deterministic first-boot and runtime behavior without image reuse.

Options:
  --attempts N           Number of consecutive attempts (default: ${ATTEMPTS})
  --tag PREFIX           Artifact tag prefix (default: ${TAG_PREFIX})
  --artifacts DIR        Release artifact directory (default: ${ARTIFACT_DIR})
  --source-root-disk P   Immutable source ZFS root image to clone for each attempt
                         (default: ${PANTHERA_ROOT}/images/qemu/panthera-zfs-root.img if present)
  --work-dir DIR         Directory for isolated per-attempt disk images
                         (default: ${DETERMINISM_DIR})
  --timeout SECONDS      Per-boot timeout passed to release gate
  --remote-timeout SEC   Per-SSH-command timeout passed to release gate
  --port PORT            Host SSH forward port passed to release gate
  --help                 Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --attempts)
      if [[ $# -lt 2 ]]; then
        echo "Error: --attempts requires a value" >&2
        usage >&2
        exit 2
      fi
      ATTEMPTS="$2"
      shift 2
      ;;
    --attempts=*)
      ATTEMPTS="${1#*=}"
      shift
      ;;
    --tag)
      if [[ $# -lt 2 ]]; then
        echo "Error: --tag requires a value" >&2
        usage >&2
        exit 2
      fi
      TAG_PREFIX="$2"
      shift 2
      ;;
    --tag=*)
      TAG_PREFIX="${1#*=}"
      shift
      ;;
    --artifacts)
      if [[ $# -lt 2 ]]; then
        echo "Error: --artifacts requires a value" >&2
        usage >&2
        exit 2
      fi
      ARTIFACT_DIR="$2"
      shift 2
      ;;
    --artifacts=*)
      ARTIFACT_DIR="${1#*=}"
      shift
      ;;
    --source-root-disk)
      if [[ $# -lt 2 ]]; then
        echo "Error: --source-root-disk requires a value" >&2
        usage >&2
        exit 2
      fi
      SOURCE_ROOT_DISK="$2"
      shift 2
      ;;
    --source-root-disk=*)
      SOURCE_ROOT_DISK="${1#*=}"
      shift
      ;;
    --work-dir)
      if [[ $# -lt 2 ]]; then
        echo "Error: --work-dir requires a value" >&2
        usage >&2
        exit 2
      fi
      DETERMINISM_DIR="$2"
      shift 2
      ;;
    --work-dir=*)
      DETERMINISM_DIR="${1#*=}"
      shift
      ;;
    --timeout)
      if [[ $# -lt 2 ]]; then
        echo "Error: --timeout requires a value" >&2
        usage >&2
        exit 2
      fi
      TIMEOUT_SECONDS="$2"
      shift 2
      ;;
    --timeout=*)
      TIMEOUT_SECONDS="${1#*=}"
      shift
      ;;
    --remote-timeout)
      if [[ $# -lt 2 ]]; then
        echo "Error: --remote-timeout requires a value" >&2
        usage >&2
        exit 2
      fi
      REMOTE_TIMEOUT_SECONDS="$2"
      shift 2
      ;;
    --remote-timeout=*)
      REMOTE_TIMEOUT_SECONDS="${1#*=}"
      shift
      ;;
    --port)
      if [[ $# -lt 2 ]]; then
        echo "Error: --port requires a value" >&2
        usage >&2
        exit 2
      fi
      PORT="$2"
      shift 2
      ;;
    --port=*)
      PORT="${1#*=}"
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

if ! [[ "${ATTEMPTS}" =~ ^[1-9][0-9]*$ ]]; then
  echo "Error: --attempts must be a positive integer (got: ${ATTEMPTS})" >&2
  exit 2
fi

CANONICAL_SOURCE_ROOT_DISK="${PANTHERA_ROOT}/images/qemu/panthera-zfs-root.img"

if [[ -z "${SOURCE_ROOT_DISK}" ]]; then
  if [[ -f "${CANONICAL_SOURCE_ROOT_DISK}" ]]; then
    SOURCE_ROOT_DISK="${CANONICAL_SOURCE_ROOT_DISK}"
  else
    echo "Error: --source-root-disk is required (canonical root image not found at ${CANONICAL_SOURCE_ROOT_DISK})" >&2
    exit 2
  fi
fi

if [[ "${SOURCE_ROOT_DISK}" != /* ]]; then
  SOURCE_ROOT_DISK="${PWD}/${SOURCE_ROOT_DISK}"
fi

if [[ ! -f "${SOURCE_ROOT_DISK}" ]]; then
  echo "Error: Source root disk is not a regular file: ${SOURCE_ROOT_DISK}" >&2
  exit 1
fi
if ! command -v sha256sum >/dev/null 2>&1 && \
   ! command -v shasum >/dev/null 2>&1 && \
   ! command -v openssl >/dev/null 2>&1; then
  echo "Error: No supported SHA-256 checksum tool found (requires sha256sum, shasum, or openssl)" >&2
  exit 1
fi


if [[ "${DETERMINISM_DIR}" != /* ]]; then
  DETERMINISM_DIR="${PWD}/${DETERMINISM_DIR}"
fi

mkdir -p "${ARTIFACT_DIR}"
mkdir -p "${DETERMINISM_DIR}"
cd "${PANTHERA_ROOT}"

SUMMARY_PATH="${ARTIFACT_DIR}/${TAG_PREFIX}.summary.md"
LATEST_SUMMARY_PATH="${ARTIFACT_DIR}/latest-zfs-determinism.summary.md"

EXECUTION_BEGUN=0
OVERALL_RESULT="FAIL"
SUMMARY_WRITTEN=0
EXPECTED_BASELINE_SHA256=""
FINAL_BASELINE_SHA256=""
declare -a ATTEMPT_ROWS=()
declare -a ATTEMPT_TAGS=()
declare -a ATTEMPT_RESULTS=()
declare -a ATTEMPT_SUMMARIES=()

cleanup_tmp() {
  rm -f "${SUMMARY_PATH}.tmp.$$" "${LATEST_SUMMARY_PATH}.tmp.$$"
}

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

clone_image() {
  local src="$1"
  local dst="$2"
  rm -f "${dst}"
  # Try APFS / copy-on-write clone on macOS/Darwin
  if cp -c "${src}" "${dst}" 2>/dev/null; then
    return 0
  fi
  rm -f "${dst}"
  # Try GNU reflink clone if available
  if cp --reflink=auto "${src}" "${dst}" 2>/dev/null; then
    return 0
  fi
  rm -f "${dst}"
  # Fallback to portable copy
  cp -f "${src}" "${dst}"
}

write_aggregate_summary() {
  local result="$1"
  local tmp_summary="${SUMMARY_PATH}.tmp.$$"
  local tmp_latest="${LATEST_SUMMARY_PATH}.tmp.$$"
  local total_executed=${#ATTEMPT_TAGS[@]}
  local passed_count=0
  local failed_count=0

  for res in "${ATTEMPT_RESULTS[@]}"; do
    if [[ "${res}" == "PASS" ]]; then
      ((passed_count++)) || true
    else
      ((failed_count++)) || true
    fi
  done

  {
    echo "# Panthera ZFS Determinism Gate"
    echo
    echo "Result: ${result}"
    echo
    echo "Scope: Consecutive fresh ZFS root acceptance runs (${ATTEMPTS} attempts requested, ${total_executed} executed)."
    echo
    echo "- Baseline Source Root: \`${SOURCE_ROOT_DISK}\`"
    echo "- Expected Baseline SHA-256: \`${EXPECTED_BASELINE_SHA256:-unknown}\`"
    echo "- Final Baseline SHA-256: \`${FINAL_BASELINE_SHA256:-unknown}\`"
    echo "- Determinism Work Directory: \`${DETERMINISM_DIR}\`"
    echo
    echo "## Attempts"
    echo
    echo "| Attempt | Tag | Result | Child Summary | Source Image | Control Image | Root Image |"
    echo "|---|---|---|---|---|---|---|"
    if [[ ${#ATTEMPT_ROWS[@]} -eq 0 ]]; then
      echo "| - | - | NONE | No attempts recorded | - | - | - |"
    else
      for row in "${ATTEMPT_ROWS[@]}"; do
        echo "${row}"
      done
    fi
    echo
    echo "## Summary"
    echo
    echo "- Requested attempts: ${ATTEMPTS}"
    echo "- Executed attempts: ${total_executed}"
    echo "- Passed attempts: ${passed_count}"
    echo "- Failed attempts: ${failed_count}"
    echo
    echo "## Artifacts"
    echo
    if [[ ${#ATTEMPT_SUMMARIES[@]} -eq 0 ]]; then
      echo "- None."
    else
      for summary_item in "${ATTEMPT_SUMMARIES[@]}"; do
        echo "- ${summary_item}"
      done
    fi
  } >"${tmp_summary}"

  cp -f "${tmp_summary}" "${tmp_latest}"
  mv -f "${tmp_summary}" "${SUMMARY_PATH}"
  mv -f "${tmp_latest}" "${LATEST_SUMMARY_PATH}"
  SUMMARY_WRITTEN=1
}

cleanup_on_signal() {
  local sig="$1"
  trap - INT TERM EXIT
  if [[ -n "${SOURCE_ROOT_DISK:-}" && -f "${SOURCE_ROOT_DISK}" ]]; then
    FINAL_BASELINE_SHA256="$(compute_sha256 "${SOURCE_ROOT_DISK}" 2>/dev/null || echo "${FINAL_BASELINE_SHA256:-unknown}")"
  fi
  if [[ "${EXECUTION_BEGUN}" -eq 1 && "${SUMMARY_WRITTEN}" -eq 0 ]]; then
    write_aggregate_summary "FAIL"
  fi
  cleanup_tmp
  case "${sig}" in
    INT)  exit 130 ;;
    TERM) exit 143 ;;
    *)    exit 1 ;;
  esac
}

cleanup_on_exit() {
  local exit_code=$?
  trap - INT TERM EXIT
  if [[ -n "${SOURCE_ROOT_DISK:-}" && -f "${SOURCE_ROOT_DISK}" && -z "${FINAL_BASELINE_SHA256:-}" ]]; then
    FINAL_BASELINE_SHA256="$(compute_sha256 "${SOURCE_ROOT_DISK}" 2>/dev/null || echo "${EXPECTED_BASELINE_SHA256:-unknown}")"
  fi
  if [[ "${EXECUTION_BEGUN}" -eq 1 && "${SUMMARY_WRITTEN}" -eq 0 ]]; then
    write_aggregate_summary "FAIL"
  fi
  cleanup_tmp
  exit "${exit_code}"
}

if ! EXPECTED_BASELINE_SHA256="$(compute_sha256 "${SOURCE_ROOT_DISK}")" || [[ -z "${EXPECTED_BASELINE_SHA256}" ]]; then
  echo "Error: Failed to compute initial SHA-256 for baseline source image: ${SOURCE_ROOT_DISK}" >&2
  exit 1
fi
FINAL_BASELINE_SHA256="${EXPECTED_BASELINE_SHA256}"

trap 'cleanup_on_signal INT' INT
trap 'cleanup_on_signal TERM' TERM
trap cleanup_on_exit EXIT
EXECUTION_BEGUN=1

echo "================================================================================"
echo "Starting Panthera ZFS Determinism Gate"
echo "Attempts: ${ATTEMPTS}"
echo "Tag Prefix: ${TAG_PREFIX}"
echo "Artifacts: ${ARTIFACT_DIR}"
echo "Baseline Source Root: ${SOURCE_ROOT_DISK}"
echo "Baseline SHA-256: ${EXPECTED_BASELINE_SHA256}"
echo "Determinism Work Directory: ${DETERMINISM_DIR}"
echo "================================================================================"

ALL_PASSED=1

for ((i = 1; i <= ATTEMPTS; i++)); do
  attempt_tag="${TAG_PREFIX}-attempt-${i}"
  child_summary="${ARTIFACT_DIR}/${attempt_tag}.summary.md"
  attempt_source_disk="${DETERMINISM_DIR}/${attempt_tag}-source.img"
  attempt_control_disk="${DETERMINISM_DIR}/${attempt_tag}-control.img"
  attempt_root_disk="${DETERMINISM_DIR}/${attempt_tag}-root.img"

  # Remove and replace only this attempt's isolated images
  rm -f "${attempt_source_disk}" "${attempt_control_disk}" "${attempt_root_disk}"

  echo
  echo "--------------------------------------------------------------------------------"
  echo ">>> Determinism Attempt ${i}/${ATTEMPTS} (tag: ${attempt_tag})"
  echo "    Source Image:  ${attempt_source_disk}"
  echo "    Control Image: ${attempt_control_disk}"
  echo "    Root Image:    ${attempt_root_disk}"
  echo "--------------------------------------------------------------------------------"

  # Clone immutable baseline to per-attempt source image
  if ! clone_image "${SOURCE_ROOT_DISK}" "${attempt_source_disk}"; then
    echo "Error: Failed to clone baseline source image to ${attempt_source_disk}" >&2
    FINAL_BASELINE_SHA256="$(compute_sha256 "${SOURCE_ROOT_DISK}" 2>/dev/null || echo "UNKNOWN")"
    ATTEMPT_TAGS+=("${attempt_tag}")
    ATTEMPT_RESULTS+=("FAIL")
    ATTEMPT_ROWS+=("| ${i} | \`${attempt_tag}\` | FAIL | \`${child_summary}\` | \`${attempt_source_disk}\` | \`${attempt_control_disk}\` | \`${attempt_root_disk}\` |")
    ATTEMPT_SUMMARIES+=(
      "Attempt ${i} (\`${attempt_tag}\`): \`${child_summary}\` (FAIL - source clone failed)"
      "  - Source Image: \`${attempt_source_disk}\`"
      "  - Control Image: \`${attempt_control_disk}\`"
      "  - Root Image: \`${attempt_root_disk}\`"
    )
    ALL_PASSED=0
    break
  fi

  gate_args=(
    --tag "${attempt_tag}"
    --artifacts "${ARTIFACT_DIR}"
    --only-root
    --source-root-disk "${attempt_source_disk}"
  )

  if [[ -n "${TIMEOUT_SECONDS}" ]]; then
    gate_args+=(--timeout "${TIMEOUT_SECONDS}")
  fi
  if [[ -n "${REMOTE_TIMEOUT_SECONDS}" ]]; then
    gate_args+=(--remote-timeout "${REMOTE_TIMEOUT_SECONDS}")
  fi
  if [[ -n "${PORT}" ]]; then
    gate_args+=(--port "${PORT}")
  fi

  gate_exit_code=0
  PANTHERA_ZFS_ROOT_INSTALLER_DISK="${attempt_control_disk}" \
  PANTHERA_ZFS_ROOT_DISK="${attempt_root_disk}" \
  bash "${SCRIPT_DIR}/zfs_release_gate.sh" "${gate_args[@]}" || gate_exit_code=$?

  current_baseline_sha256="$(compute_sha256 "${SOURCE_ROOT_DISK}" 2>/dev/null || echo "UNKNOWN")"
  FINAL_BASELINE_SHA256="${current_baseline_sha256}"

  baseline_intact=1
  if [[ "${current_baseline_sha256}" != "${EXPECTED_BASELINE_SHA256}" ]]; then
    echo "Error: Baseline source image mutated during attempt ${i}!" >&2
    echo "  Expected SHA-256: ${EXPECTED_BASELINE_SHA256}" >&2
    echo "  Actual SHA-256:   ${current_baseline_sha256}" >&2
    baseline_intact=0
  fi

  attempt_pass=0
  if [[ ${gate_exit_code} -eq 0 && -f "${child_summary}" && ${baseline_intact} -eq 1 ]]; then
    if grep -q "^Result: PASS" "${child_summary}"; then
      attempt_pass=1
    fi
  fi
  ATTEMPT_TAGS+=("${attempt_tag}")

  if [[ ${attempt_pass} -eq 1 ]]; then
    ATTEMPT_RESULTS+=("PASS")
    ATTEMPT_ROWS+=("| ${i} | \`${attempt_tag}\` | PASS | \`${child_summary}\` | \`${attempt_source_disk}\` | \`${attempt_control_disk}\` | \`${attempt_root_disk}\` |")
    ATTEMPT_SUMMARIES+=(
      "Attempt ${i} (\`${attempt_tag}\`): \`${child_summary}\` (PASS)"
      "  - Source Image: \`${attempt_source_disk}\`"
      "  - Control Image: \`${attempt_control_disk}\`"
      "  - Root Image: \`${attempt_root_disk}\`"
    )
    echo ">>> Attempt ${i}/${ATTEMPTS} PASSED"
  else
    ATTEMPT_RESULTS+=("FAIL")
    ATTEMPT_ROWS+=("| ${i} | \`${attempt_tag}\` | FAIL | \`${child_summary}\` | \`${attempt_source_disk}\` | \`${attempt_control_disk}\` | \`${attempt_root_disk}\` |")
    if [[ ${baseline_intact} -eq 0 ]]; then
      ATTEMPT_SUMMARIES+=(
        "Attempt ${i} (\`${attempt_tag}\`): \`${child_summary}\` (FAIL - baseline source image mutated)"
        "  - Source Image: \`${attempt_source_disk}\`"
        "  - Control Image: \`${attempt_control_disk}\`"
        "  - Root Image: \`${attempt_root_disk}\`"
        "  - Expected Baseline SHA-256: \`${EXPECTED_BASELINE_SHA256}\`"
        "  - Mutated Baseline SHA-256:  \`${current_baseline_sha256}\`"
      )
      echo ">>> Attempt ${i}/${ATTEMPTS} FAILED (baseline source image mutated)" >&2
    else
      ATTEMPT_SUMMARIES+=(
        "Attempt ${i} (\`${attempt_tag}\`): \`${child_summary}\` (FAIL)"
        "  - Source Image: \`${attempt_source_disk}\`"
        "  - Control Image: \`${attempt_control_disk}\`"
        "  - Root Image: \`${attempt_root_disk}\`"
      )
      echo ">>> Attempt ${i}/${ATTEMPTS} FAILED (exit code: ${gate_exit_code})" >&2
    fi
    ALL_PASSED=0
    break
  fi
done

if [[ "${ALL_PASSED}" -eq 1 && ${#ATTEMPT_RESULTS[@]} -eq "${ATTEMPTS}" && "${FINAL_BASELINE_SHA256}" == "${EXPECTED_BASELINE_SHA256}" ]]; then
  OVERALL_RESULT="PASS"
else
  OVERALL_RESULT="FAIL"
fi

write_aggregate_summary "${OVERALL_RESULT}"
cat "${SUMMARY_PATH}"

if [[ "${OVERALL_RESULT}" != "PASS" ]]; then
  exit 1
fi
exit 0
