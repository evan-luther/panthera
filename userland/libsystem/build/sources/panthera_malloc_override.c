/*
 * panthera_malloc_override.c — Simple working malloc for Panthera
 *
 * Apple's libsystem_malloc requires complex initialization (zone setup,
 * nanov2, etc.) that depends on many other subsystems being ready.
 * This provides a simple working malloc using mmap that bootstraps
 * the system until we can properly initialize Apple's allocator.
 *
 * Uses a simple arena allocator backed by mmap'd pages.
 */

#include <stdint.h>
#include <stddef.h>

/* Raw BSD syscall interface */
static long _sys(long num, long a1, long a2, long a3, long a4, long a5, long a6) {
    long ret;
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    register long r9  __asm__("r9")  = a6;
    __asm__ volatile("syscall"
        : "=a"(ret)
        : "a"(num | 0x2000000), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory", "cc");
    return ret;
}

#define SYS_mmap   197
#define SYS_munmap 73
#define PROT_RW    3 /* PROT_READ | PROT_WRITE */
#define MAP_ANON_PRIV 0x1002 /* MAP_ANON | MAP_PRIVATE */

/* Arena: a region of mmap'd memory with a bump pointer */
#define ARENA_SIZE (16 * 1024 * 1024) /* 16 MB per arena */

struct arena {
    struct arena *next;
    char *base;
    size_t used;
    size_t capacity;
};

static struct arena *_current_arena = 0;
static uint32_t _malloc_lock = 0;

static void _lock(void) {
    while (__sync_lock_test_and_set(&_malloc_lock, 1))
        __asm__ volatile("pause");
}
static void _unlock(void) { __sync_lock_release(&_malloc_lock); }

static struct arena *_new_arena(size_t min_size) {
    size_t sz = min_size > ARENA_SIZE ? min_size + 4096 : ARENA_SIZE;
    sz = (sz + 4095) & ~4095UL; /* page align */

    /* mmap the arena (includes space for the arena header) */
    long p = _sys(SYS_mmap, 0, (long)(sz + sizeof(struct arena)),
                  PROT_RW, MAP_ANON_PRIV, -1, 0);
    if (p < 0 || p == 0) return 0;

    struct arena *a = (struct arena *)p;
    a->base = (char *)p + sizeof(struct arena);
    a->used = 0;
    a->capacity = sz;
    a->next = _current_arena;
    _current_arena = a;
    return a;
}

/* Allocation header: size + magic for free/realloc */
struct alloc_header {
    size_t size;
    uint32_t magic;
    uint32_t _pad;
};
#define ALLOC_MAGIC 0xAA55AA55

void *malloc(size_t size) {
    if (size == 0) size = 1;
    size_t total = sizeof(struct alloc_header) + ((size + 15) & ~15UL);

    _lock();

    /* Large allocations: use dedicated mmap */
    if (total > ARENA_SIZE / 2) {
        _unlock();
        size_t mmap_sz = (total + 4095) & ~4095UL;
        long p = _sys(SYS_mmap, 0, (long)mmap_sz, PROT_RW, MAP_ANON_PRIV, -1, 0);
        if (p < 0 || p == 0) return 0;
        struct alloc_header *h = (struct alloc_header *)p;
        h->size = mmap_sz;
        h->magic = ALLOC_MAGIC;
        return (void *)(h + 1);
    }

    /* Try current arena */
    struct arena *a = _current_arena;
    if (!a || a->used + total > a->capacity) {
        a = _new_arena(total);
        if (!a) { _unlock(); return 0; }
    }

    struct alloc_header *h = (struct alloc_header *)(a->base + a->used);
    a->used += total;
    h->size = total;
    h->magic = ALLOC_MAGIC;

    _unlock();
    return (void *)(h + 1);
}

void *calloc(size_t count, size_t size) {
    size_t total = count * size;
    void *p = malloc(total);
    if (p) {
        /* mmap gives zeroed memory, but arena reuse might not */
        char *c = (char *)p;
        for (size_t i = 0; i < total; i++) c[i] = 0;
    }
    return p;
}

void free(void *ptr) {
    if (!ptr) return;
    /* For large allocations (direct mmap), we could munmap.
     * For arena allocations, we just leak (arena freed in bulk).
     * This is fine for bootstrap — real malloc takes over later. */
    struct alloc_header *h = (struct alloc_header *)ptr - 1;
    if (h->magic == ALLOC_MAGIC && h->size > ARENA_SIZE / 2) {
        /* Large allocation — munmap it */
        _sys(SYS_munmap, (long)h, (long)h->size, 0, 0, 0, 0);
    }
    /* Small arena allocations: no-op free (bump allocator) */
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return 0; }

    struct alloc_header *h = (struct alloc_header *)ptr - 1;
    size_t old_size = 0;
    if (h->magic == ALLOC_MAGIC) {
        old_size = h->size - sizeof(struct alloc_header);
    }

    void *new_ptr = malloc(size);
    if (new_ptr && old_size > 0) {
        size_t copy = old_size < size ? old_size : size;
        char *d = (char *)new_ptr;
        char *s = (char *)ptr;
        for (size_t i = 0; i < copy; i++) d[i] = s[i];
    }
    return new_ptr;
}

void *reallocf(void *ptr, size_t size) {
    void *r = realloc(ptr, size);
    if (!r && size > 0) free(ptr);
    return r;
}

void *valloc(size_t size) {
    return malloc(size);
}

int posix_memalign(void **memptr, size_t alignment, size_t size) {
    (void)alignment;
    *memptr = malloc(size);
    return *memptr ? 0 : 12; /* ENOMEM */
}

void *aligned_alloc(size_t alignment, size_t size) {
    (void)alignment;
    return malloc(size);
}

size_t malloc_size(const void *ptr) {
    if (!ptr) return 0;
    const struct alloc_header *h = (const struct alloc_header *)ptr - 1;
    if (h->magic == ALLOC_MAGIC) return h->size - sizeof(struct alloc_header);
    return 0;
}

size_t malloc_good_size(size_t size) {
    return (size + 15) & ~15UL;
}

/* $DARWIN_EXTSN alias */
__asm__(".globl _reallocarray$DARWIN_EXTSN\n_reallocarray$DARWIN_EXTSN = _realloc");
