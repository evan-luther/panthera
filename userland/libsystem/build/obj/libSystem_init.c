/* libSystem.B.dylib init — called by dyld during process startup */

#include <stddef.h>

static void
panthera_libsystem_trace(const char *msg)
{
    (void)msg;
}

struct ProgramVars {
    void *mh;
    int *NXArgcPtr;
    char ***NXArgvPtr;
    char ***environPtr;
    char **__prognamePtr;
};

struct _libpthread_functions {
    unsigned long version;
    void (*exit)(int);
    void *(*malloc)(size_t);
    void (*free)(void *);
};

static void
panthera_libsystem_atfork_prepare(void)
{
}

static void
panthera_libsystem_atfork_parent(void)
{
}

static void
panthera_libsystem_atfork_child(void)
{
}

/* External init functions from sub-libraries */
extern void __malloc_init(const char *apple[]) __attribute__((weak));
extern void __libc_init(const struct ProgramVars *vars,
    void (*atfork_prepare)(void),
    void (*atfork_parent)(void),
    void (*atfork_child)(void),
    const char *apple[]) __attribute__((weak));
extern int __pthread_init(const struct _libpthread_functions *pthread_funcs,
    const char *envp[], const char *apple[],
    const struct ProgramVars *vars) __attribute__((weak));
extern void __pthread_late_init(const char *envp[], const char *apple[],
    const struct ProgramVars *vars) __attribute__((weak));
extern void libdispatch_init(void) __attribute__((weak));
extern void exit(int) __attribute__((noreturn));
extern void *malloc(size_t);
extern void free(void *);

/* This function is stored in __mod_init_func and called by dyld */
__attribute__((constructor))
void __libSystem_init(int argc, const char *argv[], const char *envp[],
    const char *apple[], const struct ProgramVars *vars) {
    (void)argc;
    (void)argv;
    (void)envp;

    /*
     * Apple's init ordering: malloc MUST be initialized before anything
     * else calls malloc(). __malloc_init() replaces the 0xdeaddeaddeaddead
     * sentinel in malloc_zones with real zone pointers.
     *
     * Primary path: panthera_dyld calls __malloc_init directly before
     * running constructors.  This fallback catches the case where a
     * different dyld is used or the primary call was skipped.
     * Apple's _malloc_initialize() is idempotent — safe to call twice.
     */
    panthera_libsystem_trace("libSystem_init: before malloc\n");
    if (__malloc_init) __malloc_init(apple);
    panthera_libsystem_trace("libSystem_init: after malloc\n");

    /* libc init */
    panthera_libsystem_trace("libSystem_init: before libc\n");
    if (__libc_init) {
        __libc_init(vars,
            panthera_libsystem_atfork_prepare,
            panthera_libsystem_atfork_parent,
            panthera_libsystem_atfork_child,
            apple);
    }
    panthera_libsystem_trace("libSystem_init: after libc\n");

    /*
     * libdispatch asks libpthread for workqueue features during root queue
     * initialization.  Keep pthread before dispatch so that query is backed by
     * __bsdthread_register instead of tripping libpthread's uninitialized guard.
     */
    static const struct _libpthread_functions pthread_funcs = {
        2,
        exit,
        malloc,
        free,
    };
    panthera_libsystem_trace("libSystem_init: before pthread\n");
    if (__pthread_init) {
        panthera_libsystem_trace("libSystem_init: pthread available\n");
        __pthread_init(&pthread_funcs, envp, apple, vars);
    } else {
        panthera_libsystem_trace("libSystem_init: pthread missing\n");
    }
    panthera_libsystem_trace("libSystem_init: after pthread\n");

    /* dispatch init */
    panthera_libsystem_trace("libSystem_init: before dispatch\n");
    if (libdispatch_init) libdispatch_init();
    panthera_libsystem_trace("libSystem_init: after dispatch\n");

    if (__pthread_late_init) __pthread_late_init(envp, apple, vars);
}
