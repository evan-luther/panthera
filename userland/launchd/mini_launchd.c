/*
 * mini_launchd.c — Minimal PID 1 for Panthera bring-up.
 *
 * This is a temporary bootstrap program that:
 *   1. Opens /dev/console for stdin/stdout/stderr
 *   2. Prints a banner
 *   3. Fork/execs a shell as a child process
 *   4. Reaps and respawns that child forever
 *
 * Built as a static Mach-O x86_64 binary with no libSystem dependency.
 * Uses raw syscalls via the BSD syscall convention (syscall number in %rax,
 * args in %rdi, %rsi, %rdx, %r10, %r8, %r9; SYSCALL instruction).
 *
 * Replace with real launchd once libSystem is available.
 */

/* XNU BSD syscall numbers (from bsd/kern/syscalls.master) */
#define SYS_exit      1
#define SYS_read      3
#define SYS_write     4
#define SYS_open      5
#define SYS_close     6
#define SYS_wait4     7
#define SYS_dup2     90
#define SYS_execve   59
#define SYS_reboot   55
#define SYS_sync     36

/* open() flags */
#define O_RDWR   0x0002
#define O_NOCTTY 0x20000

#define ERR_EINTR   4
#define ERR_ECHILD 10

void *
memcpy(void *dst, const void *src, unsigned long n)
{
	unsigned char *d = (unsigned char *)dst;
	const unsigned char *s = (const unsigned char *)src;
	while (n--) {
		*d++ = *s++;
	}
	return dst;
}

static long
panthera_syscall(long number, long a1, long a2, long a3, long a4, long a5)
{
	long ret;
	unsigned char carry;
	/*
	 * XNU BSD syscall ABI on x86_64:
	 *   number | 0x2000000 in %rax  (class 2 = BSD)
	 *   args in %rdi, %rsi, %rdx, %r10, %r8, %r9
	 *   SYSCALL instruction
	 *   result in %rax, carry flag set on error
	 */
	register long r10 __asm__("r10") = a4;
	register long r8  __asm__("r8")  = a5;
	__asm__ volatile (
		"syscall"
		: "=a"(ret), "=@ccc"(carry)
		: "a"(number | 0x2000000),
		  "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8)
		: "rcx", "r11", "memory", "cc"
	);
	return carry ? -ret : ret;
}

#define sys_exit(code)           panthera_syscall(SYS_exit,  (code), 0, 0, 0, 0)
#define sys_write(fd, buf, len)  panthera_syscall(SYS_write, (fd), (long)(buf), (len), 0, 0)
#define sys_open(path, flags, mode) panthera_syscall(SYS_open, (long)(path), (flags), (mode), 0, 0)
#define sys_close(fd)            panthera_syscall(SYS_close, (fd), 0, 0, 0, 0)
#define sys_wait4(pid, status, options, rusage) panthera_syscall(SYS_wait4, (pid), (long)(status), (options), (long)(rusage), 0)
#define sys_dup2(old, new)       panthera_syscall(SYS_dup2,  (old), (new), 0, 0, 0)
#define sys_execve(path, argv, envp) panthera_syscall(SYS_execve, (long)(path), (long)(argv), (long)(envp), 0, 0)
#define sys_sync()               panthera_syscall(SYS_sync,  0, 0, 0, 0, 0)
#define sys_reboot(how)          panthera_syscall(SYS_reboot, (how), 0, 0, 0, 0)

static unsigned long
str_len(const char *s)
{
	unsigned long n = 0;
	while (*s++) n++;
	return n;
}

static void
puts_console(const char *s)
{
	sys_write(1, s, str_len(s));
}

static void
spin_delay(void)
{
	volatile unsigned long i;

	for (i = 0; i != 50000000UL; i++) {
		__asm__ volatile("" ::: "memory");
	}
}

extern long panthera_fork_child_aware(void);

static void
spawn_shell_child(const char *const envp[])
{
	const char *const sh_argv[] = {
		"/bin/sh",
		(const char *)0
	};
	const char *const zsh_argv[] = {
		"/bin/zsh",
		"+m",
		(const char *)0
	};

	puts_console("mini_launchd: child exec /bin/zsh +m\n");
	sys_execve("/bin/zsh", zsh_argv, envp);

	puts_console("mini_launchd: zsh exec failed, falling back to /bin/sh\n");
	sys_execve("/bin/sh", sh_argv, envp);

	puts_console("mini_launchd: shell exec failed\n");
	sys_exit(127);
}

void _start(void) __attribute__((noreturn));

void
_start(void)
{
	/* Open /dev/console and wire it to fd 0, 1, 2 */
	long fd = sys_open("/dev/console", O_RDWR | O_NOCTTY, 0);
	if (fd >= 0) {
		if (fd != 0) {
			sys_dup2(fd, 0);
		}
		sys_dup2(0, 1);
		sys_dup2(0, 2);
		if (fd > 2) {
			sys_close(fd);
		}
	}

	puts_console(
		"\n"
		"===================================\n"
		"  Panthera Darwin - first light\n"
		"  XNU " __DATE__ " " __TIME__ "\n"
		"===================================\n"
		"\n"
	);

	const char *envp[] = {
		"PATH=/bin:/sbin:/usr/bin:/usr/sbin",
		"HOME=/",
		"PANTHERA_MINIMAL_LAUNCHD=1",
		(const char *)0
	};

	for (;;) {
		long child = panthera_fork_child_aware();
		if (child == 0) {
			spawn_shell_child(envp);
		}
		if (child < 0) {
			puts_console("mini_launchd: fork failed, retrying\n");
			spin_delay();
			continue;
		}

		puts_console("mini_launchd: supervising child shell\n");
		for (;;) {
			int status = 0;
			long waited = sys_wait4(-1, &status, 0, 0);

			if (waited == child) {
				puts_console("mini_launchd: shell exited, respawning\n");
				break;
			}
			if (waited == -ERR_EINTR) {
				continue;
			}
			if (waited == -ERR_ECHILD) {
				puts_console("mini_launchd: no child to reap, respawning\n");
				break;
			}
			if (waited < 0) {
				puts_console("mini_launchd: wait4 failed, respawning\n");
				spin_delay();
				break;
			}
		}
	}
}
