#!/bin/bash
#
# build_coreutils.sh — Cross-compile Apple coreutils for Panthera x86_64
#
set -e
set -o pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUTDIR="$PANTHERA_ROOT/userland/coreutils/bin"
SYSROOT="$PANTHERA_ROOT/userland/libsystem/build/sysroot"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
CC="$(xcrun -find clang)"

FILE_CMDS="$PANTHERA_ROOT/src/file_cmds-475"
TEXT_CMDS="$PANTHERA_ROOT/src/text_cmds-197"
SHELL_CMDS="$PANTHERA_ROOT/src/shell_cmds-326"

mkdir -p "$OUTDIR"

PASS=0
FAIL=0

# Common flags — SDK headers, Panthera sysroot for linking
TGT="-target x86_64-apple-darwin23.0"
SHIMDIR="$PANTHERA_ROOT/userland/coreutils/shims"
CFLAGS_COMMON="$TGT -mmacosx-version-min=14.0 -isysroot $SDKROOT -O2 -Wno-deprecated-declarations -I$SHIMDIR"
# Link against SDK's libSystem TBD stubs — they declare every public symbol.
# At runtime, the guest's real libSystem.B.dylib (re-exporting Panthera
# sub-dylibs) satisfies these references.  Do NOT add -L paths to the
# Panthera sysroot here — incomplete Panthera dylibs shadow the SDK stubs.
LDFLAGS_COMMON="$TGT -isysroot $SDKROOT -lSystem"

build() {
    local name="$1"
    shift
    printf "  %-12s" "$name"
    if "$CC" $CFLAGS_COMMON \
        -o "$OUTDIR/$name" \
        "$@" \
        $LDFLAGS_COMMON 2>/tmp/panthera_build_$name.log; then
        echo "OK  ($(wc -c < "$OUTDIR/$name" | tr -d ' ') bytes)"
        PASS=$((PASS + 1))
    else
        echo "FAILED"
        tail -5 /tmp/panthera_build_$name.log
        FAIL=$((FAIL + 1))
    fi
}

echo "============================================"
echo " Panthera Coreutils Build"
echo " Target: x86_64-apple-darwin23.0"
echo "============================================"
echo ""

echo ">>> Simple commands"
build cat      "$TEXT_CMDS/cat/cat.c" -DNO_UDOM_SUPPORT
build echo     "$SHELL_CMDS/echo/echo.c"
build mkdir    "$FILE_CMDS/mkdir/mkdir.c"
build rm       "$FILE_CMDS/rm/rm.c"
build mv       "$FILE_CMDS/mv/mv.c"
build hostname "$SHELL_CMDS/hostname/hostname.c"
build pwd      "$SHELL_CMDS/pwd/pwd.c"
build test     "$SHELL_CMDS/test/test.c"
build true     "$SHELL_CMDS/true/true.c"
build false    "$SHELL_CMDS/false/false.c"
build yes      "$SHELL_CMDS/yes/yes.c"
build sleep    "$SHELL_CMDS/sleep/sleep.c"
build basename "$SHELL_CMDS/basename/basename.c"
build dirname  "$SHELL_CMDS/dirname/dirname.c"
build printenv "$SHELL_CMDS/printenv/printenv.c"
build tee      "$SHELL_CMDS/tee/tee.c"
build uname    "$SHELL_CMDS/uname/uname.c"
build head     "$TEXT_CMDS/head/head.c"
build wc       "$TEXT_CMDS/wc/wc.c" -DNO_XO
build rev      "$TEXT_CMDS/rev/rev.c"
build uniq     "$TEXT_CMDS/uniq/uniq.c"
echo ""

echo ">>> Multi-file commands"
build ls       "$FILE_CMDS/ls/ls.c" "$FILE_CMDS/ls/cmp.c" "$FILE_CMDS/ls/print.c" "$FILE_CMDS/ls/util.c" -I"$FILE_CMDS/ls" -include "$SHIMDIR/ls_nocolor_fix.h"
build cp       "$FILE_CMDS/cp/cp.c" "$FILE_CMDS/cp/utils.c" -I"$FILE_CMDS/cp"
build chmod    "$FILE_CMDS/chmod/chmod.c" "$FILE_CMDS/chmod/chmod_acl.c" -I"$FILE_CMDS/chmod"
build env      "$SHELL_CMDS/env/env.c" "$SHELL_CMDS/env/envopts.c" -I"$SHELL_CMDS/env"
build id       "$SHELL_CMDS/id/id.c" -DHAVE_GETPWNAM -DHAVE_GETGRNAM
echo ""

echo ">>> Text processing"
build cut      "$TEXT_CMDS/cut/cut.c"
build comm     "$TEXT_CMDS/comm/comm.c"
build expand   "$TEXT_CMDS/expand/expand.c"
build paste    "$TEXT_CMDS/paste/paste.c"
build fold     "$TEXT_CMDS/fold/fold.c"
echo ""

echo "============================================"
echo " Results: $PASS passed, $FAIL failed"
echo "============================================"

echo ""
echo "Built binaries:"
for f in "$OUTDIR"/*; do
    [ -f "$f" ] && [ -x "$f" ] && echo "  $(basename "$f")"
done

exit 0
