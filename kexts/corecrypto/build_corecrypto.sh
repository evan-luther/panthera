#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SRC_DIR="${SCRIPT_DIR}/src"

exec bash "${PANTHERA_ROOT}/kexts/OpenIOKit/build_kext.sh" \
	corecrypto \
	"${SRC_DIR}" \
	--module-start panthera_corecrypto_start \
	--module-stop panthera_corecrypto_stop \
	--prepend-include "${SRC_DIR}/include" \
	--extra-include "${SRC_DIR}/algorithms" \
	panthera_corecrypto.c \
	algorithms/md5.c \
	algorithms/md5_impl.cpp \
	algorithms/sha1.c \
	algorithms/sha1_impl.cc \
	algorithms/sha512.cc \
	algorithms/aes128.c \
	algorithms/aes_cbc.c \
	algorithms/aes_ecb.c \
	algorithms/pdcrypto_digest_final.c
