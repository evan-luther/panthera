#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <mach-o/loader.h>
#include <pwd.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#undef FLOCKFILE
#undef FUNLOCKFILE

#ifndef _RLIMIT_POSIX_FLAG
#define _RLIMIT_POSIX_FLAG 0
#endif

extern int __execve(const char *, char *const *, char *const *);
extern int __fchmod(int, mode_t);
extern int __fcntl(int, int, long);
extern int __fork(void);
extern int _mach_fork_child(void);
extern void _pthread_atfork_prepare(void);
extern void _pthread_atfork_parent(void);
extern void _pthread_atfork_child(void);
extern void _init_clock_port(void);
extern pid_t __getpid(void);
extern int __getrlimit(int, struct rlimit *);
extern int __gettimeofday(struct timeval *, void *);
extern int __ioctl(int, unsigned long, void *);
extern int __kill(int, int);
extern off_t __lseek(int, off_t, int);
extern void *__mmap(void *, size_t, int, int, int, off_t);
extern int __munmap(void *, size_t);
extern int __open(const char *, int, int);
extern int __pipe(int[2]);
extern int __rename(const char *, const char *);
extern int __rmdir(const char *);
extern int __select(int, void *, void *, void *, void *);
extern int __setrlimit(int, const struct rlimit *);
extern int __platform_sigaction(int, const struct sigaction *, struct sigaction *);
extern int __unlink(const char *);
extern pid_t __wait4(pid_t, int *, int, void *);

extern void _platform_bzero(void *, size_t);
extern void *_platform_memchr(const void *, int, size_t);
extern int _platform_memcmp(const void *, const void *, size_t);
extern void *_platform_memmove(void *, const void *, size_t);
extern void *_platform_memset(void *, int, size_t);
extern void _platform_memset_pattern16(void *, const void *, size_t);
extern char *_platform_strchr(const char *, int);
extern int _platform_strcmp(const char *, const char *);
extern char *_platform_strcpy(char *, const char *);
extern size_t _platform_strlcpy(char *, const char *, size_t);
extern size_t _platform_strlen(const char *);
extern int _platform_strncmp(const char *, const char *, size_t);
extern char *_platform_strncpy(char *, const char *, size_t);
extern char *_platform_strstr(const char *, const char *);

int NXArgc;
char **NXArgv;
char **environ;
char *__progname = "unknown";
int __isthreaded;

static int panthera_errno_storage;
static int panthera_pw_iterating;

static char *panthera_wheel_members[] = { "root", NULL };
static struct passwd panthera_root_pw = {
    .pw_name = "root",
    .pw_passwd = "*",
    .pw_uid = 0,
    .pw_gid = 0,
    .pw_change = 0,
    .pw_class = "",
    .pw_gecos = "System Administrator",
    .pw_dir = "/var/root",
    .pw_shell = "/bin/sh",
    .pw_expire = 0,
};
static struct group panthera_root_gr = {
    .gr_name = "wheel",
    .gr_passwd = "*",
    .gr_gid = 0,
    .gr_mem = panthera_wheel_members,
};

int *
_NSGetArgc(void)
{
    return &NXArgc;
}

char ***
_NSGetArgv(void)
{
    return &NXArgv;
}

char ***
_NSGetEnviron(void)
{
    return &environ;
}

char **
_NSGetProgname(void)
{
    return &__progname;
}

const struct mach_header_64 *
_NSGetMachExecuteHeader(void)
{
    extern struct mach_header_64 _mh_execute_header;
    return &_mh_execute_header;
}

int
NSVersionOfLinkTimeLibrary(const char *name)
{
    (void)name;
    return -1;
}

int
NSVersionOfRunTimeLibrary(const char *name)
{
    (void)name;
    return -1;
}

int *
__error(void)
{
    return &panthera_errno_storage;
}

long
cerror(int err)
{
    panthera_errno_storage = err;
    return -1;
}

long
cerror_nocancel(int err)
{
    panthera_errno_storage = err;
    return -1;
}

void
panthera_FLOCKFILE(FILE *fp) __asm("_FLOCKFILE");

void
panthera_FLOCKFILE(FILE *fp)
{
    (void)fp;
}

void
panthera_FUNLOCKFILE(FILE *fp) __asm("_FUNLOCKFILE");

void
panthera_FUNLOCKFILE(FILE *fp)
{
    (void)fp;
}

int32_t
panthera_OSAtomicDecrement32Barrier(volatile int32_t *value) __asm("_OSAtomicDecrement32Barrier");

int32_t
panthera_OSAtomicDecrement32Barrier(volatile int32_t *value)
{
    return __sync_sub_and_fetch(value, 1);
}

int32_t
panthera_OSAtomicIncrement32Barrier(volatile int32_t *value) __asm("_OSAtomicIncrement32Barrier");

int32_t
panthera_OSAtomicIncrement32Barrier(volatile int32_t *value)
{
    return __sync_add_and_fetch(value, 1);
}

int32_t
OSAtomicDecrement32(volatile int32_t *value)
{
    return __sync_sub_and_fetch(value, 1);
}

int32_t
OSAtomicIncrement32(volatile int32_t *value)
{
    return __sync_add_and_fetch(value, 1);
}

int32_t
OSAtomicAdd32Barrier(int32_t amount, volatile int32_t *value)
{
    return __sync_add_and_fetch(value, amount);
}

int
OSAtomicCompareAndSwap32Barrier(int32_t oldval, int32_t newval, volatile int32_t *value)
{
    return __sync_bool_compare_and_swap(value, oldval, newval);
}

