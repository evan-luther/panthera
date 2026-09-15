/* Panthera shim: os/collections.h
 * Provides os_set and os_map types used by Libnotify's table.c.
 * Implemented as simple open-addressing hash tables.
 */
#ifndef _OS_COLLECTIONS_H
#define _OS_COLLECTIONS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifndef OS_NOESCAPE
#define OS_NOESCAPE __attribute__((__noescape__))
#endif

#define _OSET_INITIAL_CAP 32

/* ---- String hash ---- */
static inline uint32_t _os_hash_str(const char *s) {
    uint32_t h = 5381;
    while (*s) h = h * 33 + (uint8_t)*s++;
    return h;
}

/* ---- String-keyed set ---- */
typedef struct {
    void **buckets;
    uint32_t count;
    uint32_t capacity;
    uint32_t mask;
} os_set_str_ptr_t;

static inline void _os_set_str_init(os_set_str_ptr_t *s, void *unused) {
    (void)unused;
    s->capacity = _OSET_INITIAL_CAP;
    s->mask = s->capacity - 1;
    s->count = 0;
    s->buckets = (void **)calloc(s->capacity, sizeof(void *));
}

static inline void _os_set_str_insert(os_set_str_ptr_t *s, void *item) {
    if (s->count * 4 >= s->capacity * 3) {
        uint32_t old_cap = s->capacity;
        void **old = s->buckets;
        s->capacity *= 2;
        s->mask = s->capacity - 1;
        s->buckets = (void **)calloc(s->capacity, sizeof(void *));
        s->count = 0;
        for (uint32_t i = 0; i < old_cap; i++)
            if (old[i]) _os_set_str_insert(s, old[i]);
        free(old);
    }
    const char *k = *(const char **)item;
    uint32_t h = _os_hash_str(k) & s->mask;
    while (s->buckets[h]) {
        if (strcmp(*(const char **)s->buckets[h], k) == 0) {
            s->buckets[h] = item;
            return;
        }
        h = (h + 1) & s->mask;
    }
    s->buckets[h] = item;
    s->count++;
}

static inline void *_os_set_str_find(os_set_str_ptr_t *s, const char *key) {
    if (!s->buckets) return NULL;
    uint32_t h = _os_hash_str(key) & s->mask;
    for (uint32_t i = 0; i < s->capacity; i++) {
        void *b = s->buckets[h];
        if (!b) return NULL;
        if (strcmp(*(const char **)b, key) == 0) return b;
        h = (h + 1) & s->mask;
    }
    return NULL;
}

static inline void *_os_set_str_delete(os_set_str_ptr_t *s, const char *key) {
    if (!s->buckets) return NULL;
    uint32_t h = _os_hash_str(key) & s->mask;
    for (uint32_t i = 0; i < s->capacity; i++) {
        void *b = s->buckets[h];
        if (!b) return NULL;
        if (strcmp(*(const char **)b, key) == 0) {
            s->buckets[h] = NULL;
            s->count--;
            uint32_t j = (h + 1) & s->mask;
            while (s->buckets[j]) {
                void *d = s->buckets[j];
                s->buckets[j] = NULL;
                s->count--;
                _os_set_str_insert(s, d);
                j = (j + 1) & s->mask;
            }
            return b;
        }
        h = (h + 1) & s->mask;
    }
    return NULL;
}

/* ---- 32-bit keyed set ---- */
typedef struct {
    void **buckets;
    uint32_t count;
    uint32_t capacity;
    uint32_t mask;
} os_set_32_ptr_t;

static inline void _os_set_32_init(os_set_32_ptr_t *s, void *unused) {
    (void)unused;
    s->capacity = _OSET_INITIAL_CAP;
    s->mask = s->capacity - 1;
    s->count = 0;
    s->buckets = (void **)calloc(s->capacity, sizeof(void *));
}

static inline void _os_set_32_insert(os_set_32_ptr_t *s, void *item) {
    if (s->count * 4 >= s->capacity * 3) {
        uint32_t old_cap = s->capacity;
        void **old = s->buckets;
        s->capacity *= 2;
        s->mask = s->capacity - 1;
        s->buckets = (void **)calloc(s->capacity, sizeof(void *));
        s->count = 0;
        for (uint32_t i = 0; i < old_cap; i++)
            if (old[i]) _os_set_32_insert(s, old[i]);
        free(old);
    }
    uint32_t k = *(uint32_t *)item;
    uint32_t h = (k * 2654435761u) & s->mask;
    while (s->buckets[h]) {
        if (*(uint32_t *)s->buckets[h] == k) {
            s->buckets[h] = item;
            return;
        }
        h = (h + 1) & s->mask;
    }
    s->buckets[h] = item;
    s->count++;
}

