#!/bin/bash
#
# build_new_coreutils.sh — Cross-compile additional Apple coreutils for Panthera x86_64
# Adds ~37 commands to the existing 31
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

TGT="-target x86_64-apple-darwin23.0"
SHIMDIR="$PANTHERA_ROOT/userland/coreutils/shims"
CFLAGS_COMMON="$TGT -mmacosx-version-min=14.0 -isysroot $SDKROOT -O2 -Wno-deprecated-declarations -I$SHIMDIR"
LDFLAGS_COMMON="$TGT -isysroot $SDKROOT -lSystem"

PRINTF_PATCH="$PANTHERA_ROOT/userland/coreutils/patches/panthera-printf-flush.patch"
if patch -p0 -d "$SHELL_CMDS" -N --dry-run < "$PRINTF_PATCH" >/dev/null 2>&1; then
    patch -p0 -d "$SHELL_CMDS" -f -s < "$PRINTF_PATCH"
elif ! patch -p0 -d "$SHELL_CMDS" -f -R --dry-run < "$PRINTF_PATCH" >/dev/null 2>&1; then
    echo "ERROR: printf patch does not apply cleanly to $SHELL_CMDS" >&2
    exit 1
fi

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
echo " Panthera NEW Coreutils Build"
echo " Target: x86_64-apple-darwin23.0"
echo "============================================"
echo ""

# =============================================
# PRIORITY 1 — Daily-use commands
# =============================================
echo ">>> Priority 1: Daily-use commands"
echo ""

echo "--- text_cmds ---"

# grep: 4 .c files (lzma.h shimmed — stub, no real xz support; needs libz + libbz2 for compressed files)
build grep \
    "$TEXT_CMDS/grep/grep.c" \
    "$TEXT_CMDS/grep/file.c" \
    "$TEXT_CMDS/grep/queue.c" \
    "$TEXT_CMDS/grep/util.c" \
    -I"$TEXT_CMDS/grep" \
    -lz -lbz2

# sed: 4 .c files
build sed \
    "$TEXT_CMDS/sed/main.c" \
    "$TEXT_CMDS/sed/compile.c" \
    "$TEXT_CMDS/sed/misc.c" \
    "$TEXT_CMDS/sed/process.c" \
    -I"$TEXT_CMDS/sed"

# sort: many .c files, uses CommonCrypto (available in SDK)
build sort \
    "$TEXT_CMDS/sort/sort.c" \
    "$TEXT_CMDS/sort/bwstring.c" \
    "$TEXT_CMDS/sort/coll.c" \
    "$TEXT_CMDS/sort/file.c" \
    "$TEXT_CMDS/sort/mem.c" \
    "$TEXT_CMDS/sort/radixsort.c" \
    "$TEXT_CMDS/sort/vsort.c" \
    -I"$TEXT_CMDS/sort" \
    -DWITHOUT_NLS -DSORT_THREADS \
    -DSORT_VERSION=\"197\"

# tail: 5 .c files (expand_number shimmed)
build tail \
    "$TEXT_CMDS/tail/tail.c" \
    "$TEXT_CMDS/tail/forward.c" \
    "$TEXT_CMDS/tail/misc.c" \
    "$TEXT_CMDS/tail/read.c" \
    "$TEXT_CMDS/tail/reverse.c" \
    -I"$TEXT_CMDS/tail" \
    -include "$SHIMDIR/expand_number.h"

# tr: 4 .c files
build tr \
    "$TEXT_CMDS/tr/tr.c" \
    "$TEXT_CMDS/tr/cmap.c" \
    "$TEXT_CMDS/tr/cset.c" \
    "$TEXT_CMDS/tr/str.c" \
    -I"$TEXT_CMDS/tr"

echo ""
echo "--- shell_cmds ---"

# date: 2 .c files
build date \
    "$SHELL_CMDS/date/date.c" \
    "$SHELL_CMDS/date/vary.c" \
    -I"$SHELL_CMDS/date"

# find: 7 .c files + getdate.y (yacc)
echo -n "  find        "
FIND_TMP="$(mktemp -d)"
if yacc -o "$FIND_TMP/getdate.c" "$SHELL_CMDS/find/getdate.y" 2>/tmp/panthera_build_find_yacc.log; then
    if "$CC" $CFLAGS_COMMON \
        -o "$OUTDIR/find" \
        "$SHELL_CMDS/find/main.c" \
        "$SHELL_CMDS/find/find.c" \
        "$SHELL_CMDS/find/function.c" \
        "$SHELL_CMDS/find/ls.c" \
        "$SHELL_CMDS/find/misc.c" \
        "$SHELL_CMDS/find/operator.c" \
        "$SHELL_CMDS/find/option.c" \
        "$FIND_TMP/getdate.c" \
        -I"$SHELL_CMDS/find" \
        $LDFLAGS_COMMON 2>/tmp/panthera_build_find.log; then
        echo "OK  ($(wc -c < "$OUTDIR/find" | tr -d ' ') bytes)"
        PASS=$((PASS + 1))
    else
        echo "FAILED (compile)"
        tail -5 /tmp/panthera_build_find.log
        FAIL=$((FAIL + 1))
    fi
else
    echo "FAILED (yacc)"
    tail -5 /tmp/panthera_build_find_yacc.log
    FAIL=$((FAIL + 1))
fi
rm -rf "$FIND_TMP"

# printf: single file
build printf "$SHELL_CMDS/printf/printf.c"

# xargs: 2 .c files
build xargs \
    "$SHELL_CMDS/xargs/xargs.c" \
    "$SHELL_CMDS/xargs/strnsubst.c" \
    -I"$SHELL_CMDS/xargs"

# which: single file
build which "$SHELL_CMDS/which/which.c"

