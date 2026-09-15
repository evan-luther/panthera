#!/bin/bash
# verify_exports.sh — Check all Panthera binaries for unresolved and duplicate symbols
#
# Run after every library rebuild, before staging to root image.
# Exit code: 0 = clean, 1 = problems found

set -euo pipefail

VERIFY_MODE="full"
if [ "${1:-}" = "--runtime-only" ]; then
    VERIFY_MODE="runtime"
    shift
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SYSROOT="${1:-${PANTHERA_ROOT}/userland/libsystem/build/sysroot}"
ROOT_IMG_MOUNT="${2:-}"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

ERRORS=0

echo "========================================"
echo "Panthera Link Verification"
echo "========================================"
echo "Sysroot: ${SYSROOT}"
echo ""

collect_reexported_system_dylibs() {
    local umbrella="$1"
    local reexport path

    if [ ! -f "$umbrella" ]; then
        return
    fi

    while IFS= read -r reexport; do
        [ -n "$reexport" ] || continue
        path="${SYSROOT}${reexport}"
        [ -f "$path" ] && printf '%s\n' "$path"
    done < <(otool -l "$umbrella" 2>/dev/null | awk '
        $1 == "cmd" && $2 == "LC_REEXPORT_DYLIB" { want = 1; next }
        want && $1 == "name" { print $2; want = 0 }
    ')
}

# -- Step 1: Build combined symbol table from all dylibs --

echo ">>> Building combined export table..."
EXPORT_TABLE=$(mktemp)
trap "rm -f '$EXPORT_TABLE'" EXIT

for lib in "${SYSROOT}"/usr/lib/*.dylib; do
    [ -f "$lib" ] || continue
    nm -gU "$lib" 2>/dev/null | awk '{print $NF}' >> "$EXPORT_TABLE"
done

while IFS= read -r lib; do
    [ -f "$lib" ] || continue
    nm -gU "$lib" 2>/dev/null | awk '{print $NF}' >> "$EXPORT_TABLE"
done < <(collect_reexported_system_dylibs "${SYSROOT}/usr/lib/libSystem.B.dylib")

if [ ! -s "$EXPORT_TABLE" ]; then
    for lib in "${SYSROOT}"/usr/lib/system/*.dylib "${SYSROOT}"/usr/lib/*.dylib; do
        [ -f "$lib" ] || continue
        nm -gU "$lib" 2>/dev/null | awk '{print $NF}' >> "$EXPORT_TABLE"
    done
fi

sort -u "$EXPORT_TABLE" -o "$EXPORT_TABLE"
TOTAL_EXPORTS=$(wc -l < "$EXPORT_TABLE" | tr -d ' ')
echo "  ${TOTAL_EXPORTS} unique symbols exported across all dylibs"
echo ""

# -- Step 2: Check each binary for unresolved imports --

echo ">>> Checking binaries for unresolved symbols..."

check_binary() {
    local bin="$1"
    local label="$2"
    local count=0
    local missing_list=""

    while IFS= read -r sym; do
        [ -z "$sym" ] && continue
        if ! grep -qFx "$sym" "$EXPORT_TABLE"; then
            missing_list="${missing_list}    ${sym}\n"
            count=$((count + 1))
        fi
    done < <(nm -gu "$bin" 2>/dev/null | awk '{print $NF}')

    if [ $count -gt 0 ]; then
        echo -e "  ${RED}FAIL${NC}: ${label} -- ${count} unresolved symbols:"
        echo -e "$missing_list"
        ERRORS=$((ERRORS + 1))
    else
        echo -e "  ${GREEN}OK${NC}: ${label}"
    fi
}

# Check build-tree binaries owned by the completed phase. Runtime-only checks
# must not depend on stale userland artifacts whose providers are rebuilt later.
BUILD_TREE_BINARIES=(
    "${PANTHERA_ROOT}/userland/launchd/launchd"
    "${PANTHERA_ROOT}/userland/dyld/dyld"
)
if [ "${VERIFY_MODE}" = "full" ]; then
    BUILD_TREE_BINARIES+=("${PANTHERA_ROOT}/userland/shell/zsh")
fi
for bin in "${BUILD_TREE_BINARIES[@]}"; do
    [ -f "$bin" ] || continue
    check_binary "$bin" "$(basename "$bin") (build tree)"
done

# Check staged binaries on mounted root image if provided
if [ -n "$ROOT_IMG_MOUNT" ] && [ -d "$ROOT_IMG_MOUNT" ]; then
    echo ""
    echo ">>> Checking staged binaries on root image..."
    for bin in \
        "${ROOT_IMG_MOUNT}"/sbin/launchd \
        "${ROOT_IMG_MOUNT}"/bin/zsh \
        "${ROOT_IMG_MOUNT}"/bin/sh \
        "${ROOT_IMG_MOUNT}"/usr/lib/dyld; do
        [ -f "$bin" ] || continue
        check_binary "$bin" "$(echo "$bin" | sed "s|${ROOT_IMG_MOUNT}||")"
    done
fi

# -- Step 3: Check for duplicate symbols across dylibs --

echo ""
echo ">>> Checking for duplicate symbol exports..."

DYLIBS=()
UMBRELLA="${SYSROOT}/usr/lib/libSystem.B.dylib"

filter_allowed_dupes() {
    local pair="$1"
    case "$pair" in
        libsystem_kernel.dylib:libsystem_malloc.dylib)
            grep -v '^_malloc$' || true
            ;;
        *)
            cat
            ;;
    esac
}

while IFS= read -r lib; do
    [ -f "$lib" ] && DYLIBS+=("$lib")
done < <(collect_reexported_system_dylibs "$UMBRELLA")

if [ "${#DYLIBS[@]}" -eq 0 ]; then
    for f in "${SYSROOT}"/usr/lib/system/*.dylib; do
        # Skip backup copies
        case "$f" in *.pre_*) continue ;; esac
        [ -f "$f" ] && DYLIBS+=("$f")
    done
fi

DUPE_COUNT=0

for ((i=0; i<${#DYLIBS[@]}; i++)); do
    for ((j=i+1; j<${#DYLIBS[@]}; j++)); do
        lib_a="${DYLIBS[$i]}"
        lib_b="${DYLIBS[$j]}"
        name_a=$(basename "$lib_a")
        name_b=$(basename "$lib_b")

        dupes=$(comm -12 \
            <(nm -gU "$lib_a" 2>/dev/null | awk '{print $NF}' | sort -u) \
            <(nm -gU "$lib_b" 2>/dev/null | awk '{print $NF}' | sort -u) \
            | grep -v "^_dyld_stub_binder$" \
            | grep -v "^dyld_stub_binder$" \
            || true)

        dupes=$(printf '%s\n' "$dupes" | filter_allowed_dupes "${name_a}:${name_b}")

        if [ -z "$dupes" ]; then
            count=0
        else
            count=$(echo "$dupes" | wc -l | tr -d ' ')
        fi

        if [ "$count" -gt 0 ]; then
            echo -e "  ${YELLOW}WARN${NC}: ${name_a} vs ${name_b} -- ${count} duplicate exports"
            if [ "$count" -le 10 ]; then
                echo "$dupes" | sed 's/^/    /'
            fi
            DUPE_COUNT=$((DUPE_COUNT + count))
        fi
    done
done

if [ "$DUPE_COUNT" -eq 0 ]; then
    echo -e "  ${GREEN}OK${NC}: No duplicate exports between dylibs"
else
    echo -e "  ${YELLOW}${DUPE_COUNT} total duplicate exports${NC} (potential shadow bugs)"
    ERRORS=$((ERRORS + 1))
fi

# -- Step 4: Spot-check critical symbol availability --

echo ""
echo ">>> Spot-checking critical symbols..."

CRITICAL_SYMS=(
    # malloc
    _malloc _free _calloc _realloc
    # pthread
    _pthread_create _pthread_mutex_lock _pthread_self
    # libc core
    _printf _fprintf _snprintf _fopen _fclose _fwrite _fread
    _fork _execve _wait _waitpid _exit
    _getenv _setenv _unsetenv _getopt
    _sigaction _signal _kill
    _open _close _read _write _dup2
    _socket _bind _listen _accept _connect
    _getpwnam _getpwuid _getgrnam
    # Mach
    _mach_msg _mach_port_allocate _mach_port_deallocate
    _mach_task_self_ _task_self_trap _host_self_trap
    # dispatch
    _dispatch_async_f _dispatch_sync_f _dispatch_once_f
    _dispatch_queue_create _dispatch_source_create
    # launchd-specific
    _login_tty _uuid_generate _uuid_is_null
    _bootstrap_port _environ
    _setlocale _isatty _ttyname
)

MISSING_CRITICAL=0
for sym in "${CRITICAL_SYMS[@]}"; do
    if ! grep -qFx "$sym" "$EXPORT_TABLE"; then
        echo -e "  ${RED}MISSING${NC}: $sym"
        MISSING_CRITICAL=$((MISSING_CRITICAL + 1))
    fi
done

if [ "$MISSING_CRITICAL" -eq 0 ]; then
    echo -e "  ${GREEN}OK${NC}: All ${#CRITICAL_SYMS[@]} critical symbols present"
else
    echo -e "  ${RED}${MISSING_CRITICAL} critical symbols missing${NC}"
    ERRORS=$((ERRORS + 1))
fi

# -- Summary --

echo ""
echo "========================================"
if [ "$ERRORS" -eq 0 ]; then
    echo -e "${GREEN}PASS${NC}: All checks passed"
else
    echo -e "${RED}FAIL${NC}: ${ERRORS} problem(s) found"
fi
echo "========================================"

exit $((ERRORS > 0 ? 1 : 0))
