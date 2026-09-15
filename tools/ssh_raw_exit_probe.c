int
main(void)
{
	static const char marker[] = "PANTHERA_SSH_RAW_EXIT\n";
	long ret;

	__asm__ volatile (
	    "syscall"
	    : "=a"(ret)
	    : "a"(0x2000004), "D"(1), "S"(marker), "d"(sizeof(marker) - 1)
	    : "rcx", "r11", "memory", "cc");
	(void)ret;

	static const char after_write[] = "PANTHERA_SSH_RAW_AFTER_WRITE\n";
	__asm__ volatile (
	    "syscall"
	    : "=a"(ret)
	    : "a"(0x2000004), "D"(1), "S"(after_write), "d"(sizeof(after_write) - 1)
	    : "rcx", "r11", "memory", "cc");
	(void)ret;

	__asm__ volatile (
	    "syscall"
	    :
	    : "a"(0x2000001), "D"(0)
	    : "rcx", "r11", "memory", "cc");
	static const char returned[] = "PANTHERA_SSH_RAW_EXIT_RETURNED\n";
	__asm__ volatile (
	    "syscall"
	    : "=a"(ret)
	    : "a"(0x2000004), "D"(2), "S"(returned), "d"(sizeof(returned) - 1)
	    : "rcx", "r11", "memory", "cc");
	(void)ret;
	__builtin_unreachable();
}
