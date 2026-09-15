#!/bin/bash
#
# build_more_coreutils.sh — Build additional Panthera utilities from local source trees
#
set -e
set -o pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUTDIR="$PANTHERA_ROOT/userland/coreutils/bin"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
CC="$(xcrun -find clang)"

FILE_CMDS="$PANTHERA_ROOT/src/file_cmds-475"
TEXT_CMDS="$PANTHERA_ROOT/src/text_cmds-197"
SHELL_CMDS="$PANTHERA_ROOT/src/shell_cmds-326"
NCURSES="$PANTHERA_ROOT/src/apple-ncurses-71.100.2/ncurses"
NCURSES_BUILD="$PANTHERA_ROOT/build/apple-ncurses-target"
NCURSES_BUILD_SCRIPT="$PANTHERA_ROOT/tools/build_apple_ncurses.sh"
NCURSES_PROGS="$NCURSES_BUILD/progs"
SHIMDIR="$PANTHERA_ROOT/userland/coreutils/shims"
NCURSES_LIBDIR="$PANTHERA_ROOT/userland/libsystem/build/sysroot/usr/lib"

mkdir -p "$OUTDIR"

if [ -x "$NCURSES_BUILD_SCRIPT" ]; then
	bash "$NCURSES_BUILD_SCRIPT" --target-lib
fi

PASS=0
FAIL=0

TGT="-target x86_64-apple-darwin23.0"
CFLAGS_COMMON="$TGT -mmacosx-version-min=14.0 -isysroot $SDKROOT -O2 -Wno-deprecated-declarations -I$SHIMDIR"
LDFLAGS_COMMON="$TGT -isysroot $SDKROOT -lSystem"
NCURSES_INCLUDES="-I$NCURSES_BUILD/include -I$NCURSES/include -I$NCURSES/ncurses -I$NCURSES/progs"
NCURSES_LDFLAGS="-L$NCURSES_LIBDIR -lncurses.5.4 -lSystem"

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
		tail -20 /tmp/panthera_build_$name.log
		FAIL=$((FAIL + 1))
	fi
}

build_ncurses_tool() {
	local name="$1"
	shift
	local objdir
	objdir="$(mktemp -d)"

	printf "  %-12s" "$name"
	cat >"$objdir/transform.h" <<'EOF'
#ifndef __TRANSFORM_H
#define __TRANSFORM_H 1
#include <progs.priv.h>
extern bool same_program(const char *, const char *);
#define PROG_CAPTOINFO "captoinfo"
#define PROG_INFOTOCAP "infotocap"
#define PROG_CLEAR "clear"
#define PROG_RESET "reset"
#define PROG_INIT "init"
#endif
EOF

	sh "$NCURSES/progs/MKtermsort.sh" awk "$NCURSES/include/Caps" >"$objdir/termsort.h"

	if "$CC" $TGT -mmacosx-version-min=14.0 -isysroot "$SDKROOT" -O2 -Wno-deprecated-declarations \
		-I"$SHIMDIR" $NCURSES_INCLUDES -I"$objdir" \
		-o "$OUTDIR/$name" \
		"$@" \
		$NCURSES_LDFLAGS 2>/tmp/panthera_build_$name.log; then
		echo "OK  ($(wc -c < "$OUTDIR/$name" | tr -d ' ') bytes)"
		PASS=$((PASS + 1))
	else
		echo "FAILED"
		tail -20 /tmp/panthera_build_$name.log
		FAIL=$((FAIL + 1))
	fi

	rm -rf "$objdir"
}

stage_apple_ncurses_tool() {
	local name="$1"
	local src="$NCURSES_PROGS/$name"

	printf "  %-12s" "$name"
	if [ ! -x "$src" ]; then
		echo "FAILED"
		echo "missing Apple ncurses tool: $src"
		FAIL=$((FAIL + 1))
		return
	fi

	cp -f "$src" "$OUTDIR/$name"
	install_name_tool \
		-change /usr/local/lib/libncursesw.5.4.dylib /usr/lib/libncurses.5.dylib \
		"$OUTDIR/$name"
	echo "OK  ($(wc -c < "$OUTDIR/$name" | tr -d ' ') bytes)"
	PASS=$((PASS + 1))
}

echo "============================================"
echo " Panthera More Coreutils Build"
echo " Target: x86_64-apple-darwin23.0"
echo "============================================"
echo ""

