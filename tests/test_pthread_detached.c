/*
 * test_pthread_detached.c - Verify detached pthread startup on Panthera.
 *
 * OpenZFS libtpool creates detached worker threads after saving/restoring the
 * pthread signal mask.  Keep this probe close to that shape so a failure points
 * at libpthread/kernel thread startup instead of the ZFS import scanner.
 */
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static volatile int worker_ran;
static volatile int cond_worker_ran;
static pthread_mutex_t cond_mutex;
static pthread_cond_t cond;

static void
alarm_handler(int signo)
{
	(void)signo;
	write(1, "PANTHERA_PTHREAD_DETACHED:cond_wait_alarm\n", 42);
	_exit(4);
}

static void *
detached_worker(void *arg)
{
	(void)arg;
	write(1, "PANTHERA_PTHREAD_DETACHED:worker_start\n", 39);
	worker_ran = 1;
	return NULL;
}

static void *
cond_worker(void *arg)
{
	(void)arg;
	write(1, "PANTHERA_PTHREAD_DETACHED:cond_worker_before_lock\n", 50);
	pthread_mutex_lock(&cond_mutex);
	write(1, "PANTHERA_PTHREAD_DETACHED:cond_worker_after_lock\n", 49);
	cond_worker_ran = 1;
	pthread_cond_signal(&cond);
	pthread_mutex_unlock(&cond_mutex);
	return NULL;
}

int
main(void)
{
	pthread_attr_t attr;
	pthread_t thread;
	sigset_t oset;
	int rc;

	setvbuf(stdout, NULL, _IONBF, 0);
	printf("PANTHERA_PTHREAD_DETACHED:enter\n");

	rc = pthread_attr_init(&attr);
	printf("PANTHERA_PTHREAD_DETACHED:attr_init_rc=%d\n", rc);
	if (rc != 0) {
		return 1;
	}

	rc = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	printf("PANTHERA_PTHREAD_DETACHED:setdetach_rc=%d\n", rc);
	if (rc != 0) {
		return 1;
	}

	rc = pthread_mutex_init(&cond_mutex, NULL);
	printf("PANTHERA_PTHREAD_DETACHED:mutex_init_rc=%d\n", rc);
	if (rc != 0) {
		(void)pthread_attr_destroy(&attr);
		return 1;
	}

	rc = pthread_cond_init(&cond, NULL);
	printf("PANTHERA_PTHREAD_DETACHED:cond_init_rc=%d\n", rc);
	if (rc != 0) {
		(void)pthread_attr_destroy(&attr);
		return 1;
	}

	rc = pthread_sigmask(SIG_SETMASK, NULL, &oset);
	printf("PANTHERA_PTHREAD_DETACHED:sigmask_read_rc=%d\n", rc);
	if (rc != 0) {
		return 1;
	}

	rc = pthread_create(&thread, &attr, detached_worker, NULL);
	printf("PANTHERA_PTHREAD_DETACHED:create_rc=%d thread=%p\n", rc,
	    (void *)thread);

	(void)pthread_sigmask(SIG_SETMASK, &oset, NULL);

	if (rc != 0) {
		(void)pthread_attr_destroy(&attr);
		return 1;
	}

	rc = 0;
	for (int i = 0; i < 50; i++) {
		if (worker_ran) {
			rc = 1;
			break;
		}
		usleep(100000);
	}

	if (rc == 0) {
		(void)pthread_attr_destroy(&attr);
		printf("PANTHERA_PTHREAD_DETACHED:worker_timeout\n");
		return 2;
	}
	printf("PANTHERA_PTHREAD_DETACHED:START_PASS\n");

	pthread_mutex_lock(&cond_mutex);
	cond_worker_ran = 0;
	rc = pthread_create(&thread, &attr, cond_worker, NULL);
	printf("PANTHERA_PTHREAD_DETACHED:cond_create_rc=%d thread=%p\n", rc,
	    (void *)thread);
	(void)pthread_attr_destroy(&attr);
	if (rc != 0) {
		pthread_mutex_unlock(&cond_mutex);
		return 1;
	}

	signal(SIGALRM, alarm_handler);
	alarm(6);
	while (!cond_worker_ran) {
		rc = pthread_cond_wait(&cond, &cond_mutex);
		printf("PANTHERA_PTHREAD_DETACHED:cond_wait_rc=%d ran=%d\n",
		    rc, cond_worker_ran);
		if (rc != 0) {
			break;
		}
	}
	alarm(0);
	pthread_mutex_unlock(&cond_mutex);

	if (!cond_worker_ran) {
		printf("PANTHERA_PTHREAD_DETACHED:cond_worker_timeout\n");
		return 3;
	}

	printf("PANTHERA_PTHREAD_DETACHED:COND_PASS\n");
	printf("PANTHERA_PTHREAD_DETACHED:ALL_PASS\n");
	return 0;
}
