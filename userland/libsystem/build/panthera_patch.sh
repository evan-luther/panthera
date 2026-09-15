#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
SYSROOT="${SCRIPT_DIR}/sysroot"
STUBS_FILE="${SCRIPT_DIR}/obj/panthera_extra_stubs.c"
BRIDGE_FILE="${SCRIPT_DIR}/obj/panthera_extra_bridge.c"
ALIASES_FILE="${SCRIPT_DIR}/obj/panthera_extra_aliases.s"
STRIP_FILE="${SCRIPT_DIR}/libpanthera_extra_strip_symbols.txt"
RELINK_SCRIPT="${SCRIPT_DIR}/relink_libpanthera_extra.sh"
VERIFY_SCRIPT="${PANTHERA_ROOT}/userland/libsystem/verify_exports.sh"

DRY_RUN=0

usage() {
    cat <<'EOF'
Usage:
  ./panthera_patch.sh stub _symbol [body]
  ./panthera_patch.sh impl /path/to/implementation.c
  ./panthera_patch.sh alias _new_name _existing_name
  ./panthera_patch.sh unix2003 _function_name
  ./panthera_patch.sh verify
  ./panthera_patch.sh batch
  ./panthera_patch.sh --dry-run <command> ...
EOF
}

normalize_symbol() {
    local sym="$1"
    if [[ "${sym}" != _* ]]; then
        sym="_${sym}"
    fi
    printf '%s\n' "${sym}"
}

symbol_to_c_ident() {
    local sym
    sym="$(normalize_symbol "$1")"
    printf '%s\n' "${sym#_}"
}

symbol_in_strip_list() {
    local sym
    sym="$(normalize_symbol "$1")"
    grep -qFx "${sym}" "${STRIP_FILE}"
}

find_symbol_hits() {
    local sym
    sym="$(normalize_symbol "$1")"
    local dylib
    for dylib in "${SYSROOT}"/usr/lib/system/*.dylib "${SYSROOT}"/usr/lib/*.dylib; do
        [ -f "${dylib}" ] || continue
        if nm -gU "${dylib}" 2>/dev/null | awk '{print $NF}' | grep -qFx "${sym}"; then
            printf '%s\t%s\n' "${sym}" "${dylib}"
        fi
    done
}

confirm_conflict() {
    local sym hits reply
    sym="$(normalize_symbol "$1")"
    hits="$(find_symbol_hits "${sym}" || true)"
    if [[ -z "${hits}" ]]; then
        return 0
    fi

    printf 'warning: %s already exists in:\n%s\n' "${sym}" "${hits}" >&2
    if [[ "${DRY_RUN}" -eq 1 ]]; then
        return 0
    fi

    printf 'continue anyway? [y/N] ' >&2
    read -r reply
    [[ "${reply}" == "y" || "${reply}" == "Y" ]]
}

append_stub() {
    local sym ident body
    sym="$(normalize_symbol "$1")"
    ident="$(symbol_to_c_ident "${sym}")"
    body="${2:-return 0;}"

    if symbol_in_strip_list "${sym}"; then
        echo "refusing to add ${sym}: symbol is in ${STRIP_FILE}" >&2
        exit 1
    fi
    confirm_conflict "${sym}" || exit 1

    cat <<EOF
append stub ${sym} -> ${STUBS_FILE}
EOF
    if [[ "${DRY_RUN}" -eq 1 ]]; then
        return 0
    fi

    cat >> "${STUBS_FILE}" <<EOF

/* panthera_patch.sh stub: ${sym} */
int ${ident}(void) {
    ${body}
}
EOF
}

append_impl() {
    local impl_path
    impl_path="$1"

    if [[ ! -f "${impl_path}" ]]; then
        echo "missing implementation file: ${impl_path}" >&2
        exit 1
    fi

    cat <<EOF
append implementation ${impl_path} -> ${BRIDGE_FILE}
EOF
    if [[ "${DRY_RUN}" -eq 1 ]]; then
        return 0
    fi

    {
        printf '\n/* panthera_patch.sh impl: %s */\n' "${impl_path}"
        cat "${impl_path}"
        printf '\n'
    } >> "${BRIDGE_FILE}"
}

