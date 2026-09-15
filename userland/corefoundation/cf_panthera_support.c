/*
 * cf_panthera_support.c — Implementations of symbols CF needs that aren't in the Panthera sysroot.
 * These are linked directly into libCoreFoundation.dylib.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <mach/mach.h>
#include <mach-o/loader.h>
#include <mach-o/fat.h>
#include <mach-o/arch.h>
#include <math.h>
#include <dlfcn.h>
#include <errno.h>

/* ── OSAtomic ── */

bool OSAtomicCompareAndSwap64Barrier(int64_t oldValue, int64_t newValue,
    volatile int64_t *theValue) {
    return __sync_bool_compare_and_swap(theValue, oldValue, newValue);
}

typedef int32_t OSSpinLock;

void OSSpinLockLock(volatile OSSpinLock *lock) {
    while (!__sync_bool_compare_and_swap(lock, 0, 1)) {
        while (*lock != 0) {
            __asm__ volatile("pause" ::: "memory");
        }
    }
}

void OSSpinLockUnlock(volatile OSSpinLock *lock) {
    __sync_lock_release(lock);
}

/* ── dyld APIs ── */

int _NSGetExecutablePath(char *buf, uint32_t *bufsize) {
    /* Return the executable path. Panthera uses a simple approach. */
    const char *path = "/usr/bin/unknown";
    extern const char ***_NSGetArgv(void) __attribute__((weak));
    if (_NSGetArgv && *_NSGetArgv() && (*_NSGetArgv())[0]) {
        path = (*_NSGetArgv())[0];
    }
    uint32_t len = (uint32_t)strlen(path) + 1;
    if (len > *bufsize) {
        *bufsize = len;
        return -1;
    }
    memcpy(buf, path, len);
    return 0;
}

/* dyld image enumeration — report the main executable as image 0 */
uint32_t _dyld_image_count(void) {
    return 1;
}

const char *_dyld_get_image_name(uint32_t image_index) {
    if (image_index == 0) {
        static char path[1024];
        static bool resolved = false;
        if (!resolved) {
            extern const char ***_NSGetArgv(void) __attribute__((weak));
            if (_NSGetArgv && *_NSGetArgv() && (*_NSGetArgv())[0])
                strlcpy(path, (*_NSGetArgv())[0], sizeof(path));
            else
                strlcpy(path, "/usr/bin/unknown", sizeof(path));
            resolved = true;
        }
        return path;
    }
    return NULL;
}

intptr_t _dyld_get_image_vmaddr_slide(uint32_t image_index) {
    (void)image_index;
    return 0;
}

/* ── Mach-O section lookup ── */

const struct section_64 *getsectbynamefromheader_64(
    const struct mach_header_64 *mhp, const char *segname, const char *sectname) {
    if (!mhp) return NULL;
    const uint8_t *cmd = (const uint8_t *)mhp + sizeof(struct mach_header_64);
    for (uint32_t i = 0; i < mhp->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cmd;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *)cmd;
            if (strncmp(seg->segname, segname, 16) == 0) {
                const struct section_64 *sect = (const struct section_64 *)(cmd + sizeof(struct segment_command_64));
                for (uint32_t j = 0; j < seg->nsects; j++) {
                    if (strncmp(sect[j].sectname, sectname, 16) == 0) {
                        return &sect[j];
                    }
                }
            }
        }
        cmd += lc->cmdsize;
    }
    return NULL;
}

const struct segment_command_64 *getsegbyname(const char *segname) {
    extern const struct mach_header_64 *_NSGetMachExecuteHeader(void) __attribute__((weak));
    if (!_NSGetMachExecuteHeader) return NULL;
    const struct mach_header_64 *mhp = _NSGetMachExecuteHeader();
    if (!mhp) return NULL;
    const uint8_t *cmd = (const uint8_t *)mhp + sizeof(struct mach_header_64);
    for (uint32_t i = 0; i < mhp->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cmd;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *)cmd;
            if (strncmp(seg->segname, segname, 16) == 0)
                return seg;
        }
        cmd += lc->cmdsize;
    }
    return NULL;
}

/* ── NX (fat binary) APIs ── */

const NXArchInfo *NXGetLocalArchInfo(void) {
    static const NXArchInfo info = {
        .name = "x86_64",
        .cputype = CPU_TYPE_X86_64,
        .cpusubtype = CPU_SUBTYPE_X86_64_ALL,
        .byteorder = NX_LittleEndian,
        .description = "Intel x86-64"
    };
    return &info;
}