static inline void *_os_set_32_find(os_set_32_ptr_t *s, uint32_t key) {
    if (!s->buckets) return NULL;
    uint32_t h = (key * 2654435761u) & s->mask;
    for (uint32_t i = 0; i < s->capacity; i++) {
        void *b = s->buckets[h];
        if (!b) return NULL;
        if (*(uint32_t *)b == key) return b;
        h = (h + 1) & s->mask;
    }
    return NULL;
}

static inline void *_os_set_32_delete(os_set_32_ptr_t *s, uint32_t key) {
    if (!s->buckets) return NULL;
    uint32_t h = (key * 2654435761u) & s->mask;
    for (uint32_t i = 0; i < s->capacity; i++) {
        void *b = s->buckets[h];
        if (!b) return NULL;
        if (*(uint32_t *)b == key) {
            s->buckets[h] = NULL;
            s->count--;
            uint32_t j = (h + 1) & s->mask;
            while (s->buckets[j]) {
                void *d = s->buckets[j];
                s->buckets[j] = NULL;
                s->count--;
                _os_set_32_insert(s, d);
                j = (j + 1) & s->mask;
            }
            return b;
        }
        h = (h + 1) & s->mask;
    }
    return NULL;
}

/* ---- 64-bit keyed set ---- */
typedef struct {
    void **buckets;
    uint32_t count;
    uint32_t capacity;
    uint32_t mask;
} os_set_64_ptr_t;

static inline void _os_set_64_init(os_set_64_ptr_t *s, void *unused) {
    (void)unused;
    s->capacity = _OSET_INITIAL_CAP;
    s->mask = s->capacity - 1;
    s->count = 0;
    s->buckets = (void **)calloc(s->capacity, sizeof(void *));
}

static inline void _os_set_64_insert(os_set_64_ptr_t *s, void *item) {
    if (s->count * 4 >= s->capacity * 3) {
        uint32_t old_cap = s->capacity;
        void **old = s->buckets;
        s->capacity *= 2;
        s->mask = s->capacity - 1;
        s->buckets = (void **)calloc(s->capacity, sizeof(void *));
        s->count = 0;
        for (uint32_t i = 0; i < old_cap; i++)
            if (old[i]) _os_set_64_insert(s, old[i]);
        free(old);
    }
    uint64_t k = *(uint64_t *)item;
    uint32_t h = (uint32_t)((k * 11400714819323198485llu) >> 32) & s->mask;
    while (s->buckets[h]) {
        if (*(uint64_t *)s->buckets[h] == k) {
            s->buckets[h] = item;
            return;
        }
        h = (h + 1) & s->mask;
    }
    s->buckets[h] = item;
    s->count++;
}

static inline void *_os_set_64_find(os_set_64_ptr_t *s, uint64_t key) {
    if (!s->buckets) return NULL;
    uint32_t h = (uint32_t)((key * 11400714819323198485llu) >> 32) & s->mask;
    for (uint32_t i = 0; i < s->capacity; i++) {
        void *b = s->buckets[h];
        if (!b) return NULL;
        if (*(uint64_t *)b == key) return b;
        h = (h + 1) & s->mask;
    }
    return NULL;
}

static inline void *_os_set_64_delete(os_set_64_ptr_t *s, uint64_t key) {
    if (!s->buckets) return NULL;
    uint32_t h = (uint32_t)((key * 11400714819323198485llu) >> 32) & s->mask;
    for (uint32_t i = 0; i < s->capacity; i++) {
        void *b = s->buckets[h];
        if (!b) return NULL;
        if (*(uint64_t *)b == key) {
            s->buckets[h] = NULL;
            s->count--;
            uint32_t j = (h + 1) & s->mask;
            while (s->buckets[j]) {
                void *d = s->buckets[j];
                s->buckets[j] = NULL;
                s->count--;
                _os_set_64_insert(s, d);
                j = (j + 1) & s->mask;
            }
            return b;
        }
        h = (h + 1) & s->mask;
    }
    return NULL;
}

/* ---- 64-bit keyed map ---- */
typedef struct {
    struct _os_map_entry_64 {
        uint64_t key;
        void *value;
    } *entries;
    uint32_t count;
    uint32_t capacity;
    uint32_t mask;
} os_map_64_t;