int
panthera_OSAtomicCompareAndSwapPtrBarrier(void *oldval, void *newval, void * volatile *value)
    __asm("_OSAtomicCompareAndSwapPtrBarrier");

int
panthera_OSAtomicCompareAndSwapPtrBarrier(void *oldval, void *newval, void * volatile *value)
{
    return __sync_bool_compare_and_swap(value, oldval, newval);
}

void
OSMemoryBarrier(void)
{
    __sync_synchronize();
}

int
__darwin_check_fd_set_overflow(int fd, const void *set, int is_unlimited)
{
    (void)set;
    if (fd < 0) return 0;
    if (is_unlimited) return 1;
    return fd < 1024;
}

void
__bzero(void *s, size_t n)
{
    _platform_bzero(s, n);
}

void
bzero(void *s, size_t n)
{
    _platform_bzero(s, n);
}

double
fmod(double x, double y)
{
    return __builtin_fmod(x, y);
}

float
fmodf(float x, float y)
{
    return __builtin_fmodf(x, y);
}


float
powf(float x, float y)
{
    return __builtin_powf(x, y);
}

int
execve(const char *path, char *const argv[], char *const envp[])
{
    return __execve(path, argv, envp);
}

int
fchmod(int fd, mode_t mode)
{
    return __fchmod(fd, mode);
}

int
fcntl(int fd, int cmd, ...)
{
    va_list ap;
    long arg;

    va_start(ap, cmd);
    arg = va_arg(ap, long);
    va_end(ap);
    return __fcntl(fd, cmd, arg);
}

int
fork(void)
{
    _pthread_atfork_prepare();
    int pid = __fork();
    if (pid == 0) {
        /* Mach ports and pthread identity belong to the new task. */
        _mach_fork_child();
        _pthread_atfork_child();
        _init_clock_port();
    } else {
        int saved_errno = errno;
        _pthread_atfork_parent();
        errno = saved_errno;
    }
    return pid;
}


pid_t
getpid(void)
{
    return __getpid();
}

int
getrlimit(int resource, struct rlimit *rlp)
{
    return __getrlimit(resource | _RLIMIT_POSIX_FLAG, rlp);
}

int
gettimeofday(struct timeval *tv, void *tz)
{
    return __gettimeofday(tv, tz);
}

int
ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    void *arg;

    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);
    return __ioctl(fd, request, arg);
}

int
kill(int pid, int sig)
{
    return __kill(pid, sig);
}

int
killpg(int pgrp, int sig)
{
    return __kill(-pgrp, sig);
}

off_t
lseek(int fd, off_t offset, int whence)
{
    return __lseek(fd, offset, whence);
}

void *
memchr(const void *s, int c, size_t n)
{
    return _platform_memchr(s, c, n);
}

int
memcmp(const void *a, const void *b, size_t n)
{
    return _platform_memcmp(a, b, n);
}

void *
memcpy(void *dst, const void *src, size_t n)
{
    return _platform_memmove(dst, src, n);
}

void *
memmove(void *dst, const void *src, size_t n)
{
    return _platform_memmove(dst, src, n);
}

void *
memset(void *s, int c, size_t n)
{
    return _platform_memset(s, c, n);
}

void
memset_pattern16(void *dst, const void *pattern16, size_t len)
{
    _platform_memset_pattern16(dst, pattern16, len);
}

void *
mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset)
{
    return __mmap(addr, len, prot, flags, fd, offset);
}

int
munmap(void *addr, size_t len)
{
    return __munmap(addr, len);
}

int
open(const char *path, int oflag, ...)
{
    mode_t mode;

    mode = 0;
    if (oflag & O_CREAT) {
        va_list ap;

        va_start(ap, oflag);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    return __open(path, oflag, mode);
}

int
pipe(int fildes[2])
{
    return __pipe(fildes);
}

int
rename(const char *oldpath, const char *newpath)
{
    return __rename(oldpath, newpath);
}

int
rmdir(const char *path)
{
    return __rmdir(path);
}

int
setrlimit(int resource, const struct rlimit *rlp)
{
    return __setrlimit(resource | _RLIMIT_POSIX_FLAG, rlp);
}

int
sigaction(int sig, const struct sigaction *act, struct sigaction *oact)
{
    return __platform_sigaction(sig, act, oact);
}


char *
strchr(const char *s, int c)
{
    return _platform_strchr(s, c);
}

int
strcmp(const char *a, const char *b)
{
    return _platform_strcmp(a, b);
}

char *
strcpy(char *dst, const char *src)
{
    return _platform_strcpy(dst, src);
}

size_t
strlcpy(char *dst, const char *src, size_t size)
{
    return _platform_strlcpy(dst, src, size);
}

size_t
strlen(const char *s)
{
    return _platform_strlen(s);
}

int
strncmp(const char *a, const char *b, size_t n)
{
    return _platform_strncmp(a, b, n);
}

char *
strncpy(char *dst, const char *src, size_t n)
{
    return _platform_strncpy(dst, src, n);
}

char *
strstr(const char *haystack, const char *needle)
{
    return _platform_strstr(haystack, needle);
}

int
unlink(const char *path)
{
    return __unlink(path);
}

pid_t
waitpid(pid_t pid, int *status, int options)
{
    return __wait4(pid, status, options, NULL);
}

pid_t
wait(int *status)
{
    return __wait4(-1, status, 0, NULL);
}
