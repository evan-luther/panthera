/*
 * panthera_tier1_syms.c — symbols needed by Tier 1 utilities
 * (awk, less, bash, vim, bzip2, system_cmds)
 *
 * Functions that get Darwin-suffixed ($UNIX2003, $DARWIN_EXTSN) by the
 * headers are defined using __asm to force the exact symbol name that
 * the cross-compiled binaries reference.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>  /* size_t */
#include <signal.h>  /* stack_t, sigset_t, SS_DISABLE */
#include <sys/time.h> /* struct timespec */

/* Avoid pulling in full stdio/stdlib which apply Darwin symbol versioning.
 * Declare only what we need. */
typedef struct __sFILE FILE;
extern FILE *__stderrp;
#define stderr __stderrp

extern int fprintf(FILE *, const char *, ...);
extern int snprintf(char *, size_t, const char *, ...);
extern FILE *fopen(const char *, const char *);
extern FILE *fdopen(int, const char *);
extern int fclose(FILE *);
extern void *memset(void *, int, size_t);
extern size_t strlen(const char *);
extern char *strncpy(char *, const char *, size_t);
extern int pipe(int [2]);
extern int dup2(int, int);
extern int close(int);
extern int open(const char *, int, ...) __asm("_open");
extern int fork(void) __asm("_fork");
extern int execv(const char *, char *const []) __asm("_execv");
extern int execl(const char *, const char *, ...) __asm("_execl");
extern void _exit(int) __attribute__((noreturn));
extern int waitpid(int, int *, int) __asm("_waitpid");
extern int *__error(void);
#define errno (*__error())
#define EINTR  4
#define ECHILD 10
#define ENOTSUP 45
#define O_WRONLY 1
#define O_CREAT  0x200
#define O_TRUNC  0x400
#define STDOUT_FILENO 1
#define STDIN_FILENO  0

typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef unsigned short mode_t;

/* ====== Math functions ====== */

double sin(double x)   __asm("_sin");
double cos(double x)   __asm("_cos");
double tan(double x)   __asm("_tan");
double asin(double x)  __asm("_asin");
double acos(double x)  __asm("_acos");
double atan(double x)  __asm("_atan");
double atan2(double y, double x) __asm("_atan2");
double exp(double x)   __asm("_exp");
double log(double x)   __asm("_log");
double log10(double x) __asm("_log10");
double sinh(double x)  __asm("_sinh");
double cosh(double x)  __asm("_cosh");
double tanh(double x)  __asm("_tanh");
double modf(double x, double *iptr) __asm("_modf");

double sin(double x)  { return __builtin_sin(x); }
double cos(double x)  { return __builtin_cos(x); }
double tan(double x)  { return __builtin_tan(x); }
double asin(double x) { return __builtin_asin(x); }
double acos(double x) { return __builtin_acos(x); }
double atan(double x) { return __builtin_atan(x); }
double atan2(double y, double x) { return __builtin_atan2(y, x); }
double exp(double x)  { return __builtin_exp(x); }
double log(double x)  { return __builtin_log(x); }
double log10(double x) { return __builtin_log10(x); }
double sinh(double x) { return __builtin_sinh(x); }
double cosh(double x) { return __builtin_cosh(x); }
double tanh(double x) { return __builtin_tanh(x); }
double modf(double x, double *iptr) {
    union {
        double d;
        uint64_t u;
    } value, whole, zero;
    int exponent;
    uint64_t mask;

    value.d = x;
    exponent = (int)((value.u >> 52) & 0x7ff) - 1023;

    zero.u = value.u & (1ULL << 63);

    if (exponent < 0) {
        *iptr = zero.d;
        return x;
    }
    if (exponent >= 52) {
        *iptr = x;
        if (exponent == 1024)
            return x - x;
        return zero.d;
    }

    mask = (1ULL << (52 - exponent)) - 1;
    if ((value.u & mask) == 0) {
        *iptr = x;
        return zero.d;
    }

    whole.u = value.u & ~mask;
    *iptr = whole.d;
    return x - whole.d;
}

/* ====== err/warn family ====== */
/* _verr/_vwarn exist in libsystem_c; these are the non-v wrappers */

extern void verr(int eval, const char *fmt, __builtin_va_list ap)
    __asm("_verr") __attribute__((noreturn));
extern void vwarn(const char *fmt, __builtin_va_list ap) __asm("_vwarn");

void err(int eval, const char *fmt, ...) __asm("_err");
void warn(const char *fmt, ...) __asm("_warn");

void err(int eval, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    verr(eval, fmt, ap);
}

void warn(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vwarn(fmt, ap);
    va_end(ap);
}

/* ====== system() ====== */
/* Binaries reference _system (plain), not _system$UNIX2003 */

int panthera_system(const char *command) __asm("_system");
int panthera_system(const char *command) {
    if (command == (void *)0) return 1;
    int pid = fork();
    if (pid == -1) return -1;
    if (pid == 0) {
        const char *argv[] = { "/bin/sh", "-c", command, (void *)0 };
        execv("/bin/sh", (char *const *)argv);
        _exit(127);
    }
    int status;
    while (waitpid(pid, &status, 0) == -1) {
        if (errno != EINTR) return -1;
    }
    return status;
}

/* ====== popen/pclose ====== */
/* Binaries reference _popen (plain), not _popen$DARWIN_EXTSN */

static int _popen_pid_storage = -1;

FILE *panthera_popen(const char *command, const char *type) __asm("_popen");
int panthera_pclose(FILE *stream) __asm("_pclose");