# kill: single file
build kill "$SHELL_CMDS/kill/kill.c"

# expr: yacc file — generate C first
echo -n "  expr        "
EXPR_TMP="$(mktemp -d)"
if yacc -o "$EXPR_TMP/expr.c" "$SHELL_CMDS/expr/expr.y" 2>/tmp/panthera_build_expr_yacc.log; then
    if "$CC" $CFLAGS_COMMON \
        -o "$OUTDIR/expr" \
        "$EXPR_TMP/expr.c" \
        $LDFLAGS_COMMON 2>/tmp/panthera_build_expr.log; then
        echo "OK  ($(wc -c < "$OUTDIR/expr" | tr -d ' ') bytes)"
        PASS=$((PASS + 1))
    else
        echo "FAILED (compile)"
        tail -5 /tmp/panthera_build_expr.log
        FAIL=$((FAIL + 1))
    fi
else
    echo "FAILED (yacc)"
    tail -5 /tmp/panthera_build_expr_yacc.log
    FAIL=$((FAIL + 1))
fi
rm -rf "$EXPR_TMP"

# seq: single file
build seq "$SHELL_CMDS/seq/seq.c"

echo ""
echo "--- file_cmds ---"

# touch: single file
build touch "$FILE_CMDS/touch/touch.c"

# ln: single file
build ln "$FILE_CMDS/ln/ln.c"

# chown: single file
build chown "$FILE_CMDS/chown/chown.c"

# stat: single file
build stat "$FILE_CMDS/stat/stat.c"

# rmdir: single file
build rmdir "$FILE_CMDS/rmdir/rmdir.c"

# dd: 7 .c files (HN_IEC_PREFIXES shimmed in libutil.h)
build dd \
    "$FILE_CMDS/dd/dd.c" \
    "$FILE_CMDS/dd/args.c" \
    "$FILE_CMDS/dd/conv.c" \
    "$FILE_CMDS/dd/conv_tab.c" \
    "$FILE_CMDS/dd/misc.c" \
    "$FILE_CMDS/dd/position.c" \
    -I"$FILE_CMDS/dd"

echo ""

# =============================================
# PRIORITY 2 — Important utilities
# =============================================
echo ">>> Priority 2: Important utilities"
echo ""

echo "--- text_cmds ---"
build fmt    "$TEXT_CMDS/fmt/fmt.c"

# join: needs stdbool.h
build join   "$TEXT_CMDS/join/join.c" -include stdbool.h

build nl     "$TEXT_CMDS/nl/nl.c"

# split: needs expand_number
build split  "$TEXT_CMDS/split/split.c" -include "$SHIMDIR/expand_number.h"

build column "$TEXT_CMDS/column/column.c"

# ed: 7 .c files
build ed \
    "$TEXT_CMDS/ed/main.c" \
    "$TEXT_CMDS/ed/buf.c" \
    "$TEXT_CMDS/ed/glbl.c" \
    "$TEXT_CMDS/ed/io.c" \
    "$TEXT_CMDS/ed/re.c" \
    "$TEXT_CMDS/ed/sub.c" \
    "$TEXT_CMDS/ed/undo.c" \
    -I"$TEXT_CMDS/ed"

echo ""
echo "--- shell_cmds ---"
build nice    "$SHELL_CMDS/nice/nice.c"

# nohup: vproc_priv.h shimmed
build nohup   "$SHELL_CMDS/nohup/nohup.c"

build mktemp  "$SHELL_CMDS/mktemp/mktemp.c"
build realpath "$SHELL_CMDS/realpath/realpath.c"
build killall "$SHELL_CMDS/killall/killall.c"

# hexdump: 6 .c files
build hexdump \
    "$SHELL_CMDS/hexdump/hexdump.c" \
    "$SHELL_CMDS/hexdump/conv.c" \
    "$SHELL_CMDS/hexdump/display.c" \
    "$SHELL_CMDS/hexdump/hexsyntax.c" \
    "$SHELL_CMDS/hexdump/odsyntax.c" \
    "$SHELL_CMDS/hexdump/parse.c" \
    -I"$SHELL_CMDS/hexdump"

build who "$SHELL_CMDS/who/who.c"

echo ""
echo "--- file_cmds ---"

# du: needs expand_number
build du       "$FILE_CMDS/du/du.c" -include "$SHIMDIR/expand_number.h"

# truncate: needs expand_number
build truncate "$FILE_CMDS/truncate/truncate.c" -include "$SHIMDIR/expand_number.h"

build mkfifo   "$FILE_CMDS/mkfifo/mkfifo.c"
build mknod    "$FILE_CMDS/mknod/mknod.c" "$FILE_CMDS/mknod/pack_dev.c" -I"$FILE_CMDS/mknod"

# cksum: 6 .c files (no crypto)
build cksum \
    "$FILE_CMDS/cksum/cksum.c" \
    "$FILE_CMDS/cksum/crc.c" \
    "$FILE_CMDS/cksum/crc32.c" \
    "$FILE_CMDS/cksum/print.c" \
    "$FILE_CMDS/cksum/sum1.c" \
    "$FILE_CMDS/cksum/sum2.c" \
    -I"$FILE_CMDS/cksum"

echo ""

# =============================================
# SKIPPED (documented reasons)
# =============================================
echo ">>> Skipped:"
echo "  df         — needs libxo"
echo "  w          — needs kvm.h"
echo "  gzip       — needs zlib"
echo "  script     — needs libutil/pty"
echo ""

echo "============================================"
echo " Results: $PASS passed, $FAIL failed"
echo "============================================"
echo ""
echo "All binaries in $OUTDIR:"
ls -1 "$OUTDIR"

exit 0
