#include <sys/wait.h>

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <mach-o/dyld.h>

static struct {
	int size;
	int next;
	char **strings;
} runlist;

static void
die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

void *
allocate(unsigned long size)
{
	void *ptr = malloc(size);
	if (ptr == NULL)
		die("virtual memory exhausted");
	return ptr;
}

void *
reallocate(void *ptr, unsigned long size)
{
	void *new_ptr = realloc(ptr, size);
	if (new_ptr == NULL)
		die("virtual memory exhausted");
	return new_ptr;
}

char *
makestr(const char *first, ...)
{
	va_list ap;
	const char *part;
	size_t len = 0;
	char *out;

	va_start(ap, first);
	for (part = first; part != NULL; part = va_arg(ap, const char *))
		len += strlen(part);
	va_end(ap);

	out = allocate(len + 1);
	out[0] = '\0';

	va_start(ap, first);
	for (part = first; part != NULL; part = va_arg(ap, const char *))
		strcat(out, part);
	va_end(ap);

	return out;
}

int
execute(char **argv, int verbose)
{
	pid_t pid;
	pid_t waited;
	int status;

	if (verbose) {
		for (char **p = argv; *p != NULL; p++)
			fprintf(stderr, "%s%s", p == argv ? "+ " : " ", *p);
		fputc('\n', stderr);
	}

	pid = fork();
	if (pid < 0)
		die("can't fork: %s", strerror(errno));
	if (pid == 0) {
		execvp(argv[0], argv);
		die("can't find or exec %s: %s", argv[0], strerror(errno));
	}

	do {
		waited = wait(&status);
	} while (waited == -1 && errno == EINTR);
	if (waited == -1)
		die("wait on forked process %d failed: %s", pid, strerror(errno));
	if (WIFSIGNALED(status) && WTERMSIG(status) != SIGINT &&
	    WTERMSIG(status) != SIGPIPE)
		die("fatal error in %s", argv[0]);

	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

void
add_execute_list(char *str)
{
	if (runlist.strings == NULL) {
		runlist.next = 0;
		runlist.size = 16;
		runlist.strings = allocate((unsigned long)runlist.size * sizeof(char *));
	}
	if (runlist.next + 1 >= runlist.size) {
		runlist.size *= 2;
		runlist.strings = reallocate(runlist.strings,
		    (unsigned long)runlist.size * sizeof(char *));
	}
	runlist.strings[runlist.next++] = str;
	runlist.strings[runlist.next] = NULL;
}

char *
cmd_with_prefix(char *str)
{
	if (strcmp(str, "ranlib") == 0 && access("/usr/bin/ranlib", X_OK) == 0)
		return makestr("/usr/bin/", str, NULL);
	return str;
}

void
add_execute_list_with_prefix(char *str)
{
	add_execute_list(cmd_with_prefix(str));
}

void
reset_execute_list(void)
{
	runlist.next = 0;
}

int
execute_list(int verbose)
{
	return execute(runlist.strings, verbose);
}
