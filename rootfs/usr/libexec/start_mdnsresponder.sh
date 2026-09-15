#!/bin/sh
echo "PANTHERA:mDNSResponder wrapper starting after loopback grace"
exec /usr/sbin/mDNSResponder -debug