echo ">>> Priority 1: ncurses tools"
stage_apple_ncurses_tool clear
stage_apple_ncurses_tool tput
stage_apple_ncurses_tool tset
if [ -f "$OUTDIR/tset" ]; then
	cp -f "$OUTDIR/tset" "$OUTDIR/reset"
fi
stage_apple_ncurses_tool infocmp
stage_apple_ncurses_tool tic
stage_apple_ncurses_tool tabs
stage_apple_ncurses_tool toe
echo ""

echo ">>> Priority 2: local Apple command sources"
echo ""
build chflags     "$FILE_CMDS/chflags/chflags.c"
build ipcrm       "$FILE_CMDS/ipcrm/ipcrm.c"
build pathchk     "$FILE_CMDS/pathchk/pathchk.c"
build xattr       "$FILE_CMDS/xattr/xattr.c"

build chroot      "$SHELL_CMDS/chroot/chroot.c"
build getopt      "$SHELL_CMDS/getopt/getopt.c"
build jot         "$SHELL_CMDS/jot/jot.c"
build lockf       "$SHELL_CMDS/lockf/lockf.c"
build logname     "$SHELL_CMDS/logname/logname.c"
build path_helper "$SHELL_CMDS/path_helper/path_helper.c"
build renice      "$SHELL_CMDS/renice/renice.c"
build shlock      "$SHELL_CMDS/shlock/shlock.c"
build systime     "$SHELL_CMDS/systime/systime.c"
build time        "$SHELL_CMDS/time/time.c"
build what        "$SHELL_CMDS/what/what.c"
build whereis     "$SHELL_CMDS/whereis/whereis.c" -I"$SHELL_CMDS/whereis"

build banner      "$TEXT_CMDS/banner/banner.c"
build col         "$TEXT_CMDS/col/col.c"
build colrm       "$TEXT_CMDS/colrm/colrm.c"
build csplit      "$TEXT_CMDS/csplit/csplit.c"
build lam         "$TEXT_CMDS/lam/lam.c"
build look        "$TEXT_CMDS/look/look.c" -I"$TEXT_CMDS/look"
build pr          "$TEXT_CMDS/pr/pr.c" "$TEXT_CMDS/pr/egetopt.c" -I"$TEXT_CMDS/pr"
build rs          "$TEXT_CMDS/rs/rs.c"
build ul          "$TEXT_CMDS/ul/ul.c" -L"$NCURSES_LIBDIR" -lncurses.5.4
build unexpand    "$TEXT_CMDS/unexpand/unexpand.c"
build unvis       "$TEXT_CMDS/unvis/unvis.c"
build vis         "$TEXT_CMDS/vis/vis.c" "$TEXT_CMDS/vis/foldit.c" -I"$TEXT_CMDS/vis"

build uuencode    "$TEXT_CMDS/bintrans/bintrans.c" "$TEXT_CMDS/bintrans/uuencode.c" "$TEXT_CMDS/bintrans/uudecode.c" "$TEXT_CMDS/bintrans/apple_base64.c" "$TEXT_CMDS/bintrans/qp.c" -I"$TEXT_CMDS/bintrans"
build uudecode    "$TEXT_CMDS/bintrans/bintrans.c" "$TEXT_CMDS/bintrans/uuencode.c" "$TEXT_CMDS/bintrans/uudecode.c" "$TEXT_CMDS/bintrans/apple_base64.c" "$TEXT_CMDS/bintrans/qp.c" -I"$TEXT_CMDS/bintrans"
build base64      "$TEXT_CMDS/bintrans/bintrans.c" "$TEXT_CMDS/bintrans/uuencode.c" "$TEXT_CMDS/bintrans/uudecode.c" "$TEXT_CMDS/bintrans/apple_base64.c" "$TEXT_CMDS/bintrans/qp.c" -I"$TEXT_CMDS/bintrans"
build qp          "$TEXT_CMDS/bintrans/bintrans.c" "$TEXT_CMDS/bintrans/uuencode.c" "$TEXT_CMDS/bintrans/uudecode.c" "$TEXT_CMDS/bintrans/apple_base64.c" "$TEXT_CMDS/bintrans/qp.c" -I"$TEXT_CMDS/bintrans"

echo ""
echo "============================================"
echo " Results: $PASS passed, $FAIL failed"
echo "============================================"
echo ""
echo "Built binaries:"
for f in "$OUTDIR"/*; do
	[ -f "$f" ] && [ -x "$f" ] && echo "  $(basename "$f")"
done