struct fat_arch *NXFindBestFatArch(cpu_type_t cputype, cpu_subtype_t cpusubtype,
    struct fat_arch *fat_archs, uint32_t nfat_archs) {
    for (uint32_t i = 0; i < nfat_archs; i++) {
        if (fat_archs[i].cputype == cputype) {
            return &fat_archs[i];
        }
    }
    (void)cpusubtype;
    return NULL;
}

/* ── NSSearchPath ── */
/* Minimal implementation for CoreFoundation's directory search */

enum {
    NSApplicationDirectory = 1,
    NSLibraryDirectory = 5,
    NSAllApplicationsDirectory = 100,
    NSAllLibrariesDirectory = 101,
};

enum {
    NSUserDomainMask = 1,
    NSLocalDomainMask = 2,
    NSSystemDomainMask = 8,
    NSAllDomainsMask = 0x0ffff,
};

typedef unsigned int NSSearchPathEnumerationState;

NSSearchPathEnumerationState NSStartSearchPathEnumeration(
    unsigned int dir, unsigned int domainMask) {
    (void)dir;
    (void)domainMask;
    /* Return state 1 to start, paths will be filled in by NSGetNextSearchPathEnumeration */
    return 1;
}

NSSearchPathEnumerationState NSGetNextSearchPathEnumeration(
    NSSearchPathEnumerationState state, char *path) {
    if (state == 1) {
        strlcpy(path, "/Library", 1024);
        return 2;
    } else if (state == 2) {
        strlcpy(path, "/System/Library", 1024);
        return 0; /* done */
    }
    return 0;
}

/* ── vproc (launchd transaction) stubs ── */

typedef void *vproc_transaction_t;

vproc_transaction_t _vproc_transaction_begin(void) {
    return (vproc_transaction_t)1;
}

void _vproc_transaction_end(vproc_transaction_t t) {
    (void)t;
}

int _vproc_transaction_count(void) {
    return 0;
}

int _vproc_transaction_try_exit(int status) {
    (void)status;
    return -1;
}

void _vproc_transactions_enable(void) {
}

/* ── ASL (Apple System Log) stubs ── */

void asl_close(void *asl) {
    (void)asl;
}

void asl_free(void *msg) {
    (void)msg;
}

/* ── Bootstrap ── */

const char *bootstrap_strerror(int r) {
    (void)r;
    return "bootstrap error";
}

/* ── dlopen_preflight ── */

bool dlopen_preflight(const char *path) {
    (void)path;
    return false;
}

/* ── gethostuuid ── */

int gethostuuid(unsigned char *uuid, const struct timespec *timeout) {
    (void)timeout;
    memset(uuid, 0, 16);
    /* Generate a deterministic host UUID based on "Panthera" */
    uuid[0] = 'P'; uuid[1] = 'A'; uuid[2] = 'N'; uuid[3] = 'T';
    uuid[4] = 'H'; uuid[5] = 'E'; uuid[6] = 'R'; uuid[7] = 'A';
    return 0;
}

/* ── Mach VM ── */

kern_return_t mach_make_memory_entry_64(
    mach_port_t target_task, uint64_t *size, uint64_t offset,
    int permission, mach_port_t *object_handle, mach_port_t parent_entry) {
    (void)target_task; (void)size; (void)offset;
    (void)permission; (void)object_handle; (void)parent_entry;
    return KERN_FAILURE;
}

kern_return_t mach_vm_region(
    mach_port_t target_task, uint64_t *address, uint64_t *size,
    int flavor, void *info, unsigned int *infoCnt, mach_port_t *object_name) {
    (void)target_task; (void)address; (void)size;
    (void)flavor; (void)info; (void)infoCnt; (void)object_name;
    return KERN_FAILURE;
}

/* ── scalbn ── */

double scalbn(double x, int n) {
    /* scalbn(x, n) = x * 2^n — use builtin */
    return __builtin_scalbn(x, n);
}

/* ── pthread helpers ── */

int pthread_main_np(void) {
    return 1;
}

mach_port_t pthread_mach_thread_np(pthread_t t) {
    (void)t;
    return mach_thread_self();
}

int pthread_threadid_np(pthread_t t, uint64_t *thread_id) {
    (void)t;
    if (thread_id) *thread_id = 1;
    return 0;
}

