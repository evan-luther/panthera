#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# shellcheck source=/dev/null
source "${PANTHERA_ROOT}/rootfs/scripts/manifest_lib.sh"

fail=0

echo "# Panthera diagnostic staging audit"
echo

debug_destinations="$(
  env -u PANTHERA_STAGE_POLL_LISTEN_PROBE \
      -u PANTHERA_STAGE_POLL_HOSTFWD_PROBE \
      -u PANTHERA_STAGE_PPOLL_HOSTFWD_PROBE \
      -u PANTHERA_STAGE_PPOLL_EMPTY_HOSTFWD_PROBE \
      bash -c '
        source "$1"
        manifest_selected_destinations "$2"
      ' _ \
      "${PANTHERA_ROOT}/rootfs/scripts/manifest_lib.sh" \
      "${PANTHERA_ROOT}/manifests/debug.system"
)"

if printf '%s\n' "${debug_destinations}" | grep -Eq 'poll-listen-probe|poll-hostfwd-probe|ppoll-hostfwd-probe|ppoll-empty-hostfwd-probe'; then
  echo "- FAIL default debug manifest selects poll/ppoll host-forward probes"
  fail=1
else
  echo "- PASS poll/ppoll host-forward probes are environment-gated"
fi

if grep -Eq 'PANTHERA_DYLD_(TRACE|PROGRESS_TRACE|TRACE_BINDS|RESOLVE_TRACE|PHASE_TRACE)' \
  "${PANTHERA_ROOT}/rootfs/System/Library/LaunchDaemons/com.panthera.sshd.plist"; then
  echo "- FAIL sshd launch daemon stages dyld tracing by default"
  fail=1
else
  echo "- PASS sshd launch daemon has no default dyld tracing"
fi

if grep -REq 'PANTHERA_[A-Z0-9_]*TRACE' "${PANTHERA_ROOT}/rootfs/System/Library/LaunchDaemons"; then
  echo "- FAIL launch daemon plist stages Panthera trace env by default"
  grep -REn 'PANTHERA_[A-Z0-9_]*TRACE' "${PANTHERA_ROOT}/rootfs/System/Library/LaunchDaemons" | sed 's/^/  /'
  fail=1
else
  echo "- PASS launch daemon plists do not stage Panthera trace env by default"
fi

echo
if [[ "${fail}" == "0" ]]; then
  echo "Result: PASS"
else
  echo "Result: FAIL"
fi

exit "${fail}"