FILE *panthera_popen(const char *command, const char *type) {
    int pfd[2];
    if (pipe(pfd) < 0) return (void *)0;

    int reading = (type[0] == 'r');
    int pid = fork();
    if (pid == -1) {
        close(pfd[0]); close(pfd[1]);
        return (void *)0;
    }
    if (pid == 0) {
        if (reading) {
            close(pfd[0]);
            dup2(pfd[1], STDOUT_FILENO);
            close(pfd[1]);
        } else {
            close(pfd[1]);
            dup2(pfd[0], STDIN_FILENO);
            close(pfd[0]);
        }
        execl("/bin/sh", "sh", "-c", command, (char *)0);
        _exit(127);
    }
    _popen_pid_storage = pid;
    if (reading) {
        close(pfd[1]);
        return fdopen(pfd[0], "r");
    } else {
        close(pfd[0]);
        return fdopen(pfd[1], "w");
    }
}

int panthera_pclose(FILE *stream) {
    fclose(stream);
    int status;
    int pid = _popen_pid_storage;
    _popen_pid_storage = -1;
    if (pid == -1) { errno = ECHILD; return -1; }
    while (waitpid(pid, &status, 0) == -1) {
        if (errno != EINTR) return -1;
    }
    return status;
}

/* ====== creat ====== */

int panthera_creat(const char *path, mode_t mode) __asm("_creat");
int panthera_creat(const char *path, mode_t mode) {
    return open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
}

/* ====== freopen ====== */

FILE *panthera_freopen(const char *path, const char *mode, FILE *stream)
    __asm("_freopen");
FILE *panthera_freopen(const char *path, const char *mode, FILE *stream) {
    fclose(stream);
    return fopen(path, mode);
}

/* ====== confstr ====== */

size_t panthera_confstr(int name, char *buf, size_t len) __asm("_confstr");
size_t panthera_confstr(int name, char *buf, size_t len) {
    const char *val = "";
    if (name == 1)  /* _CS_PATH */
        val = "/usr/bin:/bin:/usr/sbin:/sbin";
    size_t vlen = strlen(val) + 1;
    if (buf && len > 0) {
        strncpy(buf, val, len);
        buf[len - 1] = '\0';
    }
    return vlen;
}

/* ====== sigaltstack ====== */

int panthera_sigaltstack(const stack_t *ss, stack_t *oss)
    __asm("_sigaltstack");
int panthera_sigaltstack(const stack_t *ss, stack_t *oss) {
    if (oss) {
        oss->ss_sp = (void *)0;
        oss->ss_size = 0;
        oss->ss_flags = SS_DISABLE;
    }
    return 0;
}

/* ====== pselect$1050 ====== */

extern int __pselect(int, void *, void *, void *,
                     const struct timespec *, const sigset_t *)
    __asm("___pselect");

int panthera_pselect_1050(int nfds, void *r, void *w, void *e,
                          const struct timespec *t, const sigset_t *s)
    __asm("_pselect$1050");
int panthera_pselect_1050(int nfds, void *r, void *w, void *e,
                          const struct timespec *t, const sigset_t *s) {
    return __pselect(nfds, r, w, e, t, s);
}

/* ====== ACL stubs ====== */

void *acl_get_file(const char *path, int type) __asm("_acl_get_file");
int acl_set_file(const char *path, int type, void *acl) __asm("_acl_set_file");
int acl_free(void *obj) __asm("_acl_free");

void *acl_get_file(const char *path, int type) {
    (void)path; (void)type;
    errno = ENOTSUP;
    return (void *)0;
}

int acl_set_file(const char *path, int type, void *acl) {
    (void)path; (void)type; (void)acl;
    errno = ENOTSUP;
    return -1;
}

int acl_free(void *obj) {
    (void)obj;
    return 0;
}

/* ====== Mach stubs for hostinfo/vm_stat ====== */

/* mach_error_string already exists in libsystem_kernel — skip it */

void mach_error(const char *str, int err) __asm("_mach_error");
void mach_error(const char *str, int err) {
    (void)err;
    fprintf(stderr, "%s: mach error %d\n", str, err);
}

const char *slot_name(int slot_type, int slot_num, char *buf, int buflen)
    __asm("_slot_name");
const char *slot_name(int slot_type, int slot_num, char *buf, int buflen) {
    (void)slot_type; (void)slot_num;
    snprintf(buf, buflen, "cpu%d", slot_num);
    return buf;
}

int processor_set_info(unsigned int pset, int flavor, unsigned int *host,
                       void *info, unsigned int *count)
    __asm("_processor_set_info");
int processor_set_info(unsigned int pset, int flavor, unsigned int *host,
                       void *info, unsigned int *count) {
    (void)pset; (void)flavor; (void)host; (void)info; (void)count;
    return 5;
}

int processor_set_statistics(unsigned int pset, int flavor,
                             void *info, unsigned int *count)
    __asm("_processor_set_statistics");
int processor_set_statistics(unsigned int pset, int flavor,
                             void *info, unsigned int *count) {
    (void)pset; (void)flavor; (void)info; (void)count;
    return 5;
}

/* ====== Membership stubs ====== */

int mbr_uid_to_uuid(uid_t uid, unsigned char *uu) __asm("_mbr_uid_to_uuid");
int mbr_gid_to_uuid(gid_t gid, unsigned char *uu) __asm("_mbr_gid_to_uuid");
int mbr_check_membership(const unsigned char *u, const unsigned char *g,
                         int *ismember) __asm("_mbr_check_membership");

int mbr_uid_to_uuid(uid_t uid, unsigned char *uu) {
    (void)uid;
    memset(uu, 0, 16);
    return -1;
}

int mbr_gid_to_uuid(gid_t gid, unsigned char *uu) {
    (void)gid;
    memset(uu, 0, 16);
    return -1;
}

int mbr_check_membership(const unsigned char *u, const unsigned char *g,
                         int *ismember) {
    (void)u; (void)g;
    *ismember = 1;
    return 0;
}
