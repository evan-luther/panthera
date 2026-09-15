#!/bin/sh
sleep 30
echo "=== OpenSSL s_client test ==="
echo | openssl s_client -connect google.com:443 -CAfile /etc/ssl/cert.pem -brief 2>&1
echo "=== openssl exit: $? ==="
echo "=== DONE ==="
