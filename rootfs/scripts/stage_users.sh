#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 5 ]]; then
  echo "Usage: $(basename "$0") MOUNTED_VOLUME ROOT_HASH PANTHERA_HASH ROOT_SHELL PANTHERA_SHELL" >&2
  exit 2
fi

mounted_volume="$1"
root_password_hash="$2"
panthera_password_hash="$3"
root_login_shell="$4"
panthera_login_shell="$5"

cat > "${mounted_volume}/etc/passwd" <<EOF
root:${root_password_hash}:0:0:System Administrator:/var/root:${root_login_shell}
daemon:*:1:1:System Services:/var/root:/usr/bin/false
nobody:*:-2:-2:Unprivileged User:/var/empty:/usr/bin/false
panthera:${panthera_password_hash}:501:20:Panthera User:/Users/panthera:${panthera_login_shell}
EOF

cat > "${mounted_volume}/etc/group" <<'EOF'
wheel:*:0:root
daemon:*:1:
staff:*:20:root,panthera
nobody:*:-2:
EOF

cat > "${mounted_volume}/etc/master.passwd" <<EOF
root:${root_password_hash}:0:0::0:0:System Administrator:/var/root:${root_login_shell}
daemon:*:1:1::0:0:System Services:/var/root:/usr/bin/false
nobody:*:-2:-2::0:0:Unprivileged User:/var/empty:/usr/bin/false
panthera:${panthera_password_hash}:501:20::0:0:Panthera User:/Users/panthera:${panthera_login_shell}
EOF
