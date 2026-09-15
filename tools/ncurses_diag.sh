#!/bin/zsh
# Diagnostic script: runs ncurses probe, then tests clear and nano
# Output goes to serial console (stdout)

export TERM=vt100
export PATH=/bin:/sbin:/usr/bin:/usr/sbin

echo "=== ncurses_diag start ==="

echo "--- running setupterm_probe (clean slate) ---"
/bin/setupterm_probe 2>&1

echo ""
echo "--- running ncurses_probe ---"
/bin/ncurses_probe 2>&1

echo ""
echo "--- testing: TERMINFO=/usr/share/terminfo /bin/clear ---"
TERMINFO=/usr/share/terminfo /bin/clear 2>&1
echo "clear exit=$?"

echo ""
echo "--- testing: /bin/clear (no explicit TERMINFO) ---"
/bin/clear 2>&1
echo "clear exit=$?"

echo ""
echo "--- testing: tput clear ---"
/bin/tput clear 2>&1
echo "tput clear exit=$?"

echo ""
echo "--- testing: curl --version ---"
/bin/curl --version 2>&1
echo "curl exit=$?"

echo "=== ncurses_diag done ==="
