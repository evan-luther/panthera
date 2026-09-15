#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OPENZFS_SRC="$("${ROOT}/tools/prepare_openzfs_source.sh")"
BUILD_KEXT="${ROOT}/kexts/OpenIOKit/build_kext.sh"

if [[ ! -x "${BUILD_KEXT}" ]]; then
  echo "Missing kext build helper: ${BUILD_KEXT}" >&2
  exit 1
fi

SPL_SRCS=(
  ../../avl/avl.c
  spl/spl-atomic.c
  spl/spl-condvar.c
  spl/spl-cred.c
  spl/spl-debug.c
  spl/spl-ddi.c
  spl/spl-err.c
  spl/spl-kmem.c
  spl/spl-kstat.c
  spl/spl-list.c
  spl/spl-mutex.c
  spl/spl-osx.c
  spl/spl-policy.c
  spl/spl-proc.c
  spl/spl-processor.c
  spl/spl-proc_list.c
  spl/spl-qsort.c
  spl/spl-rwlock.c
  spl/spl-seg_kmem.c
  spl/spl-taskq.c
  spl/spl-thread.c
  spl/spl-time.c
  spl/spl-tsd.c
  spl/spl-uio.c
  spl/spl-vmem.c
  spl/spl-vnode.c
  spl/spl-xdr.c
)

"${BUILD_KEXT}" spl "${OPENZFS_SRC}/module/os/macos" \
  --module-start spl_start \
  --module-stop spl_stop \
  --no-link-fallback \
  --define _KERNEL \
  --define __KERNEL__ \
  --define PANTHERA_SPL_KEXT=1 \
  --force-include "${ROOT}/kexts/zfs/compat/panthera_spl_compat.h" \
  --prepend-include "${ROOT}/kexts/zfs/compat" \
  --prepend-include "${OPENZFS_SRC}/include/os/macos/spl" \
  --prepend-include "${OPENZFS_SRC}/include/os/macos/zfs" \
  --prepend-include "${OPENZFS_SRC}/include" \
  --extra-include "${OPENZFS_SRC}/module/os/macos/spl" \
  --unexported-symbol _cmn_err \
  --unexported-symbol _crgetuid \
  --unexported-symbol _ddi_create_minor_node \
  --unexported-symbol _ddi_remove_minor_node \
  --unexported-symbol _max_ncpus \
  --unexported-symbol _vmem_create \
  --unexported-symbol _vmem_destroy \
  --unexported-symbol _vnode_iocount \
  "${SPL_SRCS[@]}"
