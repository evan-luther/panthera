/*
 * Copyright (c) 2016 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 *
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */

#include <libkern/crypto/crypto_internal.h>
#include <corecrypto/ccrng.h>
#include <libkern/crypto/rand.h>
#include <string.h>

extern uint64_t early_random(void);

struct panthera_crypto_random_fallback_ctx {
	uint64_t state;
};

static uint64_t
panthera_crypto_random_fallback_next(crypto_random_ctx_t ctx)
{
	struct panthera_crypto_random_fallback_ctx *state = ctx;
	uint64_t x = state && state->state ? state->state : early_random();
	uint64_t z;

	x += 0x9e3779b97f4a7c15ULL;
	z = x;
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
	z ^= z >> 31;

	if (state) {
		state->state = x;
	}

	return z;
}

static void
panthera_crypto_random_fallback_generate(
	crypto_random_ctx_t ctx,
	void *random,
	size_t random_size)
{
	uint8_t *out = random;

	while (random_size > 0) {
		uint64_t value = panthera_crypto_random_fallback_next(ctx);
		size_t chunk = random_size < sizeof(value) ? random_size : sizeof(value);

		memcpy(out, &value, chunk);
		out += chunk;
		random_size -= chunk;
	}
}

static void
panthera_crypto_random_fallback_uniform(
	crypto_random_ctx_t ctx,
	uint64_t bound,
	uint64_t *random)
{
	if (bound == 0) {
		*random = 0;
		return;
	}

	*random = panthera_crypto_random_fallback_next(ctx) % bound;
}

int
cc_rand_generate(void *out, size_t outlen)
{
	struct ccrng_state *rng_state = NULL;
	int error = -1;

	if (g_crypto_funcs) {
		rng_state = g_crypto_funcs->ccrng_fn(&error);
		if (rng_state != NULL) {
			error = ccrng_generate(rng_state, outlen, out);
		}
	}

	return error;
}

int
random_buf(void *buf, size_t buflen)
{
	return cc_rand_generate(buf, buflen);
}

void
crypto_random_generate(
	crypto_random_ctx_t ctx,
	void *random,
	size_t random_size)
{
	if (!g_crypto_funcs) {
		panthera_crypto_random_fallback_generate(ctx, random, random_size);
		return;
	}

	g_crypto_funcs->random_generate_fn(ctx, random, random_size);
}

void
crypto_random_uniform(
	crypto_random_ctx_t ctx,
	uint64_t bound,
	uint64_t *random)
{
	if (!g_crypto_funcs) {
		panthera_crypto_random_fallback_uniform(ctx, bound, random);
		return;
	}

	g_crypto_funcs->random_uniform_fn(ctx, bound, random);
}

size_t
crypto_random_kmem_ctx_size(void)
{
	if (!g_crypto_funcs) {
		return sizeof(struct panthera_crypto_random_fallback_ctx);
	}

	return g_crypto_funcs->random_kmem_ctx_size_fn();
}

void
crypto_random_kmem_init(
	crypto_random_ctx_t ctx)
{
	if (!g_crypto_funcs) {
		struct panthera_crypto_random_fallback_ctx *state = ctx;

		if (state) {
			state->state = early_random();
		}
		return;
	}

	g_crypto_funcs->random_kmem_init_fn(ctx);
}
