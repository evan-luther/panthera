#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <mach-o/dyld.h>

const char ldVersionString[] =
    "@(#)PROGRAM:ld PROJECT:ld64-264.3.102-Panthera\n";

int64_t
OSAtomicAdd64(int64_t amount, volatile int64_t *value)
{
	return __sync_add_and_fetch(value, amount);
}

int
_NSGetExecutablePath(char *buf, uint32_t *bufsize)
{
	const char *path = "/usr/bin/ld";
	uint32_t need = (uint32_t)strlen(path) + 1;

	if (buf == NULL || *bufsize < need) {
		*bufsize = need;
		return -1;
	}
	memcpy(buf, path, need);
	return 0;
}

float
ceilf(float x)
{
	int i = (int)x;

	if ((float)i == x || x < 0.0f)
		return (float)i;
	return (float)(i + 1);
}

double
log2(double x)
{
	union {
		double d;
		uint64_t u;
	} v = { x };
	int exp = (int)((v.u >> 52) & 0x7ff) - 1023;
	double mant = (double)(v.u & ((1ULL << 52) - 1)) / (double)(1ULL << 52);

	return (double)exp + mant * 1.346555814 + mant * mant * -0.360673760;
}

#define F(x, y, z) (((x) & (y)) | ((~(x)) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & (~(z))))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | (~(z))))
#define ROT(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define STEP(f, a, b, c, d, x, t, s) do { \
	(a) += f((b), (c), (d)) + (x) + (uint32_t)(t); \
	(a) = ROT((a), (s)); \
	(a) += (b); \
} while (0)

typedef struct {
	uint32_t a;
	uint32_t b;
	uint32_t c;
	uint32_t d;
	uint64_t bytes;
	unsigned char buf[64];
} MD5_CTX_P;

static void
md5_block(MD5_CTX_P *ctx, const unsigned char *p)
{
	uint32_t a = ctx->a, b = ctx->b, c = ctx->c, d = ctx->d, x[16];

	for (int j = 0; j < 16; j++) {
		x[j] = (uint32_t)p[j * 4] | ((uint32_t)p[j * 4 + 1] << 8) |
		    ((uint32_t)p[j * 4 + 2] << 16) | ((uint32_t)p[j * 4 + 3] << 24);
	}

	STEP(F, a, b, c, d, x[0], 0xd76aa478, 7);
	STEP(F, d, a, b, c, x[1], 0xe8c7b756, 12);
	STEP(F, c, d, a, b, x[2], 0x242070db, 17);
	STEP(F, b, c, d, a, x[3], 0xc1bdceee, 22);
	STEP(F, a, b, c, d, x[4], 0xf57c0faf, 7);
	STEP(F, d, a, b, c, x[5], 0x4787c62a, 12);
	STEP(F, c, d, a, b, x[6], 0xa8304613, 17);
	STEP(F, b, c, d, a, x[7], 0xfd469501, 22);
	STEP(F, a, b, c, d, x[8], 0x698098d8, 7);
	STEP(F, d, a, b, c, x[9], 0x8b44f7af, 12);
	STEP(F, c, d, a, b, x[10], 0xffff5bb1, 17);
	STEP(F, b, c, d, a, x[11], 0x895cd7be, 22);
	STEP(F, a, b, c, d, x[12], 0x6b901122, 7);
	STEP(F, d, a, b, c, x[13], 0xfd987193, 12);
	STEP(F, c, d, a, b, x[14], 0xa679438e, 17);
	STEP(F, b, c, d, a, x[15], 0x49b40821, 22);

	STEP(G, a, b, c, d, x[1], 0xf61e2562, 5);
	STEP(G, d, a, b, c, x[6], 0xc040b340, 9);
	STEP(G, c, d, a, b, x[11], 0x265e5a51, 14);
	STEP(G, b, c, d, a, x[0], 0xe9b6c7aa, 20);
	STEP(G, a, b, c, d, x[5], 0xd62f105d, 5);
	STEP(G, d, a, b, c, x[10], 0x02441453, 9);
	STEP(G, c, d, a, b, x[15], 0xd8a1e681, 14);
	STEP(G, b, c, d, a, x[4], 0xe7d3fbc8, 20);
	STEP(G, a, b, c, d, x[9], 0x21e1cde6, 5);
	STEP(G, d, a, b, c, x[14], 0xc33707d6, 9);
	STEP(G, c, d, a, b, x[3], 0xf4d50d87, 14);
	STEP(G, b, c, d, a, x[8], 0x455a14ed, 20);
	STEP(G, a, b, c, d, x[13], 0xa9e3e905, 5);
	STEP(G, d, a, b, c, x[2], 0xfcefa3f8, 9);
	STEP(G, c, d, a, b, x[7], 0x676f02d9, 14);
	STEP(G, b, c, d, a, x[12], 0x8d2a4c8a, 20);

	STEP(H, a, b, c, d, x[5], 0xfffa3942, 4);
	STEP(H, d, a, b, c, x[8], 0x8771f681, 11);
	STEP(H, c, d, a, b, x[11], 0x6d9d6122, 16);
	STEP(H, b, c, d, a, x[14], 0xfde5380c, 23);
	STEP(H, a, b, c, d, x[1], 0xa4beea44, 4);
	STEP(H, d, a, b, c, x[4], 0x4bdecfa9, 11);
	STEP(H, c, d, a, b, x[7], 0xf6bb4b60, 16);
	STEP(H, b, c, d, a, x[10], 0xbebfbc70, 23);
	STEP(H, a, b, c, d, x[13], 0x289b7ec6, 4);
	STEP(H, d, a, b, c, x[0], 0xeaa127fa, 11);
	STEP(H, c, d, a, b, x[3], 0xd4ef3085, 16);
	STEP(H, b, c, d, a, x[6], 0x04881d05, 23);
	STEP(H, a, b, c, d, x[9], 0xd9d4d039, 4);
	STEP(H, d, a, b, c, x[12], 0xe6db99e5, 11);
	STEP(H, c, d, a, b, x[15], 0x1fa27cf8, 16);
	STEP(H, b, c, d, a, x[2], 0xc4ac5665, 23);

	STEP(I, a, b, c, d, x[0], 0xf4292244, 6);
	STEP(I, d, a, b, c, x[7], 0x432aff97, 10);
	STEP(I, c, d, a, b, x[14], 0xab9423a7, 15);
	STEP(I, b, c, d, a, x[5], 0xfc93a039, 21);
	STEP(I, a, b, c, d, x[12], 0x655b59c3, 6);
	STEP(I, d, a, b, c, x[3], 0x8f0ccc92, 10);
	STEP(I, c, d, a, b, x[10], 0xffeff47d, 15);
	STEP(I, b, c, d, a, x[1], 0x85845dd1, 21);
	STEP(I, a, b, c, d, x[8], 0x6fa87e4f, 6);
	STEP(I, d, a, b, c, x[15], 0xfe2ce6e0, 10);
	STEP(I, c, d, a, b, x[6], 0xa3014314, 15);
	STEP(I, b, c, d, a, x[13], 0x4e0811a1, 21);
	STEP(I, a, b, c, d, x[4], 0xf7537e82, 6);
	STEP(I, d, a, b, c, x[11], 0xbd3af235, 10);
	STEP(I, c, d, a, b, x[2], 0x2ad7d2bb, 15);
	STEP(I, b, c, d, a, x[9], 0xeb86d391, 21);

	ctx->a += a;
	ctx->b += b;
	ctx->c += c;
	ctx->d += d;
}

