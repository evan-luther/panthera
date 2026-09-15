#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

SRCDIR="${PANTHERA_ROOT}/src/awk/src"
OUT_ROOT="${PANTHERA_ROOT}/userland/awk"
BIN_DIR="${OUT_ROOT}/bin"

CC="$(xcrun -find clang)"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
TARGET="x86_64-apple-darwin23.0"
MINVER="14.0"

mkdir -p "${BIN_DIR}"

if [[ "${PANTHERA_FORCE_REBUILD:-0}" != "1" \
   && -x "${BIN_DIR}/awk" ]]; then
  echo "awk already staged"
  exit 0
fi

SRCS=(
  awkgram.tab.c
  b.c
  lex.c
  lib.c
  main.c
  parse.c
  proctab.c
  run.c
  tran.c
)

WORKDIR="$(mktemp -d /tmp/panthera-awk-build.XXXXXX)"
trap 'rm -rf "${WORKDIR}"' EXIT

OBJS=()
for src in "${SRCS[@]}"; do
  obj="${WORKDIR}/${src%.c}.o"
  "${CC}" \
    -target "${TARGET}" \
    -mmacosx-version-min="${MINVER}" \
    -isysroot "${SDKROOT}" \
    -O2 \
    -I"${SRCDIR}" \
    -c "${SRCDIR}/${src}" \
    -o "${obj}"
  OBJS+=("${obj}")
done

"${CC}" \
  -target "${TARGET}" \
  -mmacosx-version-min="${MINVER}" \
  -isysroot "${SDKROOT}" \
  -lSystem \
  "${OBJS[@]}" \
  -o "${BIN_DIR}/awk"

echo "Built awk: ${BIN_DIR}/awk"

bash "${PANTHERA_ROOT}/tools/audit_package.sh" "${BIN_DIR}/awk"
