#include <stdarg.h>
#include <sys/types.h>
#include <unistd.h>

extern int __chmod(const char *path, mode_t mode);
extern int __reboot(int howto);
extern int __syscall(int number, ...);

int
chmod(const char *path, mode_t mode)
{
	return __chmod(path, mode);
}

__asm__(".globl _chmod$UNIX2003\n_chmod$UNIX2003 = _chmod");

int
reboot(int howto)
{
	return __reboot(howto);
}

int
syscall(int number, ...)
{
	va_list ap;
	long a1 = 0;
	long a2 = 0;
	long a3 = 0;
	long a4 = 0;
	long a5 = 0;
	long a6 = 0;

	va_start(ap, number);
	a1 = va_arg(ap, long);
	a2 = va_arg(ap, long);
	a3 = va_arg(ap, long);
	a4 = va_arg(ap, long);
	a5 = va_arg(ap, long);
	a6 = va_arg(ap, long);
	va_end(ap);

	return __syscall(number, a1, a2, a3, a4, a5, a6);
}