static void
md5_update(MD5_CTX_P *ctx, const void *data, uint32_t len)
{
	const unsigned char *p = data;
	uint32_t used = (uint32_t)(ctx->bytes & 63);

	ctx->bytes += len;
	if (used) {
		uint32_t free_bytes = 64 - used;
		if (len < free_bytes) {
			memcpy(ctx->buf + used, p, len);
			return;
		}
		memcpy(ctx->buf + used, p, free_bytes);
		md5_block(ctx, ctx->buf);
		p += free_bytes;
		len -= free_bytes;
	}
	while (len >= 64) {
		md5_block(ctx, p);
		p += 64;
		len -= 64;
	}
	if (len)
		memcpy(ctx->buf, p, len);
}

unsigned char *
CC_MD5(const void *data, uint32_t len, unsigned char *md)
{
	MD5_CTX_P ctx = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0, { 0 } };
	uint64_t bits;
	uint32_t used;
	uint32_t vals[4];

	md5_update(&ctx, data, len);
	bits = ctx.bytes * 8;
	used = (uint32_t)(ctx.bytes & 63);
	ctx.buf[used++] = 0x80;
	if (used > 56) {
		memset(ctx.buf + used, 0, 64 - used);
		md5_block(&ctx, ctx.buf);
		used = 0;
	}
	memset(ctx.buf + used, 0, 56 - used);
	for (int i = 0; i < 8; i++)
		ctx.buf[56 + i] = (unsigned char)(bits >> (8 * i));
	md5_block(&ctx, ctx.buf);

	vals[0] = ctx.a;
	vals[1] = ctx.b;
	vals[2] = ctx.c;
	vals[3] = ctx.d;
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			md[i * 4 + j] = (unsigned char)(vals[i] >> (8 * j));
	return md;
}
