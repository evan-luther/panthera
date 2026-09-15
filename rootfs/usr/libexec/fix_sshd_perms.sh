#!/bin/zsh
# Fix SSH ownership issues — files created on host have uid=501 instead of root
chown -R 0:0 /var/empty
chmod 755 /var/empty
chown 0:0 /etc/ssh/ssh_host_*
chmod 600 /etc/ssh/ssh_host_rsa_key /etc/ssh/ssh_host_ed25519_key 2>/dev/null
chmod 644 /etc/ssh/ssh_host_rsa_key.pub /etc/ssh/ssh_host_ed25519_key.pub 2>/dev/null
chown 0:0 /etc/ssh/sshd_config
echo "fix_sshd_perms: done"
