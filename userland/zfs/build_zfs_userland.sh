#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OPENZFS_SRC="$("${PANTHERA_ROOT}/tools/prepare_openzfs_source.sh")"
OUT_ROOT="${SCRIPT_DIR}"
SBIN_DIR="${OUT_ROOT}/sbin"
LIB_DIR="${OUT_ROOT}/lib"
OBJ_DIR="${OUT_ROOT}/obj"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"

SDKROOT="${SDKROOT:-$(xcrun -sdk macosx --show-sdk-path)}"
CC="${CC:-$(xcrun -find clang)}"
TARGET="${TARGET:-x86_64-apple-darwin23.0}"
MINVER="${MINVER:-14.0}"

mkdir -p "${SBIN_DIR}" "${LIB_DIR}" "${OBJ_DIR}"

if [[ ! -f "${SYSROOT}/usr/lib/libDiskArbitration.dylib" ]]; then
  bash "${PANTHERA_ROOT}/userland/diskarbitration/build_diskarbitration.sh"
fi

if [[ "${PANTHERA_FORCE_REBUILD:-0}" != "1" \
   && -x "${SBIN_DIR}/zsysctl" \
   && -x "${SBIN_DIR}/zpool" \
   && -x "${SBIN_DIR}/zfs" \
   && -x "${SBIN_DIR}/mount_zfs" \
   && -f "${LIB_DIR}/libuutil.a" \
   && -f "${LIB_DIR}/libzfs.a" \
   && -f "${LIB_DIR}/libxdr.a" ]]; then
  echo "OpenZFS userland already staged"
  exit 0
fi

common_cflags=(
  -target "${TARGET}"
  -mmacosx-version-min="${MINVER}"
  -isysroot "${SDKROOT}"
  -O2
  -Wall
  -Wextra
  -Wno-deprecated-declarations
  -Wno-sign-compare
  -Wno-unused-parameter
  -Wno-incompatible-pointer-types
  -Wno-type-limits
  -Wno-missing-field-initializers
  -Wno-format-security
  -D__APPLE_USE_RFC_3542
  -D_REENTRANT
  -D_GNU_SOURCE
  -D_POSIX_PTHREAD_SEMANTICS
  -D_LARGEFILE64_SOURCE=1
  -D_FILE_OFFSET_BITS=64
  -DHAVE_STRLCAT=1
  -DHAVE_STRLCPY=1
  -DHAVE_INTTYPES=1
  -DSYSCONFDIR=\"/etc\"
  -DPKGDATADIR=\"/usr/share/zfs\"
  -include "${SCRIPT_DIR}/compat/panthera_zfs_userland_compat.h"
  -I"${SCRIPT_DIR}/compat"
  -I"${PANTHERA_ROOT}/userland/openssl/stage/usr/include"
  -I"${PANTHERA_ROOT}/userland/zlib/stage/include"
  -I"${OPENZFS_SRC}/lib/libspl/include/os/macos"
  -I"${OPENZFS_SRC}/lib/libspl/include"
  -I"${OPENZFS_SRC}/lib/libnvpair"
  -I"${OPENZFS_SRC}/lib/libuutil"
  -I"${OPENZFS_SRC}/lib/libzfs_core"
  -I"${OPENZFS_SRC}/lib/libzutil"
  -I"${OPENZFS_SRC}/lib/libshare"
  -I"${OPENZFS_SRC}/lib/libzfs"
  -I"${OPENZFS_SRC}/lib/os/macos/libdiskmgt"
  -I"${OPENZFS_SRC}/cmd/zpool"
  -I"${OPENZFS_SRC}/cmd/zfs"
  -I"${OPENZFS_SRC}/include/os/macos/zfs"
  -I"${OPENZFS_SRC}/include"
  -I"${OPENZFS_SRC}/module/icp/include"
)

base_link_opts=(
  -target "${TARGET}"
  -mmacosx-version-min="${MINVER}"
  -isysroot "${SYSROOT}"
  -L"${LIB_DIR}"
  -L"${SYSROOT}/usr/lib"
  -L"${SYSROOT}/usr/lib/system"
  -Wl,-not_for_dyld_shared_cache
  -Wl,-rpath,/usr/lib
)

