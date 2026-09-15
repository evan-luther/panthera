/*
 * panthera_resolve_wave3.c — Third wave: symbols needed by dropbear SSH,
 * user management, and networking utilities.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

/* Syslog ownership moved to libsystem_c.dylib in Phase 8. */

/* ═══════════════════════════════════════════════════════════
 * environ — global environment pointer
 * ═══════════════════════════════════════════════════════════ */

/* Initialize to NULL — programs must handle this.
 * Can't use a static pointer because it needs relocation. */
char **environ = (char **)0;

/* ═══════════════════════════════════════════════════════════
 * Network name resolution stubs
 * (Minimal — return NODATA for now, real DNS needs networking)
 * ═══════════════════════════════════════════════════════════ */

struct hostent {
    char *h_name;
    char **h_aliases;
    int h_addrtype;
    int h_length;
    char **h_addr_list;
};

static struct hostent _localhost = {
    "localhost", (char **)0, 2 /* AF_INET */, 4, (char **)0
};

struct hostent *gethostbyname(const char *name) {
    (void)name;
    return &_localhost;
}

struct hostent *gethostbyaddr(const void *addr, unsigned int len, int type) {
    (void)addr; (void)len; (void)type;
    return &_localhost;
}

int gethostname(char *name, unsigned long namelen) {
    const char *hn = "panthera";
    unsigned long i = 0;
    while (hn[i] && i < namelen - 1) { name[i] = hn[i]; i++; }
    name[i] = 0;
    return 0;
}

int sethostname(const char *name, int namelen) {
    (void)name; (void)namelen;
    return 0;
}

struct servent {
    char *s_name;
    char **s_aliases;
    int s_port;
    char *s_proto;
};

struct servent *getservbyname(const char *name, const char *proto) {
    (void)name; (void)proto;
    return (struct servent *)0;
}

struct servent *getservbyport(int port, const char *proto) {
    (void)port; (void)proto;
    return (struct servent *)0;
}

/* ═══════════════════════════════════════════════════════════
 * getpwuid_r / getpwnam_r — reentrant password database
 * ═══════════════════════════════════════════════════════════ */

struct passwd {
    char *pw_name;
    char *pw_passwd;
    unsigned int pw_uid;
    unsigned int pw_gid;
    long pw_change;
    char *pw_class;
    char *pw_gecos;
    char *pw_dir;
    char *pw_shell;
    long pw_expire;
};

static struct passwd _root_pw2 = {
    "root", "*", 0, 0, 0, "", "System Administrator", "/var/root", "/bin/sh", 0
};

int getpwnam_r(const char *name, struct passwd *pwd, char *buf,
               unsigned long bufsize, struct passwd **result) {
    (void)buf; (void)bufsize;
    /* Simple single-user: only root */
    if (name && name[0] == 'r' && name[1] == 'o' && name[2] == 'o' && name[3] == 't' && name[4] == 0) {
        *pwd = _root_pw2;
        *result = pwd;
        return 0;
    }
    *result = (struct passwd *)0;
    return 0; /* not found, but no error */
}

/* ═══════════════════════════════════════════════════════════
 * Socket functions (stubs until networking works)
 * ═══════════════════════════════════════════════════════════ */

extern int *panthera_errno_fn(void) __asm__("_errno");

