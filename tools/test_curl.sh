#!/bin/sh
sleep 30
echo "=== curl -vkL https://google.com ==="
/usr/bin/curl -vkL --connect-timeout 15 --max-time 30 https://google.com/ 2>&1 | head -60
echo ""
echo "exit=$?"
echo "=== DONE ==="
