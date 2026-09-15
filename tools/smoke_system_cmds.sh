#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DATASET="${PANTHERA_ZFS_BOOT:-tank/ROOT/panthera}"
POOL="${DATASET%%/*}"

# Run only against disposable overlays: the smoke changes tty state and creates a dataset.
read -r -d '' COMMAND <<'GUEST' || true
set -e
for c in ps mount umount sysctl stty dmesg reboot shutdown less bsdtar vim curl; do
  command -v "$c" || exit 1
done
/usr/bin/libsystem_primitives_probe
[ "$(/bin/ps -p 1 -o pid= | /usr/bin/tr -d '[:space:]')" = 1 ]
[ "$(/bin/ps -p $$ -o pid= | /usr/bin/tr -d '[:space:]')" = "$$" ]
/bin/ps aux
/usr/sbin/sysctl kern.ostype
/sbin/dmesg >/tmp/r31-dmesg
/usr/bin/vim -Nu NONE -n -es -c 'call writefile([string(float2nr(pow(2.0, 3.0)))], "/tmp/r31-vim-math")' -c qa
[ "$(/usr/bin/curl -fsS file:///tmp/r31-vim-math)" = 8 ]
/bin/echo R31_ARCHIVE_ROUNDTRIP >/tmp/r31-archive-input
bsdtar -cf /tmp/r31-archive.tar -C /tmp r31-archive-input
/bin/rm /tmp/r31-archive-input
bsdtar -xf /tmp/r31-archive.tar -C /tmp
[ "$(less /tmp/r31-archive-input)" = R31_ARCHIVE_ROUNDTRIP ]
/bin/stty -f /dev/console -a
tty_state=$(/bin/stty -f /dev/console -g)
trap '/bin/stty -f /dev/console "$tty_state"' EXIT
/bin/stty -f /dev/console -echo
/bin/stty -f /dev/console "$tty_state"
[ "$(/bin/stty -f /dev/console -g)" = "$tty_state" ]
if /sbin/shutdown; then exit 1; fi
if /sbin/shutdown -k invalid; then exit 1; fi
if /sbin/shutdown -k +1garbage; then exit 1; fi
if /sbin/shutdown -k +9223372036854775806mins; then exit 1; fi
if /sbin/shutdown -h -r now; then exit 1; fi
/sbin/shutdown -k -q now
[ ! -e /etc/nologin ]
/sbin/mount -t zfs
/bin/mkdir /tmp/r31-mount
if /sbin/mount -t panthera_missing /dev/null /tmp/r31-mount; then exit 1; fi
/sbin/zfs create -o mountpoint=legacy "$pool/r31-mount"
/sbin/mount -t zfs "$pool/r31-mount" /tmp/r31-mount
/bin/echo R31_MOUNT_DATA >/tmp/r31-mount/proof
/sbin/umount /tmp/r31-mount
/sbin/mount -t zfs "$pool/r31-mount" /tmp/r31-mount
[ "$(/bin/cat /tmp/r31-mount/proof)" = R31_MOUNT_DATA ]
/sbin/umount /tmp/r31-mount
/sbin/zfs destroy "$pool/r31-mount"
/bin/rmdir /tmp/r31-mount
/bin/echo PANTHERA_SYSTEM_CMDS_OK
GUEST

PANTHERA_ROOT_DISK_SNAPSHOT=on \
  bash "${ROOT}/tools/smoke_openssh.sh" "$@" \
    --command "pool='${POOL}'; ${COMMAND}" \
    --marker PANTHERA_SYSTEM_CMDS_OK
