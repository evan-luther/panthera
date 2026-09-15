#include <dispatch/dispatch.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
marker(const char *line)
{
	(void)write(STDOUT_FILENO, line, strlen(line));
}

static void
timeout_handler(int signo)
{
	(void)signo;
	(void)write(STDOUT_FILENO, "PANTHERA_DISPATCH_TIMER:TIMEOUT\n", 32);
	_exit(124);
}

static void
timer_handler(void *ctx)
{
	(void)ctx;
	(void)write(STDOUT_FILENO, "PANTHERA_DISPATCH_TIMER:FIRED\n", 30);
	_exit(0);
}

int
main(void)
{
	dispatch_queue_t queue;
	dispatch_source_t timer;

	(void)write(STDOUT_FILENO, "PANTHERA_DISPATCH_TIMER:START\n", 30);
	signal(SIGALRM, timeout_handler);
	alarm(5);

	marker("PANTHERA_DISPATCH_TIMER:BEFORE_QUEUE\n");
	queue = dispatch_queue_create("panthera.dispatch.timer.probe", NULL);
	marker("PANTHERA_DISPATCH_TIMER:AFTER_QUEUE\n");
	if (queue == NULL) {
		(void)write(STDOUT_FILENO, "PANTHERA_DISPATCH_TIMER:NO_QUEUE\n", 34);
		return 2;
	}
	marker("PANTHERA_DISPATCH_TIMER:BEFORE_SOURCE\n");
	timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue);
	marker("PANTHERA_DISPATCH_TIMER:AFTER_SOURCE\n");
	if (timer == NULL) {
		(void)write(STDOUT_FILENO, "PANTHERA_DISPATCH_TIMER:NO_SOURCE\n", 35);
		return 3;
	}
	marker("PANTHERA_DISPATCH_TIMER:BEFORE_CONTEXT\n");
	dispatch_set_context(timer, NULL);
	marker("PANTHERA_DISPATCH_TIMER:AFTER_CONTEXT\n");
	marker("PANTHERA_DISPATCH_TIMER:BEFORE_HANDLER\n");
	dispatch_source_set_event_handler_f(timer, timer_handler);
	marker("PANTHERA_DISPATCH_TIMER:AFTER_HANDLER\n");
	marker("PANTHERA_DISPATCH_TIMER:BEFORE_TIME\n");
	dispatch_source_set_timer(timer,
	    dispatch_time(DISPATCH_TIME_NOW, 0),
	    DISPATCH_TIME_FOREVER,
	    0);
	marker("PANTHERA_DISPATCH_TIMER:AFTER_TIME\n");
	marker("PANTHERA_DISPATCH_TIMER:BEFORE_ACTIVATE\n");
	dispatch_activate(timer);
	marker("PANTHERA_DISPATCH_TIMER:AFTER_ACTIVATE\n");
	marker("PANTHERA_DISPATCH_TIMER:BEFORE_MAIN\n");
	dispatch_main();
	return 4;
}
