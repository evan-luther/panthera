#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
MONITOR_PATH="${PANTHERA_QEMU_MONITOR_PATH:-${PANTHERA_ROOT}/images/qemu/monitor.sock}"

rm -f "${MONITOR_PATH}"

export PANTHERA_NOGRAPHIC=1
export PANTHERA_QEMU_MONITOR_PATH="${MONITOR_PATH}"

exec "${SCRIPT_DIR}/run_phase2_qemu.sh" "$@"
