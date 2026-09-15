#!/bin/zsh
set -e

tag="${1:-pkgsrc-extract-guest}"
tarball="${2:-/var/tmp/pkgsrc.tar.gz}"
expected_count="${3:-286992}"
pkgroot="/tmp/${tag}-tree"
log="/tmp/${tag}.extract.log"
err="/tmp/${tag}.extract.err"
progress_interval="${PANTHERA_PKGSRC_EXTRACT_PROGRESS_INTERVAL:-60}"
default_tar_extract_flags="--no-xattrs --no-acls --no-mac-metadata --no-same-owner --numeric-owner"
if (( ${+PANTHERA_PKGSRC_EXTRACT_TAR_FLAGS} )); then
	tar_extract_flags="${PANTHERA_PKGSRC_EXTRACT_TAR_FLAGS}"
else
	tar_extract_flags="${default_tar_extract_flags}"
fi

if [ -n "${tar_extract_flags}" ]; then
	for flag in ${=tar_extract_flags}; do
		if [[ ! "${flag}" =~ '^[-A-Za-z0-9_./=,:+]+$' ]]; then
			/bin/echo "tar extract flags must be simple shell words: ${flag}" >&2
			exit 2
		fi
	done
fi

/bin/rm -rf "${pkgroot}" "${log}" "${err}"
/bin/test -s "${tarball}"
/bin/mkdir -p "${pkgroot}"

/bin/echo PANTHERA_PKGSRC_EXTRACT_STAGE:START
/bin/echo PANTHERA_PKGSRC_EXTRACT_STAGE:COUNT
expected=$(/usr/bin/tar -tzf "${tarball}" | /usr/bin/awk 'END { print NR }')
/bin/echo PANTHERA_PKGSRC_EXTRACT_EXPECTED:${expected}
/bin/test "${expected}" = "${expected_count}"

/bin/echo PANTHERA_PKGSRC_EXTRACT_STAGE:EXTRACT
/bin/echo PANTHERA_PKGSRC_EXTRACT_TAR_FLAGS:${tar_extract_flags:-none}
/usr/bin/tar ${=tar_extract_flags} -xzf "${tarball}" -C "${pkgroot}" >"${log}" 2>"${err}" &
tar_pid=$!
sample=0
start=$SECONDS
while kill -0 "${tar_pid}" 2>/dev/null; do
	/bin/sleep "${progress_interval}"
	sample=$((sample + 1))
	elapsed=$((SECONDS - start))
	/bin/echo PANTHERA_PKGSRC_EXTRACT_HEARTBEAT:${sample}:${elapsed}
done
wait "${tar_pid}"
tar_rc=$?
/bin/echo PANTHERA_PKGSRC_EXTRACT_ELAPSED:$((SECONDS - start))
/bin/echo PANTHERA_PKGSRC_EXTRACT_TAR_RC:${tar_rc}
/bin/cat "${err}"
/bin/test "${tar_rc}" = 0

/bin/echo PANTHERA_PKGSRC_EXTRACT_STAGE:SYNC
/sbin/sync

/bin/echo PANTHERA_PKGSRC_EXTRACT_STAGE:FINAL_COUNT
files=$(/usr/bin/find "${pkgroot}/pkgsrc" -type f 2>/dev/null | /usr/bin/awk 'END { print NR }')
dirs=$(/usr/bin/find "${pkgroot}/pkgsrc" -type d 2>/dev/null | /usr/bin/awk 'END { print NR }')
size_kb=$(/usr/bin/du -sk "${pkgroot}/pkgsrc" 2>/dev/null | /usr/bin/awk '{ print $1 }')
/bin/echo PANTHERA_PKGSRC_EXTRACT_FILES:${files}
/bin/echo PANTHERA_PKGSRC_EXTRACT_DIRS:${dirs}
/bin/echo PANTHERA_PKGSRC_EXTRACT_SIZE_KB:${size_kb}
/bin/echo PANTHERA_PKGSRC_EXTRACT_OK
