#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern double modf(double, double *);

static volatile sig_atomic_t timed_out;

static void
mark(const char *msg)
{
	(void)write(STDOUT_FILENO, msg, strlen(msg));
	(void)write(STDOUT_FILENO, "\n", 1);
}

static void
on_alarm(int signo)
{
	(void)signo;
	timed_out = 1;
}

int
main(void)
{
	char buf[128];
	double integral = 0.0;
	double value = 45.0;
	double frac;
	int n;

	if (signal(SIGALRM, on_alarm) == SIG_ERR) {
		perror("signal");
		return 1;
	}

	mark("PANTHERA_FLOAT_FORMAT_STAGE:MODF");
	alarm(10);
	frac = modf(value, &integral);
	alarm(0);
	if (timed_out) {
		printf("PANTHERA_FLOAT_FORMAT_MODF_TIMEOUT\n");
		return 1;
	}
	printf("PANTHERA_FLOAT_FORMAT_MODF_RESULT:%.0f:%.0f\n", frac, integral);

	mark("PANTHERA_FLOAT_FORMAT_STAGE:SNPRINTF_INT");
	timed_out = 0;
	alarm(10);
	errno = 0;
	n = snprintf(buf, sizeof(buf), "%.30g", value);
	alarm(0);
	if (timed_out) {
		printf("PANTHERA_FLOAT_FORMAT_SNPRINTF_INT_TIMEOUT\n");
		return 1;
	}
	printf("PANTHERA_FLOAT_FORMAT_SNPRINTF_INT_RESULT:%d:%s:%d\n",
	    n, buf, errno);

	mark("PANTHERA_FLOAT_FORMAT_STAGE:SNPRINTF_FRAC");
	timed_out = 0;
	alarm(10);
	errno = 0;
	n = snprintf(buf, sizeof(buf), "%.6g", 45.25);
	alarm(0);
	if (timed_out) {
		printf("PANTHERA_FLOAT_FORMAT_SNPRINTF_FRAC_TIMEOUT\n");
		return 1;
	}
	printf("PANTHERA_FLOAT_FORMAT_SNPRINTF_FRAC_RESULT:%d:%s:%d\n",
	    n, buf, errno);
	printf("PANTHERA_FLOAT_FORMAT_OK\n");
	return 0;
}
