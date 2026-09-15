/*
 * ls_probe.c — diagnose opendir failure
 * Found: opendir() fails because __opendir_common calls fstatfs
 * when kern.secure_kernel sysctl is missing
 */
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <dirent.h>
#include <sys/mount.h>
#include <sys/sysctl.h>

static void w(const char *s) { write(1, s, strlen(s)); }
static void wn(int n) { char b[16]; snprintf(b,sizeof(b),"%d",n); w(b); }
static void wx(unsigned n) { char b[16]; snprintf(b,sizeof(b),"0x%x",n); w(b); }

int main(void) {
    w("=== opendir diagnosis ===\n");

    /* Test 1: kern.secure_kernel sysctl */
    int secval = -1;
    size_t seclen = sizeof(secval);
    errno = 0;
    int r = sysctlbyname("kern.secure_kernel", &secval, &seclen, NULL, 0);
    w("kern.secure_kernel: ret="); wn(r);
    w(" val="); wn(secval);
    w(" errno="); wn(errno); w("\n");

    /* Test 2: fstatfs on root directory fd */
    int fd = open("/", O_RDONLY, 0);
    if (fd < 0) { w("open / failed\n"); return 1; }
    struct statfs stfs;
    memset(&stfs, 0, sizeof(stfs));
    errno = 0;

    r = fstatfs(fd, &stfs);
    w("fstatfs64(/): ret="); wn(r);
    w(" errno="); wn(errno);
    w(" f_flags="); wx(stfs.f_flags);
    w(" f_type="); wn(stfs.f_type);
    w(" f_fstypename="); w(stfs.f_fstypename);
    w("\n");

    /* Check MNT_UNION (0x20) */
    if (stfs.f_flags & 0x20)
        w("MNT_UNION IS SET\n");
    else
        w("MNT_UNION not set\n");

    /* Test 3: opendir after testing */
    errno = 0;
    DIR *dp = opendir("/");
    w("opendir(/): ");
    if (dp) {
        w("OK\n");
        struct dirent *de;
        int count = 0;
        while ((de = readdir(dp)) != NULL) count++;
        closedir(dp);
        w("entries="); wn(count); w("\n");
    } else {
        w("FAILED errno="); wn(errno); w("\n");
    }

    /* Test 4: raw getdirentries64 (known working) */
    errno = 0;
    extern int __getdirentries64(int, char *, int, long *);
    char buf[4096];
    long basep = 0;
    int nread = __getdirentries64(fd, buf, sizeof(buf), &basep);
    w("getdirentries64: nread="); wn(nread); w("\n");
    if (nread > 0) {
        int count = 0;
        long pos = 0;
        while (pos < nread) {
            struct dirent *dp2 = (struct dirent *)(buf + pos);
            if (dp2->d_reclen == 0) break;
            count++;
            pos += dp2->d_reclen;
        }
        w("raw entries="); wn(count); w("\n");
    }

    close(fd);
    w("=== done ===\n");
    return 0;
}
