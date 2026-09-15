#include <unistd.h>

int main(void)
{
    static const char msg[] = "EXIT_OK\n";
    write(1, msg, sizeof(msg) - 1);
    return 0;
}
