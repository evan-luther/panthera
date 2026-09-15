#include <unistd.h>

int
main(void)
{
	static const char clear_seq[] = "\033[2J\033[H";

	return write(STDOUT_FILENO, clear_seq, sizeof(clear_seq) - 1) < 0;
}
