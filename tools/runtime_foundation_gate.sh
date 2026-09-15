#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARTIFACT_DIR="${PANTHERA_ROOT}/artifacts/runtime"
TAG="runtime-foundation-$(date +%Y%m%d-%H%M%S)"
RUN_PANTHERA_EXTRA=1
RUN_LIBSYSTEM=1
RUN_SHARED_CACHE=1
RUN_BOOT=0
BOOT_TIMEOUT="${PANTHERA_BOOT_VERIFY_TIMEOUT:-220}"
STRICT_NETWORK=0
ALLOW_FLAT_LAUNCHD="${PANTHERA_ALLOW_FLAT_LAUNCHD:-0}"

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Runs the Panthera runtime-foundation gate:
  1. relink libpanthera_extra.dylib
  2. relink libSystem.B.dylib
  3. rebuild the dyld shared cache
  4. verify exports
  5. optionally run the Phase 5 boot gate

Options:
  --tag NAME                 Artifact tag (default: ${TAG})
  --artifacts DIR            Artifact directory (default: ${ARTIFACT_DIR})
  --skip-panthera-extra      Do not relink libpanthera_extra.dylib
  --skip-libsystem           Do not relink libSystem.B.dylib
  --skip-shared-cache        Do not rebuild the dyld shared cache
  --verify-only              Only run verify_exports.sh
  --boot                     Run tools/boot_verify.sh after export verification
  --boot-timeout SECONDS     Timeout passed to boot_verify.sh (default: ${BOOT_TIMEOUT})
  --strict-network           Pass --strict-network to boot_verify.sh
  --allow-flat-launchd       Do not fail if launchd is linked flat namespace
  --help                     Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --tag)
      TAG="$2"
      shift 2
      ;;
    --artifacts)
      ARTIFACT_DIR="$2"
      shift 2
      ;;
    --skip-panthera-extra)
      RUN_PANTHERA_EXTRA=0
      shift
      ;;
    --skip-libsystem)
      RUN_LIBSYSTEM=0
      shift
      ;;
    --skip-shared-cache)
      RUN_SHARED_CACHE=0
      shift
      ;;
    --verify-only)
      RUN_PANTHERA_EXTRA=0
      RUN_LIBSYSTEM=0
      RUN_SHARED_CACHE=0
      shift
      ;;
    --boot)
      RUN_BOOT=1
      shift
      ;;
    --boot-timeout)
      BOOT_TIMEOUT="$2"
      shift 2
      ;;
    --strict-network)
      STRICT_NETWORK=1
      shift
      ;;
    --allow-flat-launchd)
      ALLOW_FLAT_LAUNCHD=1
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

mkdir -p "${ARTIFACT_DIR}"
cd "${PANTHERA_ROOT}"

SUMMARY_PATH="${ARTIFACT_DIR}/${TAG}.summary.md"
VERIFY_LOG="${ARTIFACT_DIR}/${TAG}.verify_exports.log"
LAUNCHD_LINKAGE_LOG="${ARTIFACT_DIR}/${TAG}.launchd_linkage.log"
RUNTIME_DEBT_AUDIT="${ARTIFACT_DIR}/${TAG}.runtime_debt.md"

run_step() {
  local name="$1"
  shift
  local log_path="${ARTIFACT_DIR}/${TAG}.${name}.log"

  echo ">>> ${name}: $*"
  "$@" >"${log_path}" 2>&1
  echo "    log: ${log_path}"
}

if [[ "${RUN_PANTHERA_EXTRA}" == "1" ]]; then
  run_step relink_libpanthera_extra bash userland/libsystem/build/relink_libpanthera_extra.sh
fi

if [[ "${RUN_LIBSYSTEM}" == "1" ]]; then
  run_step relink_libSystem bash userland/libsystem/build/relink_libSystem.sh
fi

if [[ "${RUN_SHARED_CACHE}" == "1" ]]; then
  run_step build_shared_cache bash tools/build_shared_cache.sh
fi

