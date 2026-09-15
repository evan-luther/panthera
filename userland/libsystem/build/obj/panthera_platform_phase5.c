#include <stdint.h>

int32_t
OSAtomicAdd32(int32_t amount, volatile int32_t *value)
{
	return __sync_add_and_fetch(value, amount);
}
