/*
 * test_pthread2.c — Verify pthread_create works on Panthera.
 * Calls __bsdthread_register directly before creating threads,
 * since the frozen libsystem_pthread may not initialize properly.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <dlfcn.h>
#include <mach/mach.h>

/* bsdthread_register syscall — manually invoke it */
extern int __bsdthread_register(void *threadstart, void *wqthread,
    uint32_t flags, void *stack_addr_hint, void *targetconc_ptr,
    uint64_t dispatchqueue_offset, uint32_t tsd_offset);

/* These addresses are resolved at runtime via nm */

static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static volatile int shared_value = 0;
static volatile int thread_ran = 0;

static void *worker(void *arg) {
    int id = *(int *)arg;
    /* Use write() instead of printf to avoid TLS issues */
    char buf[128];
    int len = snprintf(buf, sizeof(buf), "[thread %d] started, tid=%p\n", id, (void *)pthread_self());
    write(1, buf, len);

    pthread_mutex_lock(&mtx);
    shared_value += 100;
    thread_ran = 1;
    len = snprintf(buf, sizeof(buf), "[thread %d] shared_value=%d, signaling\n", id, shared_value);
    write(1, buf, len);
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mtx);

    len = snprintf(buf, sizeof(buf), "[thread %d] done\n", id);
    write(1, buf, len);
    return (void *)(long)(id * 42);
}

int main(void) {
    pthread_t t1;
    int id1 = 1;
    void *retval = NULL;
    int rc;

    printf("test_pthread2: starting\n");

    /* Resolve _thread_start and _start_wqthread from libsystem_pthread via dlsym */
    void *thread_start_fn = dlsym(RTLD_DEFAULT, "_thread_start");
    void *wqthread_fn = dlsym(RTLD_DEFAULT, "_start_wqthread");
    printf("_thread_start=%p _start_wqthread=%p\n", thread_start_fn, wqthread_fn);

    if (thread_start_fn) {
        rc = __bsdthread_register(
            thread_start_fn,
            wqthread_fn ? wqthread_fn : thread_start_fn,
            0,   /* flags */
            NULL, /* stack_addr_hint */
            NULL, /* targetconc_ptr */
            0,   /* dispatchqueue_offset */
            0    /* tsd_offset */
        );
        printf("bsdthread_register returned %d\n", rc);
    } else {
        printf("WARNING: _thread_start not found, threads may not work\n");
    }

    printf("main thread: %p\n", (void *)pthread_self());

    rc = pthread_create(&t1, NULL, worker, &id1);
    if (rc != 0) {
        printf("FAIL: pthread_create returned %d\n", rc);
        return 1;
    }
    printf("pthread_create OK, thread=%p\n", (void *)t1);

    /* Wait for thread to signal */
    pthread_mutex_lock(&mtx);
    while (!thread_ran) {
        printf("main: waiting for cond_signal...\n");
        pthread_cond_wait(&cond, &mtx);
    }
    printf("main: woke up, shared_value=%d\n", shared_value);
    pthread_mutex_unlock(&mtx);

    rc = pthread_join(t1, &retval);
    if (rc != 0) {
        printf("FAIL: pthread_join returned %d\n", rc);
        return 1;
    }
    printf("pthread_join OK, retval=%ld\n", (long)retval);

    if (shared_value != 100 || (long)retval != 42) {
        printf("FAIL: shared_value=%d retval=%ld\n", shared_value, (long)retval);
        return 1;
    }

    printf("test_pthread2: ALL PASS\n");
    return 0;
}
