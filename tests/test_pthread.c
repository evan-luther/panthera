/*
 * test_pthread.c — Verify pthread_create works on Panthera.
 * Tests: create, join, mutex, cond_signal.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static int shared_value = 0;
static int thread_ran = 0;

static void *worker(void *arg) {
    int id = *(int *)arg;
    printf("[thread %d] started, tid=%p\n", id, (void *)pthread_self());

    pthread_mutex_lock(&mtx);
    shared_value += 100;
    thread_ran = 1;
    printf("[thread %d] shared_value=%d, signaling\n", id, shared_value);
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mtx);

    printf("[thread %d] done\n", id);
    return (void *)(long)(id * 42);
}

int main(void) {
    pthread_t t1;
    int id1 = 1;
    void *retval = NULL;
    int rc;

    printf("test_pthread: starting\n");
    printf("main thread: %p\n", (void *)pthread_self());

    /* Test 1: Create a thread */
    rc = pthread_create(&t1, NULL, worker, &id1);
    if (rc != 0) {
        printf("FAIL: pthread_create returned %d\n", rc);
        if (rc == 35) printf("  (EAGAIN — thread creation disabled)\n");
        if (rc == 12) printf("  (ENOMEM — out of memory)\n");
        return 1;
    }
    printf("pthread_create OK, thread=%p\n", (void *)t1);

    /* Test 2: Wait for thread to signal */
    pthread_mutex_lock(&mtx);
    while (!thread_ran) {
        printf("main: waiting for cond_signal...\n");
        pthread_cond_wait(&cond, &mtx);
    }
    printf("main: woke up, shared_value=%d\n", shared_value);
    pthread_mutex_unlock(&mtx);

    /* Test 3: Join the thread */
    rc = pthread_join(t1, &retval);
    if (rc != 0) {
        printf("FAIL: pthread_join returned %d\n", rc);
        return 1;
    }
    printf("pthread_join OK, retval=%ld\n", (long)retval);

    /* Verify */
    if (shared_value != 100) {
        printf("FAIL: shared_value=%d expected 100\n", shared_value);
        return 1;
    }
    if ((long)retval != 42) {
        printf("FAIL: retval=%ld expected 42\n", (long)retval);
        return 1;
    }

    printf("test_pthread: ALL PASS\n");
    return 0;
}