append_alias() {
    local new_sym existing_sym
    new_sym="$(normalize_symbol "$1")"
    existing_sym="$(normalize_symbol "$2")"

    if symbol_in_strip_list "${new_sym}"; then
        echo "refusing to add ${new_sym}: symbol is in ${STRIP_FILE}" >&2
        exit 1
    fi
    confirm_conflict "${new_sym}" || exit 1

    cat <<EOF
append alias ${new_sym} -> ${existing_sym} -> ${ALIASES_FILE}
EOF
    if [[ "${DRY_RUN}" -eq 1 ]]; then
        return 0
    fi

    cat >> "${ALIASES_FILE}" <<EOF

/* panthera_patch.sh alias: ${new_sym} -> ${existing_sym} */
.globl ${new_sym}
${new_sym}:
    jmp ${existing_sym}
EOF
}

append_unix2003() {
    local base_sym unix_sym
    base_sym="$(normalize_symbol "$1")"
    unix_sym="${base_sym}\$UNIX2003"

    if symbol_in_strip_list "${unix_sym}"; then
        echo "refusing to add ${unix_sym}: symbol is in ${STRIP_FILE}" >&2
        exit 1
    fi
    confirm_conflict "${unix_sym}" || exit 1

    cat <<EOF
append unix2003 alias ${unix_sym} -> ${base_sym} -> ${ALIASES_FILE}
EOF
    if [[ "${DRY_RUN}" -eq 1 ]]; then
        return 0
    fi

    cat >> "${ALIASES_FILE}" <<EOF

/* panthera_patch.sh unix2003 alias: ${unix_sym} -> ${base_sym} */
.globl ${unix_sym}
${unix_sym}:
    jmp ${base_sym}
EOF
}

run_verify() {
    if [[ "${DRY_RUN}" -eq 1 ]]; then
        echo "would run ${VERIFY_SCRIPT}"
        return 0
    fi
    bash "${VERIFY_SCRIPT}"
}

rebuild_and_report() {
    local changed_desc="$1"
    local verify_status export_count

    if [[ "${DRY_RUN}" -eq 1 ]]; then
        echo "would run ${RELINK_SCRIPT}"
        echo "would run ${VERIFY_SCRIPT}"
        return 0
    fi

    bash "${RELINK_SCRIPT}"
    if bash "${VERIFY_SCRIPT}"; then
        verify_status="PASS"
    else
        verify_status="FAIL"
    fi

    export_count="$(nm -gU "${SYSROOT}/usr/lib/system/libpanthera_extra.dylib" | awk '{print $NF}' | sed '/^$/d' | sort -u | wc -l | tr -d ' ')"
    printf '%s. libpanthera_extra.dylib now exports %s symbols. verify_exports: %s.\n' \
        "${changed_desc}" "${export_count}" "${verify_status}"
}

run_batch() {
    local line cmd
    local changed=0

    while IFS= read -r line; do
        [[ -n "${line}" ]] || continue
        [[ "${line}" =~ ^[[:space:]]*# ]] && continue
        eval "set -- ${line}"
        cmd="${1:-}"
        shift || true
        case "${cmd}" in
            stub) append_stub "$@" ;;
            impl) append_impl "$@" ;;
            alias) append_alias "$@" ;;
            unix2003) append_unix2003 "$@" ;;
            verify) run_verify ;;
            *) echo "unknown batch command: ${cmd}" >&2; exit 1 ;;
        esac
        changed=1
    done

    if [[ "${changed}" -eq 1 ]]; then
        rebuild_and_report "Applied batch patch set"
    fi
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dry-run)
            DRY_RUN=1
            shift
            ;;
        *)
            break
            ;;
    esac
done

cmd="${1:-}"
case "${cmd}" in
    stub)
        [[ $# -ge 2 ]] || { usage >&2; exit 1; }
        shift
        append_stub "$@"
        rebuild_and_report "Added $(normalize_symbol "$1") to ${STUBS_FILE}"
        ;;
    impl)
        [[ $# -eq 2 ]] || { usage >&2; exit 1; }
        shift
        append_impl "$1"
        rebuild_and_report "Added implementation from $1 to ${BRIDGE_FILE}"
        ;;
    alias)
        [[ $# -eq 3 ]] || { usage >&2; exit 1; }
        shift
        append_alias "$1" "$2"
        rebuild_and_report "Added $(normalize_symbol "$1") alias to ${ALIASES_FILE}"
        ;;
    unix2003)
        [[ $# -eq 2 ]] || { usage >&2; exit 1; }
        shift
        append_unix2003 "$1"
        rebuild_and_report "Added $(normalize_symbol "$1")\$UNIX2003 alias to ${ALIASES_FILE}"
        ;;
    verify)
        run_verify
        ;;
    batch)
        run_batch
        ;;
    *)
        usage >&2
        exit 1
        ;;
esac
