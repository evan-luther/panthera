#include <mach/kern_return.h>
#include <mach/kmod.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/systm.h>

#include <corecrypto/ccdigest.h>
#include <corecrypto/cchmac.h>
#include <corecrypto/ccmode.h>
#include <corecrypto/ccrng.h>
#include <corecrypto/ccsha1.h>
#include <corecrypto/ccsha2.h>
#include <libkern/crypto/register_crypto.h>

extern const struct ccdigest_info pdcmd5_di;
extern const struct ccdigest_info ccsha1_ltc_di;
extern const struct ccdigest_info ccsha256_ltc_di;
extern const struct ccdigest_info ccsha384_ltc_di;
extern const struct ccdigest_info ccsha512_ltc_di;
extern const struct ccmode_ecb pdcaes_ecb_encrypt;
extern const struct ccmode_ecb pdcaes_ecb_decrypt;
extern const struct ccmode_cbc pdcaes_cbc_encrypt;
extern const struct ccmode_cbc pdcaes_cbc_decrypt;

extern uint64_t early_random(void);
extern void read_frandom(void *buffer, unsigned int numBytes);

struct panthera_rng_state {
	CCRNG_STATE_COMMON
};

struct panthera_kmem_random_ctx {
	uint64_t state;
};

static uint64_t
panthera_splitmix64_next(uint64_t *state)
{
	uint64_t z;

	*state += 0x9e3779b97f4a7c15ULL;
	z = *state;
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
	return z ^ (z >> 31);
}

static int
panthera_ccrng_generate(struct ccrng_state *rng __unused, size_t outlen, void *out)
{
	while (outlen > 0) {
		unsigned int chunk = outlen > UINT_MAX ? UINT_MAX : (unsigned int)outlen;

		read_frandom(out, chunk);
		out = (char *)out + chunk;
		outlen -= chunk;
	}

	return 0;
}

static struct ccrng_state *
panthera_ccrng(int *error)
{
	static struct panthera_rng_state rng = {
		.generate = panthera_ccrng_generate,
	};

	if (error != NULL) {
		*error = 0;
	}
	return (struct ccrng_state *)&rng;
}

static void
panthera_digest_final(const struct ccdigest_info *di, ccdigest_ctx_t ctx, void *digest)
{
	ccdigest_final(di, ctx, digest);
}

static void
panthera_digest(const struct ccdigest_info *di, unsigned long len, const void *data, void *digest)
{
	ccdigest_di_decl(di, ctx);

	ccdigest_init(di, ctx);
	ccdigest_update(di, ctx, len, data);
	ccdigest_final(di, ctx, digest);
	ccdigest_di_clear(di, ctx);
}

static void
panthera_random_generate(crypto_random_ctx_t ctx __unused, void *random,
    size_t random_size)
{
	while (random_size > 0) {
		unsigned int chunk = random_size > UINT_MAX ? UINT_MAX : (unsigned int)random_size;

		read_frandom(random, chunk);
		random = (char *)random + chunk;
		random_size -= chunk;
	}
}

static void
panthera_random_uniform(crypto_random_ctx_t ctx, uint64_t bound, uint64_t *random)
{
	uint64_t limit;

	if (bound == 0) {
		*random = 0;
		return;
	}

	limit = UINT64_MAX - (UINT64_MAX % bound);
	do {
		panthera_random_generate(ctx, random, sizeof(*random));
	} while (*random >= limit);

	*random %= bound;
}

static size_t
panthera_random_kmem_ctx_size(void)
{
	return sizeof(struct panthera_kmem_random_ctx);
}

static void
panthera_random_kmem_init(crypto_random_ctx_t ctx)
{
	struct panthera_kmem_random_ctx *state = ctx;

	if (state != NULL) {
		read_frandom(&state->state, sizeof(state->state));
		if (state->state == 0) {
			state->state = early_random();
		}
	}
}

static void
panthera_random_kmem_generate(crypto_random_ctx_t ctx, void *random,
    size_t random_size)
{
	struct panthera_kmem_random_ctx *state = ctx;
	uint8_t *out = random;

	if (state == NULL) {
		panthera_random_generate(ctx, random, random_size);
		return;
	}

	while (random_size > 0) {
		uint64_t value = panthera_splitmix64_next(&state->state);
		size_t chunk = random_size < sizeof(value) ? random_size : sizeof(value);

		memcpy(out, &value, chunk);
		out += chunk;
		random_size -= chunk;
	}
}

static void
panthera_hmac_final_verify_unavailable(crypto_digest_alg_t alg __unused,
    void *ctx __unused, size_t ctx_size __unused, const void *tag __unused,
    size_t tag_size __unused)
{
}

static const struct crypto_functions panthera_crypto_functions = {
	.ccdigest_init_fn = ccdigest_init,
	.ccdigest_update_fn = ccdigest_update,
	.ccdigest_final_fn = panthera_digest_final,
	.ccdigest_fn = panthera_digest,

	.ccmd5_di = &pdcmd5_di,
	.ccsha1_di = &ccsha1_ltc_di,
	.ccsha256_di = &ccsha256_ltc_di,
	.ccsha384_di = &ccsha384_ltc_di,
	.ccsha512_di = &ccsha512_ltc_di,

	.cchmac_init_fn = cchmac_init,
	.cchmac_update_fn = cchmac_update,
	.cchmac_final_fn = cchmac_final,
	.cchmac_fn = cchmac,

	.ccaes_ecb_encrypt = &pdcaes_ecb_encrypt,
	.ccaes_ecb_decrypt = &pdcaes_ecb_decrypt,
	.ccaes_cbc_encrypt = &pdcaes_cbc_encrypt,
	.ccaes_cbc_decrypt = &pdcaes_cbc_decrypt,

	.ccrng_fn = panthera_ccrng,

	.random_generate_fn = panthera_random_generate,
	.random_uniform_fn = panthera_random_uniform,
	.random_kmem_ctx_size_fn = panthera_random_kmem_ctx_size,
	.random_kmem_init_fn = panthera_random_kmem_init,
};

kern_return_t
panthera_corecrypto_start(kmod_info_t *ki __unused, void *data __unused)
{
	int ret = register_crypto_functions(
	    (crypto_functions_t)&panthera_crypto_functions);

	if (ret == -1) {
		printf("corecrypto: provider already registered\n");
	} else {
		printf("corecrypto: Panthera provider registered\n");
	}

	return KERN_SUCCESS;
}

kern_return_t
panthera_corecrypto_stop(kmod_info_t *ki __unused, void *data __unused)
{
	return KERN_FAILURE;
}
