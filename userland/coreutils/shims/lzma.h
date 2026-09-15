/* Stub lzma.h — Panthera doesn't ship liblzma; disable xz support in grep */
#ifndef _PANTHERA_LZMA_STUB_H
#define _PANTHERA_LZMA_STUB_H

#include <stdint.h>
#include <string.h>

#define LZMA_STREAM_INIT {0}
#define LZMA_OK 0
#define LZMA_STREAM_END 1
#define LZMA_RUN 0
#define LZMA_FINISH 1
#define LZMA_CONCATENATED 0
#ifndef UINT64_MAX
#define UINT64_MAX __UINT64_MAX__
#endif

typedef int lzma_ret;
typedef int lzma_action;
typedef struct {
    const uint8_t *next_in;
    size_t avail_in;
    uint8_t *next_out;
    size_t avail_out;
} lzma_stream;

static inline lzma_ret lzma_code(lzma_stream *s __attribute__((unused)),
    lzma_action a __attribute__((unused))) { return LZMA_STREAM_END; }
static inline lzma_ret lzma_stream_decoder(lzma_stream *s __attribute__((unused)),
    uint64_t m __attribute__((unused)), uint32_t f __attribute__((unused))) { return LZMA_OK; }
static inline lzma_ret lzma_alone_decoder(lzma_stream *s __attribute__((unused)),
    uint64_t m __attribute__((unused))) { return LZMA_OK; }
static inline void lzma_end(lzma_stream *s __attribute__((unused))) {}

#endif
