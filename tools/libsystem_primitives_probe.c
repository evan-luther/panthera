#include <assert.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <signal.h>
#include <pthread.h>
#include <stdio.h>
#include <stdatomic.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t alarm_seen;
static atomic_int sleep_ready, sleep_go;
static char sleep_completed;
extern unsigned int sleep_nocancel(unsigned int) __asm__("_sleep$NOCANCEL");

static void
on_alarm(int signo)
{
	(void)signo;
	alarm_seen = 1;
}

static void *
sleep_worker(void *arg)
{
	int nocancel = *(int *)arg;
	atomic_store(&sleep_ready, 1);
	/* No cancellation point before the primitive under test. */
	while (!atomic_load(&sleep_go)) {}
	if (nocancel)
		assert(sleep_nocancel(1) == 0);
	else
		sleep(1);
	return &sleep_completed;
}

int
main(void)
{
	setbuf(stdout, NULL);
	/* Call the exported primitive, never a constant-folded compiler builtin. */
	double (*volatile power)(double, double) = pow;
	alarm(10);
	assert(power(2.0, 3.0) == 8.0);
	assert(power(-2.0, 3.0) == -8.0);
	assert(isnan(power(-2.0, 0.5)));
	assert(power(0.0, 0.0) == 1.0);
	assert(power(NAN, 0.0) == 1.0);
	assert(power(1.0, NAN) == 1.0);
	double value = power(-0.0, -3.0);
	assert(isinf(value) && signbit(value));
	assert(power(INFINITY, -1.0) == 0.0);
	assert(isinf(power(2.0, 1024.0)));
	assert(power(2.0, -1074.0) == DBL_TRUE_MIN);
	assert(power(2.0, -1075.0) == 0.0);
	value = power(2.0, 0.5);
	assert(value > 1.41421356237309 && value < 1.41421356237310);
	alarm(0);
	puts("PANTHERA_PRIMITIVES_MATH_OK");

	struct timespec request = { -1, 0 }, remaining;
	errno = 0;
	assert(nanosleep(&request, &remaining) == -1 && errno == EINVAL);
	request = (struct timespec){ 0, 1000000000L };
	errno = 0;
	assert(nanosleep(&request, &remaining) == -1 && errno == EINVAL);
	request = (struct timespec){ 0, -1 };
	errno = 0;
	assert(nanosleep(&request, &remaining) == -1 && errno == EINVAL);
	request = (struct timespec){ 0, -(1L << 32) };
	errno = 0;
	assert(nanosleep(&request, &remaining) == -1 && errno == EINVAL);
	request = (struct timespec){ 0, 0 };
	assert(nanosleep(&request, NULL) == 0);
	assert(sleep(0) == 0);
	assert(usleep(1000) == 0);

	/* Scheduled shutdown exercises sleep in a fork child. */
	struct timeval start, end;
	assert(gettimeofday(&start, NULL) == 0);
	pid_t child = fork();
	assert(child >= 0);
	if (child == 0) {
		alarm(5);
		_exit(sleep(1) != 0);
	}
	int status;
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	assert(gettimeofday(&end, NULL) == 0);
	assert((end.tv_sec - start.tv_sec) * 1000000LL + end.tv_usec - start.tv_usec >= 900000);

	struct sigaction action = { 0 };
	action.sa_handler = on_alarm;
	sigemptyset(&action.sa_mask);
	assert(sigaction(SIGALRM, &action, NULL) == 0);
	request = (struct timespec){ 5, 0 };
	alarm(1);
	errno = 0;
	assert(nanosleep(&request, &remaining) == -1 && errno == EINTR);
	assert(alarm_seen && remaining.tv_sec >= 0 && remaining.tv_sec < 5);
	assert(remaining.tv_nsec >= 0 && remaining.tv_nsec < 1000000000L);
	alarm_seen = 0;
	alarm(1);
	unsigned int unslept = sleep(3);
	alarm(0);
	assert(alarm_seen && unslept > 0 && unslept <= 3);
	action.sa_handler = SIG_DFL;
	assert(sigaction(SIGALRM, &action, NULL) == 0);
	alarm(10);
	for (int nocancel = 0; nocancel <= 1; ++nocancel) {
		pthread_t thread;
		atomic_store(&sleep_ready, 0);
		atomic_store(&sleep_go, 0);
		assert(pthread_create(&thread, NULL, sleep_worker, &nocancel) == 0);
		while (!atomic_load(&sleep_ready))
			usleep(1000);
		assert(pthread_cancel(thread) == 0);
		atomic_store(&sleep_go, 1);
		void *result;
		assert(pthread_join(thread, &result) == 0);
		assert(result == (nocancel ? &sleep_completed : PTHREAD_CANCELED));
	}
	alarm(0);
	puts("PANTHERA_LIBSYSTEM_PRIMITIVES_OK");
	return 0;
}