echo ">>> verify_exports"
bash userland/libsystem/verify_exports.sh >"${VERIFY_LOG}" 2>&1
echo "    log: ${VERIFY_LOG}"

echo ">>> launchd_linkage"
if [[ ! -f userland/launchd/launchd ]]; then
  echo "missing userland/launchd/launchd" >"${LAUNCHD_LINKAGE_LOG}"
  cat "${LAUNCHD_LINKAGE_LOG}" >&2
  exit 1
fi
otool -hv userland/launchd/launchd >"${LAUNCHD_LINKAGE_LOG}" 2>&1
if ! grep -q "TWOLEVEL" "${LAUNCHD_LINKAGE_LOG}"; then
  if [[ "${ALLOW_FLAT_LAUNCHD}" != "1" ]]; then
    echo "launchd is not linked two-level; pass --allow-flat-launchd only for legacy debugging" >>"${LAUNCHD_LINKAGE_LOG}"
    cat "${LAUNCHD_LINKAGE_LOG}" >&2
    exit 1
  fi
fi
echo "    log: ${LAUNCHD_LINKAGE_LOG}"

echo ">>> runtime_debt_audit"
tools/audit_runtime_debt.sh --output "${RUNTIME_DEBT_AUDIT}"
echo "    log: ${RUNTIME_DEBT_AUDIT}"

BOOT_SUMMARY=""
if [[ "${RUN_BOOT}" == "1" ]]; then
  boot_args=(
    --timeout "${BOOT_TIMEOUT}"
    --rebuild-rootfs
    --tag "${TAG}-boot"
  )
  if [[ "${STRICT_NETWORK}" == "1" ]]; then
    boot_args+=(--strict-network)
  fi
  run_step boot_verify tools/boot_verify.sh "${boot_args[@]}"
  BOOT_SUMMARY="artifacts/boot/${TAG}-boot.summary.md"
fi

{
  echo "# Panthera Runtime Foundation Gate"
  echo
  echo "Result: PASS"
  echo
  echo "## Steps"
  echo
  if [[ "${RUN_PANTHERA_EXTRA}" == "1" ]]; then
    echo "- relink_libpanthera_extra: PASS"
  else
    echo "- relink_libpanthera_extra: SKIPPED"
  fi
  if [[ "${RUN_LIBSYSTEM}" == "1" ]]; then
    echo "- relink_libSystem: PASS"
  else
    echo "- relink_libSystem: SKIPPED"
  fi
  if [[ "${RUN_SHARED_CACHE}" == "1" ]]; then
    echo "- build_shared_cache: PASS"
  else
    echo "- build_shared_cache: SKIPPED"
  fi
  echo "- verify_exports: PASS"
  if grep -q "TWOLEVEL" "${LAUNCHD_LINKAGE_LOG}"; then
    echo "- launchd_linkage: PASS (two-level namespace)"
  else
    echo "- launchd_linkage: PASS (flat namespace explicitly allowed)"
  fi
  echo "- runtime_debt_audit: PASS"
  if [[ "${RUN_BOOT}" == "1" ]]; then
    echo "- boot_verify: PASS"
  fi
  echo
  echo "## Artifacts"
  echo
  echo "- Export verification: \`${VERIFY_LOG}\`"
  echo "- launchd linkage: \`${LAUNCHD_LINKAGE_LOG}\`"
  echo "- Runtime debt audit: \`${RUNTIME_DEBT_AUDIT}\`"
  if [[ -n "${BOOT_SUMMARY}" ]]; then
    echo "- Boot summary: \`${BOOT_SUMMARY}\`"
  fi
} >"${SUMMARY_PATH}"

cp -f "${SUMMARY_PATH}" "${ARTIFACT_DIR}/latest.summary.md"
cp -f "${VERIFY_LOG}" "${ARTIFACT_DIR}/latest.verify_exports.log"

echo "Runtime foundation artifact: ${SUMMARY_PATH}"
cat "${SUMMARY_PATH}"
