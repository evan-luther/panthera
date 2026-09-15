#!/bin/sh

exec >/dev/console 2>&1

echo "PANTHERA_ZFS_SMOKE:enter"

if [ -x /usr/bin/test_pthread_detached ]; then
	/usr/bin/test_pthread_detached
	rc=$?
	echo "PANTHERA_ZFS_SMOKE:pthread_detached_rc=${rc}"
	if [ "${rc}" -ne 0 ]; then
		exit "${rc}"
	fi
fi

: "${PANTHERA_ZFS_ROOTDEV:?PANTHERA_ZFS_ROOTDEV must name the boot root slice}"
rootdev="${PANTHERA_ZFS_ROOTDEV#/dev/}"
root_path="/dev/${rootdev}"
TARGET=
candidate_count=0
for candidate in /dev/disk*s1; do
	[ "${candidate}" = "${root_path}" ] && continue
	[ -e "${candidate}" ] || continue
	TARGET="${candidate}"
	candidate_count=$((candidate_count + 1))
done
if [ "${candidate_count}" -ne 1 ] || [ -z "${TARGET}" ] || [ "${TARGET}" = "${root_path}" ]; then
	echo "PANTHERA_ZFS_SMOKE:expected_one_non_root_slice:count=${candidate_count}:root=${root_path}"
	exit 92
fi
echo "PANTHERA_ZFS_SMOKE:target=${TARGET}"

i=0
while [ "${i}" -lt 30 ]; do
	if [ -e "${TARGET}" ]; then
		break
	fi
	i=$((i + 1))
	/bin/sleep 1
done

if [ ! -e "${TARGET}" ]; then
	echo "PANTHERA_ZFS_SMOKE:target_missing:${TARGET}"
	exit 93
fi

PANTHERA_ZFS_USERLAND_TRACE=1 /sbin/zpool import -N -d "${TARGET}" pantherazboot
rc=$?
echo "PANTHERA_ZFS_SMOKE:import_rc=${rc}"
if [ "${rc}" -ne 0 ]; then
	exit "${rc}"
fi

/sbin/zpool status pantherazboot
/sbin/zfs mount pantherazboot
rc=$?
echo "PANTHERA_ZFS_SMOKE:mount_rc=${rc}"
if [ "${rc}" -ne 0 ]; then
	exit "${rc}"
fi
/bin/cat /z/hello.txt
/sbin/umount /z
rc=$?
echo "PANTHERA_ZFS_SMOKE:unmount_rc=${rc}"
if [ "${rc}" -ne 0 ]; then
	exit "${rc}"
fi

/sbin/zpool export pantherazboot
rc=$?
echo "PANTHERA_ZFS_SMOKE:export_rc=${rc}"
if [ "${rc}" -ne 0 ]; then
	exit "${rc}"
fi

echo "PANTHERA_ZFS_BOOT_SMOKE_OK"