static inline void _os_map_64_init(os_map_64_t *m, void *unused) {
    (void)unused;
    m->capacity = _OSET_INITIAL_CAP;
    m->mask = m->capacity - 1;
    m->count = 0;
    m->entries = (struct _os_map_entry_64 *)calloc(m->capacity, sizeof(struct _os_map_entry_64));
}

static inline void _os_map_64_insert(os_map_64_t *m, uint64_t key, void *value) {
    if (m->count * 4 >= m->capacity * 3) {
        uint32_t old_cap = m->capacity;
        struct _os_map_entry_64 *old = m->entries;
        m->capacity *= 2;
        m->mask = m->capacity - 1;
        m->entries = (struct _os_map_entry_64 *)calloc(m->capacity, sizeof(struct _os_map_entry_64));
        m->count = 0;
        for (uint32_t i = 0; i < old_cap; i++)
            if (old[i].value) _os_map_64_insert(m, old[i].key, old[i].value);
        free(old);
    }
    uint32_t h = (uint32_t)((key * 11400714819323198485llu) >> 32) & m->mask;
    while (m->entries[h].value) {
        if (m->entries[h].key == key) {
            m->entries[h].value = value;
            return;
        }
        h = (h + 1) & m->mask;
    }
    m->entries[h].key = key;
    m->entries[h].value = value;
    m->count++;
}

/* ---- Generic macros using _Generic ---- */

#define os_set_init(s, unused) _Generic((s), \
    os_set_str_ptr_t *: _os_set_str_init, \
    os_set_32_ptr_t *:  _os_set_32_init, \
    os_set_64_ptr_t *:  _os_set_64_init  \
)(s, NULL)

#define os_set_insert(s, item) _Generic((s), \
    os_set_str_ptr_t *: _os_set_str_insert, \
    os_set_32_ptr_t *:  _os_set_32_insert, \
    os_set_64_ptr_t *:  _os_set_64_insert  \
)((s), (item))

#define os_set_find(s, key) _Generic((key), \
    char *:         _os_set_str_find, \
    const char *:   _os_set_str_find, \
    uint32_t:       _os_set_32_find, \
    uint64_t:       _os_set_64_find  \
)((s), (key))

#define os_set_delete(s, key) _Generic((key), \
    char *:         _os_set_str_delete, \
    const char *:   _os_set_str_delete, \
    uint32_t:       _os_set_32_delete, \
    uint64_t:       _os_set_64_delete  \
)((s), (key))

#define os_set_count(s) ((s)->count)

#define os_set_foreach(s, block) do { \
    for (uint32_t _i = 0; _i < (s)->capacity; _i++) { \
        if ((s)->buckets[_i]) { \
            if (!block((s)->buckets[_i])) break; \
        } \
    } \
} while(0)

#define os_map_init(m, unused) _os_map_64_init(m, NULL)
#define os_map_insert(m, k, v) _os_map_64_insert(m, k, v)

static inline void *_os_map_64_find(os_map_64_t *m, uint64_t key) {
    if (!m->entries) return NULL;
    uint32_t h = (uint32_t)((key * 11400714819323198485llu) >> 32) & m->mask;
    for (uint32_t i = 0; i < m->capacity; i++) {
        if (!m->entries[h].value) return NULL;
        if (m->entries[h].key == key) return m->entries[h].value;
        h = (h + 1) & m->mask;
    }
    return NULL;
}

static inline void *_os_map_64_delete(os_map_64_t *m, uint64_t key) {
    if (!m->entries) return NULL;
    uint32_t h = (uint32_t)((key * 11400714819323198485llu) >> 32) & m->mask;
    for (uint32_t i = 0; i < m->capacity; i++) {
        if (!m->entries[h].value) return NULL;
        if (m->entries[h].key == key) {
            void *v = m->entries[h].value;
            m->entries[h].value = NULL;
            m->count--;
            uint32_t j = (h + 1) & m->mask;
            while (m->entries[j].value) {
                uint64_t dk = m->entries[j].key;
                void *dv = m->entries[j].value;
                m->entries[j].value = NULL;
                m->count--;
                _os_map_64_insert(m, dk, dv);
                j = (j + 1) & m->mask;
            }
            return v;
        }
        h = (h + 1) & m->mask;
    }
    return NULL;
}

#define os_map_find(m, k) _os_map_64_find(m, k)
#define os_map_delete(m, k) _os_map_64_delete(m, k)

#endif /* _OS_COLLECTIONS_H */
