/*
 * mini_sh.c — Minimal interactive shell for Panthera bring-up.
 *
 * Static Mach-O, no libSystem. Uses raw XNU BSD syscalls.
 * Supports: command execution, basic builtins (cd, exit, echo, ls, cat, uname).
 * No pipes, no redirection, no globbing — just enough to prove the system works.
 */

#define SYS_exit        1
#define SYS_fork        2
#define SYS_read        3
#define SYS_write       4
#define SYS_open        5
#define SYS_close       6
#define SYS_wait4       7
#define SYS_chdir       12
#define SYS_getpid      20
#define SYS_sync        36
#define SYS_dup2        90
#define SYS_execve      59
#define SYS_getdirentries64 344
#define SYS_stat64      338
#define SYS_reboot      55

#define O_RDONLY 0x0000
#define O_RDWR   0x0002

#define MAXLINE 512
#define MAXARGS 32

static long
_syscall(long number, long a1, long a2, long a3, long a4, long a5)
{
	long ret;
	register long r10 __asm__("r10") = a4;
	register long r8  __asm__("r8")  = a5;
	__asm__ volatile (
		"syscall"
		: "=a"(ret)
		: "a"(number | 0x2000000),
		  "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8)
		: "rcx", "r11", "memory", "cc"
	);
	return ret;
}

#define sys_exit(c)               _syscall(SYS_exit, (c), 0, 0, 0, 0)
#define sys_fork()                _syscall(SYS_fork, 0, 0, 0, 0, 0)
#define sys_read(fd, b, n)        _syscall(SYS_read, (fd), (long)(b), (n), 0, 0)
#define sys_write(fd, b, n)       _syscall(SYS_write, (fd), (long)(b), (n), 0, 0)
#define sys_open(p, f, m)         _syscall(SYS_open, (long)(p), (f), (m), 0, 0)
#define sys_close(fd)             _syscall(SYS_close, (fd), 0, 0, 0, 0)
#define sys_wait4(pid, s, o, r)   _syscall(SYS_wait4, (pid), (long)(s), (o), (long)(r), 0)
#define sys_chdir(p)              _syscall(SYS_chdir, (long)(p), 0, 0, 0, 0)
#define sys_getpid()              _syscall(SYS_getpid, 0, 0, 0, 0, 0)
#define sys_execve(p, a, e)       _syscall(SYS_execve, (long)(p), (long)(a), (long)(e), 0, 0)
#define sys_dup2(o, n)            _syscall(SYS_dup2, (o), (n), 0, 0, 0)
#define sys_sync()                _syscall(SYS_sync, 0, 0, 0, 0, 0)
#define sys_reboot(h)             _syscall(SYS_reboot, (h), 0, 0, 0, 0)
#define sys_getdirentries64(fd, b, n, bp) _syscall(SYS_getdirentries64, (fd), (long)(b), (n), (long)(bp), 0)

static unsigned long str_len(const char *s) {
	unsigned long n = 0;
	while (*s++) n++;
	return n;
}

static void print(const char *s) {
	sys_write(1, s, str_len(s));
}

static int str_eq(const char *a, const char *b) {
	while (*a && *b && *a == *b) { a++; b++; }
	return *a == *b;
}

static int str_copy(char *dst, const char *src, int max) {
	int i = 0;
	while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
	dst[i] = 0;
	return i;
}

static void print_num(long n) {
	char buf[24];
	int i = sizeof(buf) - 1;
	int neg = 0;
	if (n < 0) { neg = 1; n = -n; }
	buf[i--] = 0;
	if (n == 0) buf[i--] = '0';
	while (n > 0) { buf[i--] = '0' + (n % 10); n /= 10; }
	if (neg) buf[i--] = '-';
	print(&buf[i + 1]);
}

/* Built-in: echo */
static void builtin_echo(int argc, char **argv) {
	for (int i = 1; i < argc; i++) {
		if (i > 1) print(" ");
		print(argv[i]);
	}
	print("\n");
}

/* Built-in: ls (simple — no flags) */
struct dirent64 {
	unsigned long long d_ino;
	unsigned long long d_seekoff;
	unsigned short d_reclen;
	unsigned short d_namlen;
	unsigned char d_type;
	char d_name[1024];
};

static void builtin_ls(int argc, char **argv) {
	const char *path = argc > 1 ? argv[1] : ".";
	long fd = sys_open(path, O_RDONLY, 0);
	if (fd < 0) {
		print("ls: cannot open ");
		print(path);
		print("\n");
		return;
	}
	char buf[4096];
	long long basep = 0;
	long nread;
	while ((nread = sys_getdirentries64(fd, buf, sizeof(buf), &basep)) > 0) {
		long off = 0;
		while (off < nread) {
			struct dirent64 *d = (struct dirent64 *)(buf + off);
			if (d->d_reclen == 0) break;
			print(d->d_name);
			if (d->d_type == 4) print("/"); /* DT_DIR */
			print("  ");
			off += d->d_reclen;
		}
	}
	print("\n");
	sys_close(fd);
}