zsysctl_ldflags=(
  -target "${TARGET}"
  -mmacosx-version-min="${MINVER}"
  -isysroot "${SDKROOT}"
  -Wl,-not_for_dyld_shared_cache
  -lSystem
)

zfs_tool_link_opts=(
  "${base_link_opts[@]}"
  -L"${PANTHERA_ROOT}/userland/openssl/lib"
)

zfs_tool_libs=(
  -lDiskArbitration
  -lCoreFoundation
  -lIOKit
  -lcrypto
  -lz
  -lSystem
)

compile_obj() {
  local src="$1"
  local obj="${OBJ_DIR}/${src//\//_}.o"

  mkdir -p "$(dirname "${obj}")"
  echo "Compiling ${src}" >&2
  "${CC}" "${common_cflags[@]}" -c "${OPENZFS_SRC}/${src}" -o "${obj}"
  printf '%s\n' "${obj}"
}

archive_lib() {
  local out="$1"
  shift
  local objects=()
  local src
  local obj

  for src in "$@"; do
    obj="$(compile_obj "${src}")"
    objects+=("${obj}")
  done

  rm -f "${out}"
  ar rcs "${out}" "${objects[@]}"
  ranlib "${out}"
}

compile_panthera_obj() {
  local src="$1"
  local obj="${OBJ_DIR}/${src//\//_}.o"

  mkdir -p "$(dirname "${obj}")"
  echo "Compiling ${src}" >&2
  "${CC}" \
    -target "${TARGET}" \
    -mmacosx-version-min="${MINVER}" \
    -isysroot "${SDKROOT}" \
    -O2 \
    -Wall \
    -Wextra \
    -Wno-unused-parameter \
    -Wno-sign-compare \
    -I"${PANTHERA_ROOT}/src/Libinfo-583.0.1/rpc.subproj" \
    -I"${PANTHERA_ROOT}/src/Libinfo-583.0.1/Libinfo" \
    -Wno-deprecated-non-prototype \
    -Wno-strict-prototypes \
    -Wno-cast-function-type-mismatch \
    -Wno-implicit-int \
    -Wno-return-type \
    -c "${PANTHERA_ROOT}/${src}" \
    -o "${obj}"
  printf '%s\n' "${obj}"
}

archive_panthera_lib() {
  local out="$1"
  shift
  local objects=()
  local src
  local obj

  for src in "$@"; do
    obj="$(compile_panthera_obj "${src}")"
    objects+=("${obj}")
  done

  rm -f "${out}"
  ar rcs "${out}" "${objects[@]}"
  ranlib "${out}"
}

link_tool() {
  local out="$1"
  shift
  local objects=()
  local src
  local obj

  for src in "$@"; do
    obj="$(compile_obj "${src}")"
    objects+=("${obj}")
  done

  echo "Linking ${out}" >&2
    "${CC}" \
    "${objects[@]}" \
    "${zfs_tool_link_opts[@]}" \
    "${LIB_DIR}/libzfs.a" \
    "${LIB_DIR}/libzfs_core.a" \
    "${LIB_DIR}/libzutil.a" \
    "${LIB_DIR}/libefi.a" \
    "${LIB_DIR}/libshare.a" \
    "${LIB_DIR}/libdiskmgt.a" \
    "${LIB_DIR}/libtpool.a" \
    "${LIB_DIR}/libnvpair.a" \
    "${LIB_DIR}/libxdr.a" \
    "${LIB_DIR}/libuutil.a" \
    "${LIB_DIR}/libavl.a" \
    "${LIB_DIR}/libspl.a" \
    "${LIB_DIR}/libspl_assert.a" \
    "${LIB_DIR}/libzfs.a" \
    "${LIB_DIR}/libzfs_core.a" \
    "${LIB_DIR}/libzutil.a" \
    "${LIB_DIR}/libnvpair.a" \
    "${LIB_DIR}/libxdr.a" \
    "${LIB_DIR}/libuutil.a" \
    "${LIB_DIR}/libavl.a" \
    "${LIB_DIR}/libspl.a" \
    "${LIB_DIR}/libspl_assert.a" \
    "${zfs_tool_libs[@]}" \
    -o "${out}"
}

