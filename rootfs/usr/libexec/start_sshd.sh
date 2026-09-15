#!/bin/zsh
echo "start_sshd: starting OpenSSH sshd -D -e -f /etc/ssh/sshd_config"
exec /usr/sbin/sshd -D -e -f /etc/ssh/sshd_config
