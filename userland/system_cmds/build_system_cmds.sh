#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRCDIR="${PANTHERA_ROOT}/src/system_cmds"
BIN_DIR="${PANTHERA_ROOT}/userland/system_cmds/bin"
STTY_DIR="${PANTHERA_ROOT}/userland/system_cmds/stty"
SHUTDOWN_DIR="${PANTHERA_ROOT}/userland/system_cmds/shutdown"
PS_DIR="${PANTHERA_ROOT}/userland/system_cmds/ps"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"

CC="$(xcrun -find clang)"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
TARGET="x86_64-apple-darwin23.0"
CFLAGS="-target ${TARGET} -mmacosx-version-min=14.0 -isysroot ${SYSROOT} -isystem ${SDKROOT}/usr/include -O2"
LDFLAGS="-lSystem"

mkdir -p "${BIN_DIR}"

if [[ "${PANTHERA_FORCE_REBUILD:-0}" != "1" \
   && -x "${BIN_DIR}/sysctl" \
   && -x "${BIN_DIR}/stty" \
   && -x "${BIN_DIR}/shutdown" \
   && -x "${BIN_DIR}/ps" ]]; then
    echo "system_cmds already built"
    exit 0
fi

built=()
skipped=()

build_one() {
    local name="$1" src="$2" extra_flags="${3:-}"
    if "${CC}" ${CFLAGS} ${extra_flags} -o "${BIN_DIR}/${name}" ${src} ${LDFLAGS} 2>/dev/null; then
        built+=("${name}")
    else
        skipped+=("${name}")
    fi
}

build_required() {
    local name="$1" src="$2" extra_flags="${3:-}"
    if "${CC}" ${CFLAGS} ${extra_flags} -o "${BIN_DIR}/${name}" ${src} ${LDFLAGS}; then
        built+=("${name}")
    else
        echo "ERROR: Failed to build required utility ${name}" >&2
        exit 1
    fi
}

# Core required utilities from system_cmds source
build_required sysctl "${SRCDIR}/sysctl/sysctl.c" "-w"

# Simple single-file utilities
build_one dmesg    "${SRCDIR}/dmesg/dmesg.c"
build_one sync     "${SRCDIR}/sync/sync.c"
build_one nologin  "${SRCDIR}/nologin/nologin.c"
build_one mkfile   "${SRCDIR}/mkfile/mkfile.c"
build_one hostinfo "${SRCDIR}/hostinfo/hostinfo.c"
build_one vm_stat  "${SRCDIR}/vm_stat/vm_stat.c"
build_one wait4path "${SRCDIR}/wait4path/wait4path.c"
build_one vifs     "${SRCDIR}/vifs/vifs.c" "-w"
build_one newgrp   "${SRCDIR}/newgrp/newgrp.c"

# Reuse launchd's generated RPC client; normal shutdown has exactly one owner.
LAUNCHD_OBJ="${PANTHERA_ROOT}/userland/launchd/obj"
if [[ ! -f "${LAUNCHD_OBJ}/mig_gen_jobUser.o" || ! -f "${LAUNCHD_OBJ}/mig_gen/job.h" ]]; then
    bash "${PANTHERA_ROOT}/userland/launchd/build_launchd.sh"
fi
cat > /tmp/panthera_reboot.c <<'CEOF'
#include <sys/reboot.h>
#include <mach/mach.h>
#include "vproc_internal.h"
#include "job.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    const char *progname = strrchr(argv[0], '/');
    progname = progname ? progname + 1 : argv[0];
    int howto = 0;
    if (strcmp(progname, "halt") == 0) howto = RB_HALT;
    int ch;
    while ((ch = getopt(argc, argv, "lnq")) != -1) {
        switch (ch) {
            case 'l': break;
            case 'n': howto |= RB_NOSYNC; break;
            case 'q': howto |= RB_QUICK; break;
            default: return 2;
        }
    }
    if (optind != argc) {
        fprintf(stderr, "usage: %s [-lnq]\n", progname);
        return 2;
    }
    if (geteuid() != 0) {
        fprintf(stderr, "%s: must be root\n", progname);
        return 1;
    }
    if (howto & RB_QUICK) {
        if (!(howto & RB_NOSYNC)) sync();
        reboot(howto);
        perror("reboot");
        return 1;
    }
    mach_port_t self = mach_task_self(), bootstrap = MACH_PORT_NULL, root = MACH_PORT_NULL;
    kern_return_t result = task_get_bootstrap_port(self, &bootstrap);
    if (result == KERN_SUCCESS)
        result = vproc_mig_get_root_bootstrap(bootstrap, &root);
    if (MACH_PORT_VALID(bootstrap)) mach_port_deallocate(self, bootstrap);
    if (result == KERN_SUCCESS)
        result = vproc_mig_reboot2(root, howto);
    if (MACH_PORT_VALID(root)) mach_port_deallocate(self, root);
    if (result == KERN_SUCCESS) return 0;
    fprintf(stderr, "%s: launchd reboot request failed: %s\n", progname, mach_error_string(result));
    return 1;
}
CEOF
build_required reboot /tmp/panthera_reboot.c \
    "-I${LAUNCHD_OBJ}/mig_gen -I${PANTHERA_ROOT}/src/launchd-842.92.1/liblaunch ${LAUNCHD_OBJ}/mig_gen_jobUser.o -Wl,-dead_strip"
cp "${BIN_DIR}/reboot" "${BIN_DIR}/halt" 2>/dev/null && built+=(halt) || true

# Shutdown utility (adapted from upstream Apple/BSD system_cmds)
if "${CC}" ${CFLAGS} -I"${SHUTDOWN_DIR}" -o "${BIN_DIR}/shutdown" "${SHUTDOWN_DIR}/shutdown.c" ${LDFLAGS}; then
    built+=(shutdown)
else
    echo "ERROR: Failed to build required utility shutdown" >&2
    exit 1
fi

# POSIX/Darwin stty (adv_cmds)
if "${CC}" ${CFLAGS} -Wno-unused-const-variable -I"${STTY_DIR}" -o "${BIN_DIR}/stty" "${STTY_DIR}"/*.c ${LDFLAGS}; then
    built+=(stty)
else
    echo "ERROR: Failed to build required utility stty" >&2
    exit 1
fi

# Darwin ps (adv_cmds)
if "${CC}" ${CFLAGS} -DPS_ENTITLED=1 -Wno-deprecated-non-prototype -I"${PS_DIR}" -o "${BIN_DIR}/ps" "${PS_DIR}"/*.c ${LDFLAGS}; then
    built+=(ps)
else
    echo "ERROR: Failed to build required utility ps" >&2
    exit 1
fi


echo "Built: ${built[*]}"
[[ ${#skipped[@]} -gt 0 ]] && echo "Skipped: ${skipped[*]}" || true

bash "${PANTHERA_ROOT}/tools/audit_package.sh" "${BIN_DIR}"/*

echo "Done: ${#built[@]} system_cmds built"