rm -rf "${OBJ_DIR}"
mkdir -p "${OBJ_DIR}"

archive_lib "${LIB_DIR}/libspl_assert.a" \
  lib/libspl/assert.c \
  lib/libspl/backtrace.c

archive_lib "${LIB_DIR}/libspl.a" \
  lib/libspl/atomic.c \
  lib/libspl/getexecname.c \
  lib/libspl/list.c \
  lib/libspl/mkdirp.c \
  lib/libspl/page.c \
  lib/libspl/timestamp.c \
  lib/libspl/os/macos/getexecname.c \
  lib/libspl/os/macos/gethostid.c \
  lib/libspl/os/macos/zone.c

archive_lib "${LIB_DIR}/libavl.a" \
  module/avl/avl.c

archive_lib "${LIB_DIR}/libnvpair.a" \
  lib/libnvpair/libnvpair.c \
  lib/libnvpair/libnvpair_json.c \
  lib/libnvpair/nvpair_alloc_system.c \
  module/nvpair/nvpair_alloc_fixed.c \
  module/nvpair/nvpair.c \
  module/nvpair/fnvpair.c

archive_panthera_lib "${LIB_DIR}/libxdr.a" \
  src/Libinfo-583.0.1/rpc.subproj/xdr.c \
  src/Libinfo-583.0.1/rpc.subproj/xdr_array.c \
  src/Libinfo-583.0.1/rpc.subproj/xdr_float.c \
  src/Libinfo-583.0.1/rpc.subproj/xdr_mem.c

archive_lib "${LIB_DIR}/libuutil.a" \
  lib/libuutil/uu_alloc.c \
  lib/libuutil/uu_avl.c \
  lib/libuutil/uu_ident.c \
  lib/libuutil/uu_list.c \
  lib/libuutil/uu_misc.c \
  lib/libuutil/uu_string.c

archive_lib "${LIB_DIR}/libzfs_core.a" \
  lib/libzfs_core/libzfs_core.c \
  lib/libzfs_core/os/macos/libzfs_core_ioctl.c

archive_lib "${LIB_DIR}/libtpool.a" \
  lib/libtpool/thread_pool.c

archive_lib "${LIB_DIR}/libefi.a" \
  lib/libefi/rdwr_efi_macos.c

archive_lib "${LIB_DIR}/libzutil.a" \
  lib/libzutil/zutil_device_path.c \
  lib/libzutil/zutil_import.c \
  lib/libzutil/zutil_nicenum.c \
  lib/libzutil/zutil_pool.c \
  lib/libzutil/os/macos/zutil_device_path_os.c \
  lib/libzutil/os/macos/zutil_import_os.c

archive_lib "${LIB_DIR}/libshare.a" \
  lib/libshare/libshare.c \
  lib/libshare/nfs.c \
  lib/libshare/os/macos/nfs.c \
  lib/libshare/os/macos/smb.c

archive_lib "${LIB_DIR}/libdiskmgt.a" \
  lib/os/macos/libdiskmgt/dm.c \
  lib/os/macos/libdiskmgt/libdiskmgt.c \
  lib/os/macos/libdiskmgt/diskutil.c \
  lib/os/macos/libdiskmgt/entry.c \
  lib/os/macos/libdiskmgt/inuse_corestorage.c \
  lib/os/macos/libdiskmgt/inuse_fs.c \
  lib/os/macos/libdiskmgt/inuse_macswap.c \
  lib/os/macos/libdiskmgt/inuse_mnt.c \
  lib/os/macos/libdiskmgt/inuse_partition.c \
  lib/os/macos/libdiskmgt/inuse_zpool.c \
  lib/os/macos/libdiskmgt/slice.c

