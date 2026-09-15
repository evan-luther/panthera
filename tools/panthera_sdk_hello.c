#include <unistd.h>

int
main(void)
{
    const char marker[] = "PANTHERA_SDK_HELLO\n";
    if (write(STDOUT_FILENO, marker, sizeof(marker) - 1) < 0)
        return 1;
    return 0;
}