/* Built-in: cat */
static void builtin_cat(int argc, char **argv) {
	if (argc < 2) { print("usage: cat <file>\n"); return; }
	long fd = sys_open(argv[1], O_RDONLY, 0);
	if (fd < 0) {
		print("cat: cannot open ");
		print(argv[1]);
		print("\n");
		return;
	}
	char buf[4096];
	long n;
	while ((n = sys_read(fd, buf, sizeof(buf))) > 0) {
		sys_write(1, buf, n);
	}
	sys_close(fd);
}

/* Built-in: uname */
static void builtin_uname(void) {
	print("Panthera Darwin (XNU Mach/BSD) x86_64\n");
}

/* Built-in: help */
static void builtin_help(void) {
	print("Panthera mini-shell builtins:\n");
	print("  echo <args>    Print arguments\n");
	print("  ls [dir]       List directory\n");
	print("  cat <file>     Print file contents\n");
	print("  cd <dir>       Change directory\n");
	print("  uname          System info\n");
	print("  sync           Flush buffers\n");
	print("  pid            Print shell PID\n");
	print("  halt           Halt system\n");
	print("  exit           Exit shell\n");
	print("  help           This message\n");
}

static int readline(char *buf, int max) {
	int i = 0;
	while (i < max - 1) {
		char c;
		long n = sys_read(0, &c, 1);
		if (n <= 0) return -1;
		if (c == '\n' || c == '\r') {
			buf[i] = 0;
			print("\n");
			return i;
		}
		if (c == 127 || c == 8) { /* backspace */
			if (i > 0) {
				i--;
				print("\b \b");
			}
			continue;
		}
		if (c >= 32) {
			buf[i++] = c;
			sys_write(1, &c, 1); /* echo */
		}
	}
	buf[i] = 0;
	return i;
}

static int parse_line(char *line, char **argv, int maxargs) {
	int argc = 0;
	char *p = line;
	while (*p && argc < maxargs - 1) {
		while (*p == ' ' || *p == '\t') p++;
		if (!*p) break;
		argv[argc++] = p;
		while (*p && *p != ' ' && *p != '\t') p++;
		if (*p) *p++ = 0;
	}
	argv[argc] = (char *)0;
	return argc;
}

static const char *envp[] = {
	"PATH=/bin:/sbin:/usr/bin:/usr/sbin",
	"HOME=/",
	"TERM=vt100",
	(const char *)0
};

void _start(void) __attribute__((noreturn));

void
_start(void)
{
	char line[MAXLINE];
	char *argv[MAXARGS];

	print("\nPanthera Darwin shell (mini_sh)\nType 'help' for commands.\n\n");

	for (;;) {
		print("panthera# ");
		int len = readline(line, sizeof(line));
		if (len < 0) break;
		if (len == 0) continue;

		int argc = parse_line(line, argv, MAXARGS);
		if (argc == 0) continue;

		/* Builtins */
		if (str_eq(argv[0], "exit")) {
			break;
		} else if (str_eq(argv[0], "cd")) {
			if (argc > 1) {
				if (sys_chdir(argv[1]) < 0) {
					print("cd: failed\n");
				}
			}
		} else if (str_eq(argv[0], "echo")) {
			builtin_echo(argc, argv);
		} else if (str_eq(argv[0], "ls")) {
			builtin_ls(argc, argv);
		} else if (str_eq(argv[0], "cat")) {
			builtin_cat(argc, argv);
		} else if (str_eq(argv[0], "uname")) {
			builtin_uname();
		} else if (str_eq(argv[0], "help")) {
			builtin_help();
		} else if (str_eq(argv[0], "sync")) {
			sys_sync();
			print("sync done\n");
		} else if (str_eq(argv[0], "pid")) {
			print("PID: ");
			print_num(sys_getpid());
			print("\n");
		} else if (str_eq(argv[0], "halt")) {
			print("Halting...\n");
			sys_sync();
			sys_reboot(0x8); /* RB_HALT */
		} else {
			/* Try to exec as external command */
			long pid = sys_fork();
			if (pid == 0) {
				/* Child */
				sys_execve(argv[0], (const char **)argv, envp);
				/* execve failed */
				print("exec failed: ");
				print(argv[0]);
				print("\n");
				sys_exit(127);
			} else if (pid > 0) {
				/* Parent — wait for child */
				int status = 0;
				sys_wait4(pid, &status, 0, 0);
			} else {
				print("fork failed\n");
			}
		}
	}

	print("\nmini_sh: exiting to parent.\n");
	sys_exit(0);
	for (;;) __asm__ volatile("hlt");
}
