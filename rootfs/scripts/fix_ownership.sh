#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "Usage: $(basename "$0") IMAGE_PATH SHARED_CACHE_STAGED FIXER_SCRIPT" >&2
  exit 2
fi

image_path="$1"
shared_cache_staged="$2"
fixer_script="$3"
python3_bin="$(command -v python3 || true)"

if [[ "${shared_cache_staged}" == "1" && -n "${python3_bin}" && -f "${fixer_script}" ]]; then
  "${python3_bin}" "${fixer_script}" --quiet "${image_path}"
fi

if [[ -n "${python3_bin}" && -f "${fixer_script}" ]]; then
  "${python3_bin}" "${fixer_script}" --quiet --no-restricted --mode 755 \
    --target-name empty "${image_path}"

  "${python3_bin}" "${fixer_script}" --quiet --no-restricted --mode 600 \
    --target-name ssh_host_rsa_key \
    --target-name ssh_host_ed25519_key \
    "${image_path}" >/dev/null 2>&1 || true

  "${python3_bin}" "${fixer_script}" --quiet --no-restricted --mode 644 \
    --target-name ssh_host_rsa_key.pub \
    --target-name ssh_host_ed25519_key.pub \
    --target-name sshd_config \
    "${image_path}" >/dev/null 2>&1 || true
fi