static long _sc6(long num, long a1, long a2, long a3, long a4, long a5, long a6) {
    long ret;
    long carry;
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    register long r9  __asm__("r9")  = a6;
    __asm__ volatile(
        "syscall\n\t"
        "jnc 1f\n\t"
        "movq $1, %[cf]\n\t"
        "jmp 2f\n"
        "1: movq $0, %[cf]\n"
        "2:"
        : "=a"(ret), [cf]"=r"(carry)
        : "a"(num | 0x2000000), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory", "cc");
    if (carry) {
        *panthera_errno_fn() = (int)ret;
        return -1;
    }
    return ret;
}

int bind(int sockfd, const void *addr, unsigned int addrlen) {
    return (int)_sc6(104 /* SYS_bind */, sockfd, (long)addr, addrlen, 0, 0, 0);
}

int listen(int sockfd, int backlog) {
    return (int)_sc6(106 /* SYS_listen */, sockfd, backlog, 0, 0, 0, 0);
}

int accept(int sockfd, void *addr, unsigned int *addrlen) {
    return (int)_sc6(30 /* SYS_accept */, sockfd, (long)addr, (long)addrlen, 0, 0, 0);
}

/* send: moved to panthera_extra_stubs.c to use sendto with proper errno */

/* recv: moved to panthera_extra_stubs.c to use recvfrom with proper errno */

/* connect: removed — use libsystem_kernel _connect$NOCANCEL via alias */

/* setsockopt: removed — use libsystem_kernel _setsockopt directly */

/* ═══════════════════════════════════════════════════════════
 * PTY functions (needed by SSH for shell sessions)
 * ═══════════════════════════════════════════════════════════ */

int openpty(int *amaster, int *aslave, char *name,
            const void *termp, const void *winp) {
    (void)termp; (void)winp;
    /* Use posix_openpt + grantpt + unlockpt + ptsname */
    int master = (int)_sc6(5 /* SYS_open */, (long)"/dev/ptmx", 2 /* O_RDWR */, 0, 0, 0, 0);
    if (master < 0) return -1;

    /* grantpt and unlockpt are usually no-ops on modern systems */
    /* Get slave name: /dev/ttysXXX */
    /* For now, try a fixed slave */
    int slave = (int)_sc6(5, (long)"/dev/ttyp0", 2, 0, 0, 0, 0);
    if (slave < 0) {
        _sc6(6 /* SYS_close */, master, 0, 0, 0, 0, 0);
        return -1;
    }

    *amaster = master;
    *aslave = slave;
    if (name) {
        const char *n = "/dev/ttyp0";
        int i = 0;
        while (n[i]) { name[i] = n[i]; i++; }
        name[i] = 0;
    }
    return 0;
}

int forkpty(int *amaster, char *name,
            const void *termp, const void *winp) {
    int master, slave;
    if (openpty(&master, &slave, name, termp, winp) < 0) return -1;

    long pid = _sc6(2 /* SYS_fork */, 0, 0, 0, 0, 0, 0);
    if (pid < 0) {
        _sc6(6, master, 0, 0, 0, 0, 0);
        _sc6(6, slave, 0, 0, 0, 0, 0);
        return -1;
    }
    if (pid == 0) {
        /* Child */
        _sc6(6, master, 0, 0, 0, 0, 0);
        _sc6(147 /* SYS_setsid */, 0, 0, 0, 0, 0, 0);
        _sc6(90 /* SYS_dup2 */, slave, 0, 0, 0, 0, 0);
        _sc6(90, slave, 1, 0, 0, 0, 0);
        _sc6(90, slave, 2, 0, 0, 0, 0);
        if (slave > 2) _sc6(6, slave, 0, 0, 0, 0, 0);
        return 0;
    }
    /* Parent */
    *amaster = master;
    _sc6(6, slave, 0, 0, 0, 0, 0);
    return (int)pid;
}

/* ═══════════════════════════════════════════════════════════
 * User management support
 * ═══════════════════════════════════════════════════════════ */

/* getlogin / setlogin */
static char _login_name[256] = "root";

char *getlogin(void) {
    return _login_name;
}

int getlogin_r(char *name, unsigned long namesize) {
    unsigned long i = 0;
    while (_login_name[i] && i < namesize - 1) { name[i] = _login_name[i]; i++; }
    name[i] = 0;
    return 0;
}

/* crypt — simple password hashing stub
 * Real crypt() would use DES/MD5/SHA/bcrypt. We use a trivial comparison. */
char *crypt(const char *key, const char *salt) {
    static char result[128];
    /* Just return salt + key hash for matching purposes */
    int i = 0;
    if (salt) while (salt[i] && i < 2) { result[i] = salt[i]; i++; }
    /* Simple "hash": just copy the key */
    int j = 0;
    while (key[j] && i < 126) { result[i++] = key[j++]; }
    result[i] = 0;
    return result;
}

/* endgrent / setgrent / getgrent */
void endgrent(void) {}
void setgrent(void) {}

struct group {
    char *gr_name;
    char *gr_passwd;
    unsigned int gr_gid;
    char **gr_mem;
};

static char *_wheel_mem2[] = { "root", (char *)0 };
static struct group _wheel_gr2 = { "wheel", "*", 0, _wheel_mem2 };
static int _gr_iter = 0;

struct group *getgrent(void) {
    if (_gr_iter == 0) { _gr_iter++; return &_wheel_gr2; }
    return (struct group *)0;
}

struct group *getgrgid_r(unsigned int gid, struct group *grp, char *buf,
                          unsigned long bufsize, struct group **result) {
    (void)buf; (void)bufsize;
    if (gid == 0) { *grp = _wheel_gr2; *result = grp; return 0; }
    *result = 0;
    return 0;
}

/* uname */
struct utsname {
    char sysname[256];
    char nodename[256];
    char release[256];
    char version[256];
    char machine[256];
};

int uname(struct utsname *buf) {
    const char *fields[] = {
        "Darwin", "panthera", "23.1.0",
        "Panthera Darwin Kernel Version 23.1.0", "x86_64"
    };
    char *dsts[] = { buf->sysname, buf->nodename, buf->release,
                     buf->version, buf->machine };
    for (int f = 0; f < 5; f++) {
        int i = 0;
        while (fields[f][i] && i < 255) { dsts[f][i] = fields[f][i]; i++; }
        dsts[f][i] = 0;
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * Additional symbols needed by dash shell
 * ═══════════════════════════════════════════════════════════ */

/* fnmatch — already have $UNIX2003 version, create plain alias */
extern int fnmatch_unix2003(const char *, const char *, int);
__asm__(".globl _fnmatch\n_fnmatch = _fnmatch$UNIX2003");

/* sigsetmask — old BSD signal mask function */
int sigsetmask(int mask) {
    /* Use sigprocmask to set the signal mask */
    unsigned long oldmask = 0;
    unsigned long newmask = (unsigned long)mask;
    _sc6(329 /* __pthread_sigmask */, 1 /* SIG_SETMASK */,
         (long)&newmask, (long)&oldmask, 0, 0, 0);
    return (int)oldmask;
}

/* strchrnul — like strchr but returns pointer to null terminator if not found */
char *strchrnul(const char *s, int c) {
    while (*s && *s != (char)c) s++;
    return (char *)s;
}

/* vfork — use regular fork (vfork is unsafe without kernel support) */
__asm__(".globl _vfork\n_vfork:");
__asm__("movq $0x2000002, %rax");
__asm__("syscall");
__asm__("testq %rdx, %rdx");
__asm__("jnz 1f");
__asm__("ret");
__asm__("1: xorl %eax, %eax");
__asm__("ret");

/* ═══════════════════════════════════════════════════════════
 * setjmp/longjmp — wrappers for sigsetjmp/siglongjmp
 * These override the lazy-bind-dependent versions in libsystem_c
 * ═══════════════════════════════════════════════════════════ */

/*
 * __setjmp / __longjmp — register save/restore for setjmp/longjmp
 *
 * On Apple's system, these are the actual implementations in assembly.
 * Platform's _setjmp calls __setjmp at the end to save registers.
 * libsystem_c's __setjmp wrapper also calls _setjmp, which creates
 * a circular dependency. We break the cycle by providing the REAL
 * register-save __setjmp here.
 *
 * jmp_buf layout (Apple x86_64):
 * [0] = rbx
 * [1] = rbp
 * [2] = rsp (caller's, after __setjmp returns)
 * [3] = r12
 * [4] = r13
 * [5] = r14
 * [6] = r15
 * [7] = rip (return address)
 * [8] = rflags (or 0)
 * [9] = mxcsr (or 0)
 * [10] = fp control word (or 0)
 * [0x50] = saved signal mask (set by _setjmp before calling __setjmp)
 */
__asm__(
    ".globl __setjmp\n"
    "__setjmp:\n"
    "  movq   %rbx, (%rdi)\n"        /* jmp_buf[0] = rbx */
    "  movq   %rbp, 0x08(%rdi)\n"    /* jmp_buf[1] = rbp */
    "  movq   %rsp, %rax\n"
    "  addq   $0x08, %rax\n"         /* caller's rsp (before call pushed ret addr) */
    "  movq   %rax, 0x10(%rdi)\n"    /* jmp_buf[2] = rsp */
    "  movq   %r12, 0x18(%rdi)\n"    /* jmp_buf[3] = r12 */
    "  movq   %r13, 0x20(%rdi)\n"    /* jmp_buf[4] = r13 */
    "  movq   %r14, 0x28(%rdi)\n"    /* jmp_buf[5] = r14 */
    "  movq   %r15, 0x30(%rdi)\n"    /* jmp_buf[6] = r15 */
    "  movq   (%rsp), %rax\n"        /* return address */
    "  movq   %rax, 0x38(%rdi)\n"    /* jmp_buf[7] = rip */
    "  xorl   %eax, %eax\n"          /* return 0 */
    "  ret\n"

    ".globl __longjmp\n"
    "__longjmp:\n"
    "  movq   (%rdi), %rbx\n"        /* rbx = jmp_buf[0] */
    "  movq   0x08(%rdi), %rbp\n"    /* rbp = jmp_buf[1] */
    "  movq   0x10(%rdi), %rsp\n"    /* rsp = jmp_buf[2] */
    "  movq   0x18(%rdi), %r12\n"    /* r12 = jmp_buf[3] */
    "  movq   0x20(%rdi), %r13\n"    /* r13 = jmp_buf[4] */
    "  movq   0x28(%rdi), %r14\n"    /* r14 = jmp_buf[5] */
    "  movq   0x30(%rdi), %r15\n"    /* r15 = jmp_buf[6] */
    "  movq   0x38(%rdi), %rax\n"    /* rax = jmp_buf[7] = return addr */
    "  movl   %esi, %eax\n"          /* return value = second arg */
    "  testl  %eax, %eax\n"
    "  jnz    1f\n"
    "  movl   $1, %eax\n"            /* if val==0, return 1 instead */
    "1:\n"
    "  movq   0x38(%rdi), %rcx\n"    /* get return address */
    "  jmpq   *%rcx\n"               /* jump to saved rip */
);

/* ═══════════════════════════════════════════════════════════
 * strtoimax / strtoumax — locale-free implementations
 * Apple's versions crash without full locale initialization.
 * These provide working C-locale versions.
 * ═══════════════════════════════════════════════════════════ */

typedef long intmax_t;
typedef unsigned long uintmax_t;
typedef void *locale_t2;
#define locale_t locale_t2

uintmax_t strtoumax(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    uintmax_t acc = 0;
    int neg = 0;
    
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    
    if (base == 0) {
        if (*s == '0') {
            s++;
            if (*s == 'x' || *s == 'X') { base = 16; s++; }
            else base = 8;
        } else base = 10;
    } else if (base == 16 && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    
    while (*s) {
        int digit;
        if (*s >= '0' && *s <= '9') digit = *s - '0';
        else if (*s >= 'a' && *s <= 'f') digit = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') digit = *s - 'A' + 10;
        else break;
        if (digit >= base) break;
        acc = acc * base + digit;
        s++;
    }
    
    if (endptr) *endptr = (char *)s;
    return neg ? (uintmax_t)(-(intmax_t)acc) : acc;
}

intmax_t strtoimax(const char *nptr, char **endptr, int base) {
    return (intmax_t)strtoumax(nptr, endptr, base);
}

/* Locale variants — just forward to non-locale versions */
uintmax_t strtoumax_l(const char *nptr, char **endptr, int base, locale_t loc) {
    (void)loc;
    return strtoumax(nptr, endptr, base);
}

intmax_t strtoimax_l(const char *nptr, char **endptr, int base, locale_t loc) {
    (void)loc;
    return strtoimax(nptr, endptr, base);
}

/* Also provide strtol_l, strtoul_l, strtoll_l, strtoull_l */
long strtol_l(const char *s, char **e, int b, locale_t l) {
    (void)l; return (long)strtoimax(s, e, b);
}
unsigned long strtoul_l(const char *s, char **e, int b, locale_t l) {
    (void)l; return (unsigned long)strtoumax(s, e, b);
}
long long strtoll_l(const char *s, char **e, int b, locale_t l) {
    (void)l; return (long long)strtoimax(s, e, b);
}
unsigned long long strtoull_l(const char *s, char **e, int b, locale_t l) {
    (void)l; return (unsigned long long)strtoumax(s, e, b);
}

/* strtol, strtoul, strtoll, strtoull — C locale versions */
long strtol(const char *s, char **e, int b) { return (long)strtoimax(s, e, b); }
unsigned long strtoul(const char *s, char **e, int b) { return (unsigned long)strtoumax(s, e, b); }
long long strtoll(const char *s, char **e, int b) { return (long long)strtoimax(s, e, b); }
unsigned long long strtoull(const char *s, char **e, int b) { return (unsigned long long)strtoumax(s, e, b); }

/* BSD compat */
long long strtoq(const char *s, char **e, int b) { return strtoll(s, e, b); }
unsigned long long strtouq(const char *s, char **e, int b) { return strtoull(s, e, b); }

/* $UNIX2003 aliases */
__asm__(".globl _strtol$UNIX2003\n_strtol$UNIX2003 = _strtol");
__asm__(".globl _strtoul$UNIX2003\n_strtoul$UNIX2003 = _strtoul");
__asm__(".globl _strtoll$UNIX2003\n_strtoll$UNIX2003 = _strtoll");
__asm__(".globl _strtoull$UNIX2003\n_strtoull$UNIX2003 = _strtoull");

/* ═══════════════════════════════════════════════════════════
 * tcgetpgrp/tcsetpgrp — terminal process group
 * Our console doesn't have proper tty process groups.
 * Return the process's own pgrp so dash doesn't SIGTTIN itself.
 * ═══════════════════════════════════════════════════════════ */

extern long getpgrp(void);

int tcgetpgrp(int fd) {
    (void)fd;
    /* Return our own process group — this tells the shell
     * it IS the foreground process */
    return (int)getpgrp();
}

int tcsetpgrp(int fd, int pgrp) {
    (void)fd; (void)pgrp;
    return 0;
}

/* tcgetattr/tcsetattr — terminal attributes
 * Basic stubs that report success without doing real termios.
 * Needed by dash and other terminal-aware programs. */

struct termios {
    unsigned long c_iflag;
    unsigned long c_oflag;
    unsigned long c_cflag;
    unsigned long c_lflag;
    unsigned char c_cc[20];
    unsigned long c_ispeed;
    unsigned long c_ospeed;
};

int tcgetattr(int fd, struct termios *t) {
    (void)fd;
    if (t) {
        /* Default: canonical mode, echo on */
        t->c_iflag = 0x2B02; /* ICRNL|IXON|IXANY|IMAXBEL */
        t->c_oflag = 0x3;    /* OPOST|ONLCR */
        t->c_cflag = 0x4B00; /* CS8|CREAD|HUPCL */
        t->c_lflag = 0x5CF;  /* ECHO|ECHOE|ECHOK|ECHOCTL|ECHOKE|ICANON|ISIG|IEXTEN */
        for (int i = 0; i < 20; i++) t->c_cc[i] = 0;
        t->c_cc[0] = 4;   /* VEOF = ^D */
        t->c_cc[1] = 255;  /* VEOL = disabled */
        t->c_cc[3] = 8;   /* VERASE = ^H */
        t->c_cc[8] = 3;   /* VINTR = ^C */
        t->c_cc[12] = 26; /* VSUSP = ^Z */
        t->c_ispeed = 9600;
        t->c_ospeed = 9600;
    }
    return 0;
}

int tcsetattr(int fd, int action, const struct termios *t) {
    (void)fd; (void)action; (void)t;
    return 0;
}

int tcdrain(int fd) { (void)fd; return 0; }
int tcflush(int fd, int action) { (void)fd; (void)action; return 0; }
int tcflow(int fd, int action) { (void)fd; (void)action; return 0; }
int tcsendbreak(int fd, int dur) { (void)fd; (void)dur; return 0; }

unsigned long cfgetispeed(const struct termios *t) { return t ? t->c_ispeed : 9600; }
unsigned long cfgetospeed(const struct termios *t) { return t ? t->c_ospeed : 9600; }
int cfsetispeed(struct termios *t, unsigned long s) { if(t) t->c_ispeed = s; return 0; }
int cfsetospeed(struct termios *t, unsigned long s) { if(t) t->c_ospeed = s; return 0; }
int cfsetspeed(struct termios *t, unsigned long s) { if(t) { t->c_ispeed = s; t->c_ospeed = s; } return 0; }
void cfmakeraw(struct termios *t) {
    if(t) { t->c_iflag = 0; t->c_oflag = 0; t->c_lflag = 0; t->c_cflag |= 0x300; }
}

/* login_tty: set fd as the controlling terminal, dup to stdin/stdout/stderr */
int login_tty(int fd) {
    long ret;
    /* setsid */
    __asm__ volatile("syscall" : "=a"(ret) : "a"(0x2000147) : "rcx","r11","memory");
    /* ioctl(fd, TIOCSCTTY, 0) — TIOCSCTTY = 0x20007461 on Darwin */
    __asm__ volatile("syscall" : "=a"(ret)
        : "a"(0x2000036), "D"(fd), "S"(0x20007461L), "d"(0L) : "rcx","r11","memory");
    /* dup2(fd, 0) */
    __asm__ volatile("syscall" : "=a"(ret)
        : "a"(0x200005a), "D"(fd), "S"(0L) : "rcx","r11","memory");
    /* dup2(fd, 1) */
    __asm__ volatile("syscall" : "=a"(ret)
        : "a"(0x200005a), "D"(fd), "S"(1L) : "rcx","r11","memory");
    /* dup2(fd, 2) */
    __asm__ volatile("syscall" : "=a"(ret)
        : "a"(0x200005a), "D"(fd), "S"(2L) : "rcx","r11","memory");
    if (fd > 2) {
        /* close(fd) */
        __asm__ volatile("syscall" : "=a"(ret)
            : "a"(0x2000006), "D"(fd) : "rcx","r11","memory");
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * Missing critical symbols for launchd / coreutils
 * ═══════════════════════════════════════════════════════════ */

/* unsetenv — remove variable from environment */
extern char **environ;
int unsetenv(const char *name) {
    if (!name || !*name) return -1;
    size_t len = 0;
    while (name[len] && name[len] != '=') len++;
    if (name[len] == '=') return -1;
    if (!environ) return 0;
    char **ep = environ;
    char **wp = environ;
    while (*ep) {
        int match = 1;
        for (size_t i = 0; i < len; i++) {
            if ((*ep)[i] != name[i]) { match = 0; break; }
        }
        if (match && (*ep)[len] == '=') {
            ep++;
            continue;
        }
        *wp++ = *ep++;
    }
    *wp = (char *)0;
    return 0;
}

/* getopt — removed: use libc's getopt$UNIX2003 via alias in libc_missing_aliases.c */

/* uuid_generate — produce a random UUID */
void uuid_generate(unsigned char out[16]) {
    /* Use getentropy via syscall 244 (SYS_getentropy) */
    long ret;
    __asm__ volatile("syscall" : "=a"(ret)
        : "a"(0x20000F4), "D"(out), "S"(16L) : "rcx","r11","memory");
    /* Set version 4 (random) and variant bits */
    out[6] = (out[6] & 0x0F) | 0x40;
    out[8] = (out[8] & 0x3F) | 0x80;
}

/* uuid_is_null — check if UUID is all zeros */
int uuid_is_null(const unsigned char uu[16]) {
    for (int i = 0; i < 16; i++)
        if (uu[i]) return 0;
    return 1;
}
