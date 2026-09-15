extern __uint128_t __udivmodti4(__uint128_t a, __uint128_t b, __uint128_t *rem);

__uint128_t
__udivti3(__uint128_t a, __uint128_t b)
{
	return __udivmodti4(a, b, 0);
}
