#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OPENZFS_SRC="$("${ROOT}/tools/prepare_openzfs_source.sh")"
MACOS_MAKEFILE="${OPENZFS_SRC}/module/os/macos/Makefile.am"

extract_sources() {
    local var="$1"
    awk -v var="${var}" '
        $0 ~ "^" var "[[:space:]]*=" {
            in_var = 1
            sub(/^[^=]*=[[:space:]]*/, "")
        }
        in_var {
            line = $0
            cont = (line ~ /\\[[:space:]]*$/)
            gsub(/\\/, "", line)
            gsub(/%D%\/\.\.\/\.\.\//, "module/", line)
            gsub(/%D%\//, "module/os/macos/", line)
            n = split(line, fields, /[[:space:]]+/)
            for (i = 1; i <= n; i++) {
                if (fields[i] ~ /\.(c|cc|cpp|S|s)$/) {
                    print fields[i]
                }
            }
            if (!cont) {
                exit
            }
        }
    ' "${MACOS_MAKEFILE}"
}

extract_x86_64_asm_sources() {
    awk '
        /^if TARGET_CPU_X86_64[[:space:]]*$/ {
            in_x86 = 1
            next
        }
        in_x86 && /^else[[:space:]]*$/ {
            exit
        }
        in_x86 && /^(zfs_ASM_SOURCES_C|zfs_ASM_SOURCES_AS)[[:space:]]*=/ {
            in_var = 1
            sub(/^[^=]*=[[:space:]]*/, "")
        }
        in_x86 && in_var {
            line = $0
            cont = (line ~ /\\[[:space:]]*$/)
            gsub(/\\/, "", line)
            gsub(/%D%\/\.\.\/\.\.\//, "module/", line)
            gsub(/%D%\//, "module/os/macos/", line)
            n = split(line, fields, /[[:space:]]+/)
            for (i = 1; i <= n; i++) {
                if (fields[i] ~ /\.(c|cc|cpp|S|s)$/) {
                    print fields[i]
                }
            }
            if (!cont) {
                in_var = 0
            }
        }
    ' "${MACOS_MAKEFILE}"
}

mapfile -t ZFS_SOURCES < <(
    {
    for var in zfs_common_SRCS zfs_os_SRCS zcommon_SRCS icp_SRCS lua_SRCS zstd_SRCS; do
        extract_sources "${var}"
    done
    extract_x86_64_asm_sources
    } |
    awk '$0 != "module/avl/avl.c" && !seen[$0]++'
)
ZFS_SOURCES+=("../../../kexts/zfs/compat/panthera_zfs_malloc.c")

if [[ ${#ZFS_SOURCES[@]} -eq 0 ]]; then
    echo "ERROR: no OpenZFS sources were extracted from ${MACOS_MAKEFILE}" >&2
    exit 1
fi

EXTRA_DEFINES=()
if [[ "${PANTHERA_OPENZFS_VNOPS_PHASE_TRACE:-0}" == "1" ||
    "${PANTHERA_OPENZFS_VNOPS_DETAIL_TRACE:-0}" == "1" ]]; then
    EXTRA_DEFINES+=(--define "PANTHERA_ZFS_VNOPS_PHASE_TRACE=1")
fi
if [[ "${PANTHERA_OPENZFS_VNOPS_DETAIL_TRACE:-0}" == "1" ]]; then
    EXTRA_DEFINES+=(--define "PANTHERA_ZFS_VNOPS_DETAIL_TRACE=1")
fi

"${ROOT}/kexts/OpenIOKit/build_kext.sh" \
    zfs \
    "${OPENZFS_SRC}" \
    --no-module-info \
    --no-link-fallback \
    --unexported-symbol "_calloc" \
    --unexported-symbol "_free" \
    --unexported-symbol "_malloc" \
    --prepend-include "${SCRIPT_DIR}/compat" \
    --prepend-include "${OPENZFS_SRC}/include/os/macos/spl" \
    --prepend-include "${OPENZFS_SRC}/include/os/macos/zfs" \
    --prepend-include "${OPENZFS_SRC}/module/icp/include" \
    --prepend-include "${OPENZFS_SRC}/include" \
    --extra-include "${OPENZFS_SRC}/module/os/macos/zfs" \
    --extra-include "${OPENZFS_SRC}/module/lua" \
    --extra-include "${OPENZFS_SRC}/module/zstd/include" \
    --extra-include "${OPENZFS_SRC}/module/zstd/lib" \
    --define "__KERNEL__" \
    --define "_KERNEL" \
    --define "DRIVER_PRIVATE" \
    --define "NAMEDSTREAMS=1" \
    --define "__DARWIN_64_BIT_INO_T=1" \
    --define "NeXT" \
    "${EXTRA_DEFINES[@]}" \
    --force-include "${SCRIPT_DIR}/compat/zfs_config.h" \
    --force-include "${SCRIPT_DIR}/compat/panthera_spl_compat.h" \
    "${ZFS_SOURCES[@]}"