archive_lib "${LIB_DIR}/libzfs.a" \
  lib/libzfs/libzfs_changelist.c \
  lib/libzfs/libzfs_config.c \
  lib/libzfs/libzfs_crypto.c \
  lib/libzfs/libzfs_dataset.c \
  lib/libzfs/libzfs_diff.c \
  lib/libzfs/libzfs_import.c \
  lib/libzfs/libzfs_iter.c \
  lib/libzfs/libzfs_mount.c \
  lib/libzfs/libzfs_pool.c \
  lib/libzfs/libzfs_sendrecv.c \
  lib/libzfs/libzfs_status.c \
  lib/libzfs/libzfs_util.c \
  lib/libzfs/os/macos/libzfs_dataset_os.c \
  lib/libzfs/os/macos/libzfs_getmntany.c \
  lib/libzfs/os/macos/libzfs_mount_os.c \
  lib/libzfs/os/macos/libzfs_pool_os.c \
  lib/libzfs/os/macos/libzfs_util_os.c \
  module/zcommon/cityhash.c \
  module/zcommon/zfeature_common.c \
  module/zcommon/zfs_comutil.c \
  module/zcommon/zfs_deleg.c \
  module/zcommon/zfs_fletcher.c \
  module/zcommon/zfs_fletcher_intel.c \
  module/zcommon/zfs_fletcher_superscalar.c \
  module/zcommon/zfs_fletcher_superscalar4.c \
  module/zcommon/zfs_namecheck.c \
  module/zcommon/zfs_prop.c \
  module/zcommon/zfs_valstr.c \
  module/zcommon/zpool_prop.c \
  module/zcommon/zprop_common.c

echo "Building OpenZFS zsysctl"
"${CC}" \
  "${common_cflags[@]}" \
  "${OPENZFS_SRC}/cmd/os/macos/zsysctl/zsysctl.c" \
  -o "${SBIN_DIR}/zsysctl" \
  "${zsysctl_ldflags[@]}"

bash "${PANTHERA_ROOT}/tools/audit_package.sh" "${SBIN_DIR}/zsysctl"

echo "Building OpenZFS zpool"
link_tool "${SBIN_DIR}/zpool" \
  cmd/zpool/zpool_iter.c \
  cmd/zpool/zpool_main.c \
  cmd/zpool/zpool_util.c \
  cmd/zpool/zpool_vdev.c \
  cmd/zpool/os/macos/zpool_vdev_os.c

echo "Building OpenZFS zfs"
link_tool "${SBIN_DIR}/zfs" \
  cmd/zfs/zfs_iter.c \
  cmd/zfs/zfs_main.c \
  cmd/zfs/zfs_project.c

echo "Building OpenZFS mount_zfs"
link_tool "${SBIN_DIR}/mount_zfs" cmd/os/macos/mount_zfs/mount_zfs.c

bash "${PANTHERA_ROOT}/tools/audit_package.sh" \
  "${SBIN_DIR}/zsysctl" \
  "${SBIN_DIR}/zpool" \
  "${SBIN_DIR}/zfs" \
  "${SBIN_DIR}/mount_zfs"

echo "Built OpenZFS userland tools:"
echo "  ${SBIN_DIR}/zsysctl"
echo "  ${SBIN_DIR}/zpool"
echo "  ${SBIN_DIR}/zfs"
echo "  ${SBIN_DIR}/mount_zfs"
echo "Built OpenZFS userland libraries:"
echo "  ${LIB_DIR}/libspl_assert.a"
echo "  ${LIB_DIR}/libspl.a"
echo "  ${LIB_DIR}/libavl.a"
echo "  ${LIB_DIR}/libnvpair.a"
echo "  ${LIB_DIR}/libxdr.a"
echo "  ${LIB_DIR}/libuutil.a"
echo "  ${LIB_DIR}/libzfs_core.a"
echo "  ${LIB_DIR}/libtpool.a"
echo "  ${LIB_DIR}/libefi.a"
echo "  ${LIB_DIR}/libzutil.a"
echo "  ${LIB_DIR}/libshare.a"
echo "  ${LIB_DIR}/libdiskmgt.a"
echo "  ${LIB_DIR}/libzfs.a"
