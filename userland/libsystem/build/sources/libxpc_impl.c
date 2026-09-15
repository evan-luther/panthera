/*
 * libxpc_impl.c — Real XPC object implementation for Panthera
 *
 * Implements:
 *   - XPC object model (dictionary, array, string, int64, uint64, bool, etc.)
 *   - Refcounting (atomic)
 *   - Type introspection
 *   - Debug descriptions
 *   - Serialization (XPC dictionary ↔ flat buffer for Mach messages)
 *   - Mach IPC connections (bootstrap look-up/check-in, send/receive)
 *   - xpc_main for hosting XPC services
 *
 * Does NOT use CoreFoundation internally to avoid circular dependency.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <mach/mach.h>
#include <mach/ndr.h>
#include <dispatch/dispatch.h>
#include <Block.h>

/* ---- Internal type tags ---- */
#define _XPC_TAG_NULL       0x1000
#define _XPC_TAG_BOOL       0x2000
#define _XPC_TAG_INT64      0x3000
#define _XPC_TAG_UINT64     0x4000
#define _XPC_TAG_DOUBLE     0x5000
#define _XPC_TAG_STRING     0x6000
#define _XPC_TAG_DATA       0x7000
#define _XPC_TAG_DICTIONARY 0x8000
#define _XPC_TAG_ARRAY      0x9000
#define _XPC_TAG_FD         0xA000
#define _XPC_TAG_MACH_SEND  0xB000
#define _XPC_TAG_MACH_RECV  0xC000
#define _XPC_TAG_CONNECTION 0xD000
#define _XPC_TAG_ERROR      0xF000

/* ---- Object struct ---- */
struct xpc_object_s {
    uint32_t tag;
    volatile int32_t refcount;
    mach_port_t _reply_port;   /* for received dicts: reply port from Mach header */
    union {
        bool bool_val;
        int64_t int64_val;
        uint64_t uint64_val;
        double double_val;
        struct { char *ptr; size_t len; } string;
        struct { uint8_t *ptr; size_t len; } data;
        mach_port_t port;
        int fd;
        struct {
            char **keys;
            struct xpc_object_s **values;
            size_t count;
            size_t capacity;
        } dict;
        struct {
            struct xpc_object_s **values;
            size_t count;
            size_t capacity;
        } array;
        struct {
            mach_port_t send_port;
            mach_port_t recv_port;
            dispatch_queue_t target_queue;
            void (^event_handler)(void *);
            dispatch_source_t recv_source;
            char *service_name;
            struct xpc_object_s *pending_msg;
            bool is_listener;
            bool is_active;
            pid_t remote_pid;
        } conn;
    };
};

/* ---- xpc_type_s: used for type identity pointers ---- */
struct xpc_type_s {
    uint32_t tag;
};

/* ---- Type constants ---- */
const struct xpc_type_s _xpc_type_null_s       = { _XPC_TAG_NULL };
const struct xpc_type_s _xpc_type_bool_s       = { _XPC_TAG_BOOL };
const struct xpc_type_s _xpc_type_int64_s      = { _XPC_TAG_INT64 };
const struct xpc_type_s _xpc_type_uint64_s     = { _XPC_TAG_UINT64 };
const struct xpc_type_s _xpc_type_double_s     = { _XPC_TAG_DOUBLE };
const struct xpc_type_s _xpc_type_string_s     = { _XPC_TAG_STRING };
const struct xpc_type_s _xpc_type_data_s       = { _XPC_TAG_DATA };
const struct xpc_type_s _xpc_type_dictionary_s = { _XPC_TAG_DICTIONARY };
const struct xpc_type_s _xpc_type_array_s      = { _XPC_TAG_ARRAY };
const struct xpc_type_s _xpc_type_error_s      = { _XPC_TAG_ERROR };
const struct xpc_type_s _xpc_type_connection_s = { _XPC_TAG_CONNECTION };

/* Legacy data symbol aliases (old stubs exported these as void* pointers) */
const void *_xpc_type_bool       = &_xpc_type_bool_s;
const void *_xpc_type_dictionary = &_xpc_type_dictionary_s;
const void *_xpc_type_uint64     = &_xpc_type_uint64_s;

/* ---- Singletons ---- */

/* Bool singletons — statically allocated, never freed */
static struct xpc_object_s _xpc_bool_true_obj  = { .tag = _XPC_TAG_BOOL, .refcount = 0x7FFFFFFF, .bool_val = true };
static struct xpc_object_s _xpc_bool_false_obj = { .tag = _XPC_TAG_BOOL, .refcount = 0x7FFFFFFF, .bool_val = false };

const void *_xpc_bool_true  = &_xpc_bool_true_obj;
const void *_xpc_bool_false = &_xpc_bool_false_obj;

/* Null singleton */
static struct xpc_object_s _xpc_null_obj = { .tag = _XPC_TAG_NULL, .refcount = 0x7FFFFFFF };

/* Error singletons */
static struct xpc_object_s _xpc_error_interrupted_obj = { .tag = _XPC_TAG_ERROR, .refcount = 0x7FFFFFFF };
static struct xpc_object_s _xpc_error_invalid_obj     = { .tag = _XPC_TAG_ERROR, .refcount = 0x7FFFFFFF };
static struct xpc_object_s _xpc_error_termination_obj = { .tag = _XPC_TAG_ERROR, .refcount = 0x7FFFFFFF };

const void *_xpc_error_connection_interrupted = &_xpc_error_interrupted_obj;
const void *_xpc_error_connection_invalid     = &_xpc_error_invalid_obj;
const void *_xpc_error_termination_imminent   = &_xpc_error_termination_obj;

/* Forward declarations */
void xpc_release(void *object);
void *xpc_retain(void *object);
void *xpc_connection_send_message_with_reply_sync(void *connection,
    void *message);

/* ---- Internal helpers ---- */

static struct xpc_object_s *_xpc_alloc(uint32_t tag)
{
    struct xpc_object_s *obj = calloc(1, sizeof(struct xpc_object_s));
    if (!obj) return NULL;
    obj->tag = tag;
    obj->refcount = 1;
    obj->_reply_port = MACH_PORT_NULL;
    return obj;
}

static bool _xpc_is_singleton(struct xpc_object_s *obj)
{
    return obj == &_xpc_bool_true_obj ||
           obj == &_xpc_bool_false_obj ||
           obj == &_xpc_null_obj ||
           obj == &_xpc_error_interrupted_obj ||
           obj == &_xpc_error_invalid_obj ||
           obj == &_xpc_error_termination_obj;
}

/* ---- Lifecycle ---- */

void *xpc_retain(void *object)
{
    struct xpc_object_s *obj = (struct xpc_object_s *)object;
    if (!obj || _xpc_is_singleton(obj)) return object;
    __sync_add_and_fetch(&obj->refcount, 1);
    return object;
}

static void _xpc_object_destroy(struct xpc_object_s *obj)
{
    switch (obj->tag) {
    case _XPC_TAG_STRING:
        free(obj->string.ptr);
        break;
    case _XPC_TAG_DATA:
        free(obj->data.ptr);
        break;
    case _XPC_TAG_DICTIONARY:
        for (size_t i = 0; i < obj->dict.count; i++) {
            free(obj->dict.keys[i]);
            xpc_release(obj->dict.values[i]);
        }
        free(obj->dict.keys);
        free(obj->dict.values);
        break;
    case _XPC_TAG_ARRAY:
        for (size_t i = 0; i < obj->array.count; i++)
            xpc_release(obj->array.values[i]);
        free(obj->array.values);
        break;
    case _XPC_TAG_CONNECTION:
        if (obj->conn.recv_source) {
            dispatch_source_cancel(obj->conn.recv_source);
            dispatch_release(obj->conn.recv_source);
        }
        if (obj->conn.send_port != MACH_PORT_NULL)
            mach_port_deallocate(mach_task_self(), obj->conn.send_port);
        if (obj->conn.recv_port != MACH_PORT_NULL)
            mach_port_mod_refs(mach_task_self(), obj->conn.recv_port,
                MACH_PORT_RIGHT_RECEIVE, -1);
        if (obj->conn.event_handler)
            Block_release(obj->conn.event_handler);
        if (obj->conn.pending_msg)
            xpc_release(obj->conn.pending_msg);
        free(obj->conn.service_name);
        if (obj->conn.target_queue)
            dispatch_release(obj->conn.target_queue);
        break;
    default:
        break;
    }
    free(obj);
}

void xpc_release(void *object)
{
    struct xpc_object_s *obj = (struct xpc_object_s *)object;
    if (!obj || _xpc_is_singleton(obj)) return;
    if (__sync_sub_and_fetch(&obj->refcount, 1) <= 0)
        _xpc_object_destroy(obj);
}

const void *xpc_get_type(void *object)
{
    struct xpc_object_s *obj = (struct xpc_object_s *)object;
    if (!obj) return NULL;
    switch (obj->tag) {
    case _XPC_TAG_NULL:       return &_xpc_type_null_s;
    case _XPC_TAG_BOOL:       return &_xpc_type_bool_s;
    case _XPC_TAG_INT64:      return &_xpc_type_int64_s;
    case _XPC_TAG_UINT64:     return &_xpc_type_uint64_s;
    case _XPC_TAG_DOUBLE:     return &_xpc_type_double_s;
    case _XPC_TAG_STRING:     return &_xpc_type_string_s;
    case _XPC_TAG_DATA:       return &_xpc_type_data_s;
    case _XPC_TAG_DICTIONARY: return &_xpc_type_dictionary_s;
    case _XPC_TAG_ARRAY:      return &_xpc_type_array_s;
    case _XPC_TAG_ERROR:      return &_xpc_type_error_s;
    case _XPC_TAG_CONNECTION: return &_xpc_type_connection_s;
    default:                  return NULL;
    }
}

/* ---- Null ---- */

void *xpc_null_create(void)
{
    return &_xpc_null_obj;
}

/* ---- Bool ---- */

void *xpc_bool_create(int value)
{
    return value ? &_xpc_bool_true_obj : &_xpc_bool_false_obj;
}

int xpc_bool_get_value(void *object)
{
    struct xpc_object_s *obj = (struct xpc_object_s *)object;
    if (!obj || obj->tag != _XPC_TAG_BOOL) return 0;
    return obj->bool_val ? 1 : 0;
}

/* ---- Int64 ---- */

void *xpc_int64_create(int64_t value)
{
    struct xpc_object_s *obj = _xpc_alloc(_XPC_TAG_INT64);
    if (obj) obj->int64_val = value;
    return obj;
}

int64_t xpc_int64_get_value(void *object)
{
    struct xpc_object_s *obj = (struct xpc_object_s *)object;
    if (!obj || obj->tag != _XPC_TAG_INT64) return 0;
    return obj->int64_val;
}

/* ---- Uint64 ---- */

void *xpc_uint64_create(uint64_t value)
{
    struct xpc_object_s *obj = _xpc_alloc(_XPC_TAG_UINT64);
    if (obj) obj->uint64_val = value;
    return obj;
}

uint64_t xpc_uint64_get_value(void *object)
{
    struct xpc_object_s *obj = (struct xpc_object_s *)object;
    if (!obj || obj->tag != _XPC_TAG_UINT64) return 0;
    return obj->uint64_val;
}

/* ---- Double ---- */

void *xpc_double_create(double value)
{
    struct xpc_object_s *obj = _xpc_alloc(_XPC_TAG_DOUBLE);
    if (obj) obj->double_val = value;
    return obj;
}

double xpc_double_get_value(void *object)
{
    struct xpc_object_s *obj = (struct xpc_object_s *)object;
    if (!obj || obj->tag != _XPC_TAG_DOUBLE) return 0.0;
    return obj->double_val;
}

/* ---- String ---- */

void *xpc_string_create(const char *string)
{
    if (!string) return NULL;
    struct xpc_object_s *obj = _xpc_alloc(_XPC_TAG_STRING);
    if (!obj) return NULL;
    obj->string.len = strlen(string);
    obj->string.ptr = strdup(string);
    if (!obj->string.ptr) { free(obj); return NULL; }
    return obj;
}

size_t xpc_string_get_length(void *object)
{
    struct xpc_object_s *obj = (struct xpc_object_s *)object;
    if (!obj || obj->tag != _XPC_TAG_STRING) return 0;
    return obj->string.len;
}

const char *xpc_string_get_string_ptr(void *object)
{
    struct xpc_object_s *obj = (struct xpc_object_s *)object;
    if (!obj || obj->tag != _XPC_TAG_STRING) return NULL;
    return obj->string.ptr;
}

/* ---- Data ---- */

void *xpc_data_create(const void *bytes, size_t length)
{
    struct xpc_object_s *obj = _xpc_alloc(_XPC_TAG_DATA);
    if (!obj) return NULL;
    obj->data.len = length;
    if (bytes && length > 0) {
        obj->data.ptr = malloc(length);
        if (!obj->data.ptr) { free(obj); return NULL; }
        memcpy(obj->data.ptr, bytes, length);
    }
    return obj;
}

size_t xpc_data_get_length(void *object)
{
    struct xpc_object_s *obj = (struct xpc_object_s *)object;
    if (!obj || obj->tag != _XPC_TAG_DATA) return 0;
    return obj->data.len;
}

const void *xpc_data_get_bytes_ptr(void *object)
{
    struct xpc_object_s *obj = (struct xpc_object_s *)object;
    if (!obj || obj->tag != _XPC_TAG_DATA) return NULL;
    return obj->data.ptr;
}

/* ---- Dictionary ---- */

#define DICT_INITIAL_CAP 8

void *xpc_dictionary_create(const char *const *keys,
    const void *const *values, size_t count)
{
    struct xpc_object_s *obj = _xpc_alloc(_XPC_TAG_DICTIONARY);
    if (!obj) return NULL;

    size_t cap = (count > DICT_INITIAL_CAP) ? count : DICT_INITIAL_CAP;
    obj->dict.keys = calloc(cap, sizeof(char *));
    obj->dict.values = calloc(cap, sizeof(struct xpc_object_s *));
    if (!obj->dict.keys || !obj->dict.values) {
        free(obj->dict.keys);
        free(obj->dict.values);
        free(obj);
        return NULL;
    }
    obj->dict.capacity = cap;
    obj->dict.count = 0;

    for (size_t i = 0; i < count; i++) {
        if (keys && keys[i] && values && values[i]) {
            obj->dict.keys[i] = strdup(keys[i]);
            obj->dict.values[i] = (struct xpc_object_s *)values[i];
            xpc_retain(obj->dict.values[i]);
            obj->dict.count++;
        }
    }
    return obj;
}

void *xpc_dictionary_create_reply(void *original)
{
    struct xpc_object_s *orig = (struct xpc_object_s *)original;
    struct xpc_object_s *reply = xpc_dictionary_create(NULL, NULL, 0);
    if (reply && orig && orig->tag == _XPC_TAG_DICTIONARY)
        reply->_reply_port = orig->_reply_port;
    return reply;
}

static void _dict_grow(struct xpc_object_s *d)
{
    size_t newcap = d->dict.capacity * 2;
    d->dict.keys = realloc(d->dict.keys, newcap * sizeof(char *));
    d->dict.values = realloc(d->dict.values, newcap * sizeof(struct xpc_object_s *));
    d->dict.capacity = newcap;
}

void xpc_dictionary_set_value(void *dict, const char *key, void *value)
{
    struct xpc_object_s *d = (struct xpc_object_s *)dict;
    if (!d || d->tag != _XPC_TAG_DICTIONARY || !key) return;

    /* Update existing key */
    for (size_t i = 0; i < d->dict.count; i++) {
        if (strcmp(d->dict.keys[i], key) == 0) {
            if (value) {
                xpc_retain(value);
                xpc_release(d->dict.values[i]);
                d->dict.values[i] = (struct xpc_object_s *)value;
            } else {
                /* NULL value = remove key */
                xpc_release(d->dict.values[i]);
                free(d->dict.keys[i]);
                d->dict.count--;
                if (i < d->dict.count) {
                    d->dict.keys[i] = d->dict.keys[d->dict.count];
                    d->dict.values[i] = d->dict.values[d->dict.count];
                }
            }
            return;
        }
    }

    if (!value) return;

    /* Insert new key */
    if (d->dict.count >= d->dict.capacity)
        _dict_grow(d);
    d->dict.keys[d->dict.count] = strdup(key);
    d->dict.values[d->dict.count] = (struct xpc_object_s *)value;
    xpc_retain(value);
    d->dict.count++;
}

void *xpc_dictionary_get_value(void *dict, const char *key)
{
    struct xpc_object_s *d = (struct xpc_object_s *)dict;
    if (!d || d->tag != _XPC_TAG_DICTIONARY || !key) return NULL;
    for (size_t i = 0; i < d->dict.count; i++) {
        if (strcmp(d->dict.keys[i], key) == 0)
            return d->dict.values[i];
    }
    return NULL;
}

size_t xpc_dictionary_get_count(void *dict)
{
    struct xpc_object_s *d = (struct xpc_object_s *)dict;
    if (!d || d->tag != _XPC_TAG_DICTIONARY) return 0;
    return d->dict.count;
}

int xpc_dictionary_apply(void *dict, void *applier)
{
    struct xpc_object_s *d = (struct xpc_object_s *)dict;
    if (!d || d->tag != _XPC_TAG_DICTIONARY || !applier) return 0;
    bool (^block)(const char *, void *) = (bool (^)(const char *, void *))applier;
    for (size_t i = 0; i < d->dict.count; i++) {
        if (!block(d->dict.keys[i], d->dict.values[i]))
            return 0;
    }
    return 1;
}

/* Dictionary convenience setters */

void xpc_dictionary_set_bool(void *dict, const char *key, int value)
{
    xpc_dictionary_set_value(dict, key, xpc_bool_create(value));
}

void xpc_dictionary_set_int64(void *dict, const char *key, int64_t value)
{
    void *v = xpc_int64_create(value);
    xpc_dictionary_set_value(dict, key, v);
    xpc_release(v);
}

void xpc_dictionary_set_uint64(void *dict, const char *key, uint64_t value)
{
    void *v = xpc_uint64_create(value);
    xpc_dictionary_set_value(dict, key, v);
    xpc_release(v);
}

void xpc_dictionary_set_double(void *dict, const char *key, double value)
{
    void *v = xpc_double_create(value);
    xpc_dictionary_set_value(dict, key, v);
    xpc_release(v);
}

void xpc_dictionary_set_string(void *dict, const char *key, const char *val)
{
    void *v = xpc_string_create(val);
    xpc_dictionary_set_value(dict, key, v);
    xpc_release(v);
}

void xpc_dictionary_set_data(void *dict, const char *key,
    const void *bytes, size_t length)
{
    void *v = xpc_data_create(bytes, length);
    xpc_dictionary_set_value(dict, key, v);
    xpc_release(v);
}

void xpc_dictionary_set_mach_send(void *dict, const char *key, unsigned int port)
{
    struct xpc_object_s *v = _xpc_alloc(_XPC_TAG_MACH_SEND);
    if (!v) return;
    v->port = port;
    xpc_dictionary_set_value(dict, key, v);
    xpc_release(v);
}

void xpc_dictionary_set_mach_recv(void *dict, const char *key, unsigned int port)
{
    struct xpc_object_s *v = _xpc_alloc(_XPC_TAG_MACH_RECV);
    if (!v) return;
    v->port = port;
    xpc_dictionary_set_value(dict, key, v);
    xpc_release(v);
}

/* Dictionary convenience getters */

int xpc_dictionary_get_bool(void *dict, const char *key)
{
    return xpc_bool_get_value(xpc_dictionary_get_value(dict, key));
}

int64_t xpc_dictionary_get_int64(void *dict, const char *key)
{
    return xpc_int64_get_value(xpc_dictionary_get_value(dict, key));
}

uint64_t xpc_dictionary_get_uint64(void *dict, const char *key)
{
    return xpc_uint64_get_value(xpc_dictionary_get_value(dict, key));
}

double xpc_dictionary_get_double(void *dict, const char *key)
{
    return xpc_double_get_value(xpc_dictionary_get_value(dict, key));
}

const char *xpc_dictionary_get_string(void *dict, const char *key)
{
    return xpc_string_get_string_ptr(xpc_dictionary_get_value(dict, key));
}

const void *xpc_dictionary_get_data(void *dict, const char *key, size_t *length)
{
    struct xpc_object_s *obj = xpc_dictionary_get_value(dict, key);
    if (!obj || obj->tag != _XPC_TAG_DATA) {
        if (length) *length = 0;
        return NULL;
    }
    if (length) *length = obj->data.len;
    return obj->data.ptr;
}

unsigned int xpc_dictionary_copy_mach_send(void *dict, const char *key)
{
    struct xpc_object_s *obj = xpc_dictionary_get_value(dict, key);
    if (!obj || (obj->tag != _XPC_TAG_MACH_SEND && obj->tag != _XPC_TAG_MACH_RECV))
        return 0;
    return obj->port;
}

void xpc_dictionary_get_audit_token(void *dict, void *token)
{
    /* Audit tokens come from the Mach message header of a received message.
     * Without real IPC, zero it out. */
    if (token)
        memset(token, 0, 32); /* sizeof(audit_token_t) = 32 bytes */
}

/* ---- Array ---- */

#define ARRAY_INITIAL_CAP 8

void *xpc_array_create(const void *const *objects, size_t count)
{
    struct xpc_object_s *obj = _xpc_alloc(_XPC_TAG_ARRAY);
    if (!obj) return NULL;

    size_t cap = (count > ARRAY_INITIAL_CAP) ? count : ARRAY_INITIAL_CAP;
    obj->array.values = calloc(cap, sizeof(struct xpc_object_s *));
    if (!obj->array.values) { free(obj); return NULL; }
    obj->array.capacity = cap;
    obj->array.count = 0;

    for (size_t i = 0; i < count; i++) {
        if (objects && objects[i]) {
            obj->array.values[i] = (struct xpc_object_s *)objects[i];
            xpc_retain(obj->array.values[i]);
            obj->array.count++;
        }
    }
    return obj;
}

static void _array_grow(struct xpc_object_s *a)
{
    size_t newcap = a->array.capacity * 2;
    a->array.values = realloc(a->array.values, newcap * sizeof(struct xpc_object_s *));
    a->array.capacity = newcap;
}

void xpc_array_append_value(void *array, void *value)
{
    struct xpc_object_s *a = (struct xpc_object_s *)array;
    if (!a || a->tag != _XPC_TAG_ARRAY || !value) return;
    if (a->array.count >= a->array.capacity)
        _array_grow(a);
    a->array.values[a->array.count] = (struct xpc_object_s *)value;
    xpc_retain(value);
    a->array.count++;
}

size_t xpc_array_get_count(void *array)
{
    struct xpc_object_s *a = (struct xpc_object_s *)array;
    if (!a || a->tag != _XPC_TAG_ARRAY) return 0;
    return a->array.count;
}

void *xpc_array_get_value(void *array, size_t index)
{
    struct xpc_object_s *a = (struct xpc_object_s *)array;
    if (!a || a->tag != _XPC_TAG_ARRAY || index >= a->array.count) return NULL;
    return a->array.values[index];
}

int xpc_array_apply(void *array, void *applier)
{
    struct xpc_object_s *a = (struct xpc_object_s *)array;
    if (!a || a->tag != _XPC_TAG_ARRAY || !applier) return 0;
    bool (^block)(size_t, void *) = (bool (^)(size_t, void *))applier;
    for (size_t i = 0; i < a->array.count; i++) {
        if (!block(i, a->array.values[i]))
            return 0;
    }
    return 1;
}

/* Array convenience setters */

void xpc_array_set_string(void *array, size_t index, const char *val)
{
    struct xpc_object_s *a = (struct xpc_object_s *)array;
    if (!a || a->tag != _XPC_TAG_ARRAY || index >= a->array.count) return;
    void *v = xpc_string_create(val);
    xpc_release(a->array.values[index]);
    a->array.values[index] = (struct xpc_object_s *)v;
}

void xpc_array_set_uint64(void *array, size_t index, uint64_t val)
{
    struct xpc_object_s *a = (struct xpc_object_s *)array;
    if (!a || a->tag != _XPC_TAG_ARRAY || index >= a->array.count) return;
    void *v = xpc_uint64_create(val);
    xpc_release(a->array.values[index]);
    a->array.values[index] = (struct xpc_object_s *)v;
}

void xpc_array_set_int64(void *array, size_t index, int64_t val)
{
    struct xpc_object_s *a = (struct xpc_object_s *)array;
    if (!a || a->tag != _XPC_TAG_ARRAY || index >= a->array.count) return;
    void *v = xpc_int64_create(val);
    xpc_release(a->array.values[index]);
    a->array.values[index] = (struct xpc_object_s *)v;
}

/* Array convenience getters */

const char *xpc_array_get_string(void *array, size_t index)
{
    return xpc_string_get_string_ptr(xpc_array_get_value(array, index));
}

uint64_t xpc_array_get_uint64(void *array, size_t index)
{
    return xpc_uint64_get_value(xpc_array_get_value(array, index));
}

int64_t xpc_array_get_int64(void *array, size_t index)
{
    return xpc_int64_get_value(xpc_array_get_value(array, index));
}

/* ---- Description ---- */

char *xpc_copy_description(void *object)
{
    struct xpc_object_s *obj = (struct xpc_object_s *)object;
    char *buf = NULL;

    if (!obj) {
        buf = strdup("<null xpc object>");
        return buf;
    }

    switch (obj->tag) {
    case _XPC_TAG_NULL:
        asprintf(&buf, "<null>");
        break;
    case _XPC_TAG_BOOL:
        asprintf(&buf, "<bool: %s>", obj->bool_val ? "true" : "false");
        break;
    case _XPC_TAG_INT64:
        asprintf(&buf, "<int64: %lld>", (long long)obj->int64_val);
        break;
    case _XPC_TAG_UINT64:
        asprintf(&buf, "<uint64: %llu>", (unsigned long long)obj->uint64_val);
        break;
    case _XPC_TAG_DOUBLE:
        asprintf(&buf, "<double: %g>", obj->double_val);
        break;
    case _XPC_TAG_STRING:
        asprintf(&buf, "<string: \"%s\">", obj->string.ptr);
        break;
    case _XPC_TAG_DATA:
        asprintf(&buf, "<data: %zu bytes>", obj->data.len);
        break;
    case _XPC_TAG_DICTIONARY:
        asprintf(&buf, "<dictionary: %zu entries>", obj->dict.count);
        break;
    case _XPC_TAG_ARRAY:
        asprintf(&buf, "<array: %zu entries>", obj->array.count);
        break;
    case _XPC_TAG_MACH_SEND:
        asprintf(&buf, "<mach send: port %u>", obj->port);
        break;
    case _XPC_TAG_MACH_RECV:
        asprintf(&buf, "<mach recv: port %u>", obj->port);
        break;
    case _XPC_TAG_ERROR:
        if (obj == &_xpc_error_interrupted_obj)
            buf = strdup("<error: connection interrupted>");
        else if (obj == &_xpc_error_invalid_obj)
            buf = strdup("<error: connection invalid>");
        else if (obj == &_xpc_error_termination_obj)
            buf = strdup("<error: termination imminent>");
        else
            buf = strdup("<error: unknown>");
        break;
    default:
        asprintf(&buf, "<unknown xpc type 0x%x>", obj->tag);
        break;
    }
    return buf;
}

/* ---- Comparison & hash ---- */

int xpc_equal(void *a, void *b)
{
    struct xpc_object_s *oa = (struct xpc_object_s *)a;
    struct xpc_object_s *ob = (struct xpc_object_s *)b;
    if (oa == ob) return 1;
    if (!oa || !ob) return 0;
    if (oa->tag != ob->tag) return 0;

    switch (oa->tag) {
    case _XPC_TAG_NULL:   return 1;
    case _XPC_TAG_BOOL:   return oa->bool_val == ob->bool_val;
    case _XPC_TAG_INT64:  return oa->int64_val == ob->int64_val;
    case _XPC_TAG_UINT64: return oa->uint64_val == ob->uint64_val;
    case _XPC_TAG_DOUBLE: return oa->double_val == ob->double_val;
    case _XPC_TAG_STRING: return strcmp(oa->string.ptr, ob->string.ptr) == 0;
    case _XPC_TAG_DATA:
        return oa->data.len == ob->data.len &&
               memcmp(oa->data.ptr, ob->data.ptr, oa->data.len) == 0;
    default:
        return 0; /* containers compare by identity */
    }
}

size_t xpc_hash(void *object)
{
    struct xpc_object_s *obj = (struct xpc_object_s *)object;
    if (!obj) return 0;
    size_t h = (size_t)obj->tag;
    switch (obj->tag) {
    case _XPC_TAG_INT64:
        h ^= (size_t)obj->int64_val;
        break;
    case _XPC_TAG_UINT64:
        h ^= (size_t)obj->uint64_val;
        break;
    case _XPC_TAG_STRING:
        for (size_t i = 0; i < obj->string.len; i++)
            h = h * 31 + (unsigned char)obj->string.ptr[i];
        break;
    default:
        h ^= (size_t)(uintptr_t)obj;
        break;
    }
    return h;
}

/* ---- Utilities ---- */

void *xpc_copy_entitlements_for_pid(int pid)
{
    (void)pid;
    return NULL; /* no entitlement database on Panthera */
}

const char *xpc_strerror(int error)
{
    switch (error) {
    case 0: return "success";
    case 1: return "connection interrupted";
    case 2: return "connection invalid";
    case 3: return "termination imminent";
    default: return "unknown xpc error";
    }
}

/* ==== Serialization: XPC Dictionary ↔ Flat Buffer ==== */

#define XPC_WIRE_MAGIC   0x58504321  /* "XPC!" */
#define XPC_WIRE_VERSION 1
#define XPC_MSG_ID       0x58504300  /* "XPC\0" */

/* Wire type tags */
#define XPC_WIRE_STRING  1
#define XPC_WIRE_INT64   2
#define XPC_WIRE_UINT64  3
#define XPC_WIRE_DOUBLE  4
#define XPC_WIRE_BOOL    5
#define XPC_WIRE_DATA    6
#define XPC_WIRE_NULL    7

static inline uint32_t _xpc_pad4(uint32_t n)
{
    return (n + 3) & ~3u;
}

static uint32_t _xpc_wire_type(struct xpc_object_s *obj)
{
    switch (obj->tag) {
    case _XPC_TAG_STRING: return XPC_WIRE_STRING;
    case _XPC_TAG_INT64:  return XPC_WIRE_INT64;
    case _XPC_TAG_UINT64: return XPC_WIRE_UINT64;
    case _XPC_TAG_DOUBLE: return XPC_WIRE_DOUBLE;
    case _XPC_TAG_BOOL:   return XPC_WIRE_BOOL;
    case _XPC_TAG_DATA:   return XPC_WIRE_DATA;
    case _XPC_TAG_NULL:   return XPC_WIRE_NULL;
    default:              return 0;
    }
}

/*
 * xpc_serialize_dict — Serialize an XPC dictionary to a flat buffer.
 * Returns malloc'd buffer, sets *out_len. Caller frees.
 */
static void *xpc_serialize_dict(void *dict, size_t *out_len)
{
    struct xpc_object_s *d = (struct xpc_object_s *)dict;
    if (!d || d->tag != _XPC_TAG_DICTIONARY || !out_len) return NULL;

    /* First pass: compute total size */
    size_t total = 16; /* header: magic + version + count + payload_len */
    for (size_t i = 0; i < d->dict.count; i++) {
        struct xpc_object_s *val = d->dict.values[i];
        uint32_t wt = _xpc_wire_type(val);
        if (wt == 0) continue; /* skip unsupported types */

        uint32_t key_len = (uint32_t)strlen(d->dict.keys[i]) + 1;
        uint32_t val_len = 0;
        switch (val->tag) {
        case _XPC_TAG_STRING: val_len = (uint32_t)val->string.len + 1; break;
        case _XPC_TAG_INT64:
        case _XPC_TAG_UINT64:
        case _XPC_TAG_DOUBLE: val_len = 8; break;
        case _XPC_TAG_BOOL:   val_len = 1; break;
        case _XPC_TAG_DATA:   val_len = (uint32_t)val->data.len; break;
        case _XPC_TAG_NULL:   val_len = 0; break;
        default: break;
        }
        total += 4 + 4 + _xpc_pad4(key_len) + 4 + _xpc_pad4(val_len);
    }

    uint8_t *buf = calloc(1, total);
    if (!buf) return NULL;
    uint8_t *p = buf;

    /* Header */
    uint32_t entry_count = 0;
    /* Count serializable entries */
    for (size_t i = 0; i < d->dict.count; i++) {
        if (_xpc_wire_type(d->dict.values[i]) != 0) entry_count++;
    }

    *(uint32_t *)p = XPC_WIRE_MAGIC;   p += 4;
    *(uint32_t *)p = XPC_WIRE_VERSION;  p += 4;
    *(uint32_t *)p = entry_count;       p += 4;
    *(uint32_t *)p = (uint32_t)(total - 16); p += 4;

    /* Entries */
    for (size_t i = 0; i < d->dict.count; i++) {
        struct xpc_object_s *val = d->dict.values[i];
        uint32_t wt = _xpc_wire_type(val);
        if (wt == 0) continue;

        uint32_t key_len = (uint32_t)strlen(d->dict.keys[i]) + 1;
        uint32_t val_len = 0;
        switch (val->tag) {
        case _XPC_TAG_STRING: val_len = (uint32_t)val->string.len + 1; break;
        case _XPC_TAG_INT64:
        case _XPC_TAG_UINT64:
        case _XPC_TAG_DOUBLE: val_len = 8; break;
        case _XPC_TAG_BOOL:   val_len = 1; break;
        case _XPC_TAG_DATA:   val_len = (uint32_t)val->data.len; break;
        default: break;
        }

        *(uint32_t *)p = wt;                   p += 4;
        *(uint32_t *)p = _xpc_pad4(key_len);   p += 4;
        memcpy(p, d->dict.keys[i], key_len);    p += _xpc_pad4(key_len);
        *(uint32_t *)p = val_len;               p += 4;

        switch (val->tag) {
        case _XPC_TAG_STRING:
            memcpy(p, val->string.ptr, val_len);
            break;
        case _XPC_TAG_INT64:
            memcpy(p, &val->int64_val, 8);
            break;
        case _XPC_TAG_UINT64:
            memcpy(p, &val->uint64_val, 8);
            break;
        case _XPC_TAG_DOUBLE:
            memcpy(p, &val->double_val, 8);
            break;
        case _XPC_TAG_BOOL:
            *p = val->bool_val ? 1 : 0;
            break;
        case _XPC_TAG_DATA:
            if (val->data.len > 0)
                memcpy(p, val->data.ptr, val->data.len);
            break;
        default:
            break;
        }
        p += _xpc_pad4(val_len);
    }

    *out_len = total;
    return buf;
}

/*
 * xpc_deserialize_dict — Deserialize a flat buffer back to an XPC dictionary.
 */
static void *xpc_deserialize_dict(const void *buf, size_t len)
{
    if (!buf || len < 16) return NULL;
    const uint8_t *p = (const uint8_t *)buf;

    uint32_t magic   = *(const uint32_t *)p; p += 4;
    uint32_t version = *(const uint32_t *)p; p += 4;
    uint32_t count   = *(const uint32_t *)p; p += 4;
    /* uint32_t payload_len = */ p += 4;

    if (magic != XPC_WIRE_MAGIC || version != XPC_WIRE_VERSION)
        return NULL;

    struct xpc_object_s *dict = xpc_dictionary_create(NULL, NULL, 0);
    if (!dict) return NULL;

    const uint8_t *end = (const uint8_t *)buf + len;
    for (uint32_t i = 0; i < count && p + 12 <= end; i++) {
        uint32_t type     = *(const uint32_t *)p; p += 4;
        uint32_t key_plen = *(const uint32_t *)p; p += 4;
        if (p + key_plen > end) break;
        const char *key = (const char *)p;
        p += key_plen;
        if (p + 4 > end) break;
        uint32_t val_len  = *(const uint32_t *)p; p += 4;
        if (p + _xpc_pad4(val_len) > end) break;

        switch (type) {
        case XPC_WIRE_STRING:
            xpc_dictionary_set_string(dict, key, (const char *)p);
            break;
        case XPC_WIRE_INT64: {
            int64_t v; memcpy(&v, p, 8);
            xpc_dictionary_set_int64(dict, key, v);
            break;
        }
        case XPC_WIRE_UINT64: {
            uint64_t v; memcpy(&v, p, 8);
            xpc_dictionary_set_uint64(dict, key, v);
            break;
        }
        case XPC_WIRE_DOUBLE: {
            double v; memcpy(&v, p, 8);
            xpc_dictionary_set_double(dict, key, v);
            break;
        }
        case XPC_WIRE_BOOL:
            xpc_dictionary_set_bool(dict, key, p[0] ? 1 : 0);
            break;
        case XPC_WIRE_DATA:
            xpc_dictionary_set_data(dict, key, p, val_len);
            break;
        case XPC_WIRE_NULL:
            xpc_dictionary_set_value(dict, key, &_xpc_null_obj);
            break;
        default:
            break;
        }
        p += _xpc_pad4(val_len);
    }
    return dict;
}

/* ==== Bootstrap MIG Helpers ==== */

/* MIG message IDs for job subsystem (base 400) */
#define MIG_CHECK_IN2_ID  402
#define MIG_LOOK_UP2_ID   404

#pragma pack(push, 4)
union _xpc_check_in2_msg {
    struct {
        mach_msg_header_t hdr;
        NDR_record_t NDR;
        char servicename[128];
        uint64_t flags;
    } req;
    struct {
        mach_msg_header_t hdr;
        mach_msg_body_t body;
        mach_msg_port_descriptor_t serviceport;
        NDR_record_t NDR;
        unsigned char instanceid[16];
        char trailer[64];
    } rep;
};
#pragma pack(pop)

#pragma pack(push, 4)
union _xpc_look_up2_msg {
    struct {
        mach_msg_header_t hdr;
        NDR_record_t NDR;
        char servicename[128];
        int32_t targetpid;
        unsigned char instanceid[16];
        uint64_t flags;
    } req;
    struct {
        mach_msg_header_t hdr;
        mach_msg_body_t body;
        mach_msg_port_descriptor_t serviceport;
        NDR_record_t NDR;
        char _pad[64];
        char trailer[64];
    } rep;
};
#pragma pack(pop)

static mach_port_t _xpc_get_bootstrap_port(void)
{
    extern mach_port_t bootstrap_port;
    mach_port_t bp = bootstrap_port;
    if (bp == MACH_PORT_NULL) {
        task_get_special_port(mach_task_self(), TASK_BOOTSTRAP_PORT, &bp);
    }
    return bp;
}

static kern_return_t
_xpc_bootstrap_check_in(mach_port_t bp, const char *name, mach_port_t *port)
{
    union _xpc_check_in2_msg msg;
    mach_port_t reply_port;
    kern_return_t kr;

    reply_port = mig_get_reply_port();
    memset(&msg, 0, sizeof(msg));
    msg.req.hdr.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,
        MACH_MSG_TYPE_MAKE_SEND_ONCE);
    msg.req.hdr.msgh_size = sizeof(msg.req);
    msg.req.hdr.msgh_remote_port = bp;
    msg.req.hdr.msgh_local_port = reply_port;
    msg.req.hdr.msgh_id = MIG_CHECK_IN2_ID;
    msg.req.NDR = NDR_record;
    strlcpy(msg.req.servicename, name, sizeof(msg.req.servicename));
    msg.req.flags = 0;

    kr = mach_msg(&msg.req.hdr, MACH_SEND_MSG | MACH_RCV_MSG,
        sizeof(msg.req), sizeof(msg.rep), reply_port, 5000, MACH_PORT_NULL);
    if (kr != MACH_MSG_SUCCESS)
        return kr;

    if (!(msg.rep.hdr.msgh_bits & MACH_MSGH_BITS_COMPLEX)) {
        typedef struct { mach_msg_header_t h; NDR_record_t ndr; kern_return_t ret; } err_reply;
        return ((err_reply *)&msg)->ret;
    }

    *port = msg.rep.serviceport.name;
    return KERN_SUCCESS;
}

static kern_return_t
_xpc_bootstrap_look_up(mach_port_t bp, const char *name, mach_port_t *port)
{
    union _xpc_look_up2_msg msg;
    mach_port_t reply_port;
    kern_return_t kr;

    reply_port = mig_get_reply_port();
    memset(&msg, 0, sizeof(msg));
    msg.req.hdr.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,
        MACH_MSG_TYPE_MAKE_SEND_ONCE);
    msg.req.hdr.msgh_size = sizeof(msg.req);
    msg.req.hdr.msgh_remote_port = bp;
    msg.req.hdr.msgh_local_port = reply_port;
    msg.req.hdr.msgh_id = MIG_LOOK_UP2_ID;
    msg.req.NDR = NDR_record;
    strlcpy(msg.req.servicename, name, sizeof(msg.req.servicename));
    msg.req.targetpid = 0;
    msg.req.flags = 0;

    kr = mach_msg(&msg.req.hdr, MACH_SEND_MSG | MACH_RCV_MSG,
        sizeof(msg.req), sizeof(msg.rep), reply_port, 5000, MACH_PORT_NULL);
    if (kr != MACH_MSG_SUCCESS)
        return kr;

    if (!(msg.rep.hdr.msgh_bits & MACH_MSGH_BITS_COMPLEX)) {
        typedef struct { mach_msg_header_t h; NDR_record_t ndr; kern_return_t ret; } err_reply;
        return ((err_reply *)&msg)->ret;
    }

    *port = msg.rep.serviceport.name;
    return KERN_SUCCESS;
}

/* ==== Mach Message Send/Receive Helpers ==== */

/* Max inline payload for XPC Mach messages */
#define XPC_MACH_MSG_MAX 8192

static kern_return_t
_xpc_mach_send(mach_port_t remote, mach_port_t local,
    mach_msg_type_name_t local_disp, const void *payload, size_t payload_len)
{
    size_t msg_size = sizeof(mach_msg_header_t) + payload_len;
    mach_msg_header_t *mmsg = calloc(1, msg_size);
    if (!mmsg) return KERN_RESOURCE_SHORTAGE;

    mach_msg_bits_t bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
    if (local != MACH_PORT_NULL)
        bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, local_disp);

    mmsg->msgh_bits = bits;
    mmsg->msgh_size = (mach_msg_size_t)msg_size;
    mmsg->msgh_remote_port = remote;
    mmsg->msgh_local_port = local;
    mmsg->msgh_id = XPC_MSG_ID;

    if (payload && payload_len > 0)
        memcpy((uint8_t *)mmsg + sizeof(mach_msg_header_t), payload, payload_len);

    kern_return_t kr = mach_msg(mmsg, MACH_SEND_MSG, (mach_msg_size_t)msg_size,
        0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);

    free(mmsg);
    return kr;
}

static void *
_xpc_mach_receive(mach_port_t port, mach_port_t *out_reply_port,
    int timeout_ms)
{
    struct {
        mach_msg_header_t header;
        char body[XPC_MACH_MSG_MAX];
    } msg;

    memset(&msg, 0, sizeof(msg));
    mach_msg_option_t opts = MACH_RCV_MSG;
    if (timeout_ms >= 0)
        opts |= MACH_RCV_TIMEOUT;

    mach_msg_return_t kr = mach_msg(&msg.header, opts,
        0, sizeof(msg), port,
        (timeout_ms >= 0) ? (mach_msg_timeout_t)timeout_ms : MACH_MSG_TIMEOUT_NONE,
        MACH_PORT_NULL);

    if (kr != MACH_MSG_SUCCESS)
        return NULL;

    if (out_reply_port)
        *out_reply_port = msg.header.msgh_remote_port;

    size_t payload_len = msg.header.msgh_size - sizeof(mach_msg_header_t);
    if (payload_len == 0) return NULL;

    void *dict = xpc_deserialize_dict(msg.body, payload_len);
    if (dict && out_reply_port) {
        ((struct xpc_object_s *)dict)->_reply_port = msg.header.msgh_remote_port;
    }
    return dict;
}

/* ==== Connection Implementation ==== */

#define XPC_CONNECTION_MACH_SERVICE_LISTENER_FLAG (1ULL << 0)

static struct xpc_object_s *_xpc_connection_alloc(void)
{
    struct xpc_object_s *c = _xpc_alloc(_XPC_TAG_CONNECTION);
    if (!c) return NULL;
    c->conn.send_port = MACH_PORT_NULL;
    c->conn.recv_port = MACH_PORT_NULL;
    c->conn.target_queue = NULL;
    c->conn.event_handler = NULL;
    c->conn.recv_source = NULL;
    c->conn.service_name = NULL;
    c->conn.pending_msg = NULL;
    c->conn.is_listener = false;
    c->conn.is_active = false;
    c->conn.remote_pid = 0;
    return c;
}

void *xpc_connection_create(const char *name, void *targetq)
{
    struct xpc_object_s *c = _xpc_connection_alloc();
    if (!c) return NULL;
    if (name)
        c->conn.service_name = strdup(name);
    if (targetq) {
        c->conn.target_queue = (dispatch_queue_t)targetq;
        dispatch_retain(c->conn.target_queue);
    }
    return c;
}

void *xpc_connection_create_mach_service(const char *name,
    void *targetq, uint64_t flags)
{
    if (!name) return NULL;

    mach_port_t bp = _xpc_get_bootstrap_port();
    if (bp == MACH_PORT_NULL)
        return NULL;

    struct xpc_object_s *c = _xpc_connection_alloc();
    if (!c) return NULL;

    c->conn.service_name = strdup(name);
    if (targetq) {
        c->conn.target_queue = (dispatch_queue_t)targetq;
        dispatch_retain(c->conn.target_queue);
    } else {
        c->conn.target_queue = dispatch_queue_create(
            "com.panthera.xpc.conn", NULL);
    }

    if (flags & XPC_CONNECTION_MACH_SERVICE_LISTENER_FLAG) {
        /* Listener mode: check in with launchd to get the receive right */
        c->conn.is_listener = true;
        mach_port_t svc_port = MACH_PORT_NULL;
        kern_return_t kr = _xpc_bootstrap_check_in(bp, name, &svc_port);
        if (kr != KERN_SUCCESS) {
            xpc_release(c);
            return NULL;
        }
        c->conn.recv_port = svc_port;
    } else {
        /* Client mode: look up the service to get a send right */
        mach_port_t send_port = MACH_PORT_NULL;
        kern_return_t kr = _xpc_bootstrap_look_up(bp, name, &send_port);
        if (kr != KERN_SUCCESS) {
            xpc_release(c);
            return NULL;
        }
        c->conn.send_port = send_port;

        /* Allocate a local receive port for replies */
        mach_port_t recv_port = MACH_PORT_NULL;
        mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE,
            &recv_port);
        c->conn.recv_port = recv_port;
    }

    return c;
}

void xpc_connection_set_event_handler(void *connection, void *handler)
{
    struct xpc_object_s *c = (struct xpc_object_s *)connection;
    if (!c || c->tag != _XPC_TAG_CONNECTION) return;
    if (c->conn.event_handler)
        Block_release(c->conn.event_handler);
    c->conn.event_handler = handler ? Block_copy(handler) : NULL;
}

void xpc_connection_resume(void *connection)
{
    struct xpc_object_s *c = (struct xpc_object_s *)connection;
    if (!c || c->tag != _XPC_TAG_CONNECTION) return;
    c->conn.is_active = true;

    /* If there is a pending message (peer conn), deliver it now */
    if (c->conn.pending_msg && c->conn.event_handler) {
        void (^handler)(void *) = c->conn.event_handler;
        struct xpc_object_s *msg = c->conn.pending_msg;
        c->conn.pending_msg = NULL;
        handler(msg);
        xpc_release(msg);
        return;
    }

    if (c->conn.is_listener && c->conn.recv_port != MACH_PORT_NULL) {
        /* Listener receive loop runs in _xpc_listener_run_loop below,
         * called from xpc_main. Resume just marks it active. */
    }
}

void xpc_connection_cancel(void *connection)
{
    struct xpc_object_s *c = (struct xpc_object_s *)connection;
    if (!c || c->tag != _XPC_TAG_CONNECTION) return;
    c->conn.is_active = false;
    if (c->conn.recv_source) {
        dispatch_source_cancel(c->conn.recv_source);
        dispatch_release(c->conn.recv_source);
        c->conn.recv_source = NULL;
    }
}

void xpc_connection_send_message(void *connection, void *message)
{
    struct xpc_object_s *c = (struct xpc_object_s *)connection;
    struct xpc_object_s *m = (struct xpc_object_s *)message;
    if (!c || c->tag != _XPC_TAG_CONNECTION || !c->conn.is_active) return;
    if (!m || m->tag != _XPC_TAG_DICTIONARY) return;

    /* Determine destination port:
     * - If the message has a _reply_port (it's a reply), send there
     *   using MOVE_SEND_ONCE (reply ports are send-once rights)
     * - Otherwise use the connection's send_port with COPY_SEND */
    mach_port_t dest = c->conn.send_port;
    bool is_reply = (m->_reply_port != MACH_PORT_NULL);
    if (is_reply)
        dest = m->_reply_port;
    if (dest == MACH_PORT_NULL) return;

    size_t payload_len = 0;
    void *payload = xpc_serialize_dict(message, &payload_len);
    if (!payload) return;

    /* Build Mach message with correct disposition */
    size_t msg_size = sizeof(mach_msg_header_t) + payload_len;
    mach_msg_header_t *mmsg = calloc(1, msg_size);
    if (!mmsg) { free(payload); return; }

    mmsg->msgh_bits = MACH_MSGH_BITS(
        is_reply ? MACH_MSG_TYPE_MOVE_SEND_ONCE : MACH_MSG_TYPE_COPY_SEND, 0);
    mmsg->msgh_size = (mach_msg_size_t)msg_size;
    mmsg->msgh_remote_port = dest;
    mmsg->msgh_local_port = MACH_PORT_NULL;
    mmsg->msgh_id = XPC_MSG_ID;

    memcpy((uint8_t *)mmsg + sizeof(mach_msg_header_t), payload, payload_len);

    mach_msg(mmsg, MACH_SEND_MSG, (mach_msg_size_t)msg_size,
        0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);

    free(mmsg);
    free(payload);

    /* Clear reply port — send-once right consumed */
    if (is_reply)
        m->_reply_port = MACH_PORT_NULL;
}

void xpc_connection_send_message_with_reply(void *connection,
    void *message, void *replyq, void *handler)
{
    struct xpc_object_s *c = (struct xpc_object_s *)connection;
    if (!c || c->tag != _XPC_TAG_CONNECTION || !handler) return;

    /* Capture the handler block and do the sync call on the queue */
    void (^reply_handler)(void *) = Block_copy(handler);
    dispatch_queue_t q = replyq ? (dispatch_queue_t)replyq : c->conn.target_queue;
    xpc_retain(connection);
    xpc_retain(message);

    dispatch_async(q, ^{
        void *reply = xpc_connection_send_message_with_reply_sync(
            connection, message);
        reply_handler(reply);
        if (reply) xpc_release(reply);
        xpc_release(message);
        xpc_release(connection);
        Block_release(reply_handler);
    });
}

void *xpc_connection_send_message_with_reply_sync(void *connection,
    void *message)
{
    struct xpc_object_s *c = (struct xpc_object_s *)connection;
    if (!c || c->tag != _XPC_TAG_CONNECTION || !c->conn.is_active)
        return (void *)&_xpc_error_invalid_obj;
    if (c->conn.send_port == MACH_PORT_NULL)
        return (void *)&_xpc_error_invalid_obj;

    /* Allocate a one-shot reply port */
    mach_port_t reply_port = MACH_PORT_NULL;
    mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &reply_port);

    /* Serialize and send with reply port */
    size_t payload_len = 0;
    void *payload = xpc_serialize_dict(message, &payload_len);
    if (!payload) {
        mach_port_mod_refs(mach_task_self(), reply_port,
            MACH_PORT_RIGHT_RECEIVE, -1);
        return (void *)&_xpc_error_interrupted_obj;
    }

    kern_return_t kr = _xpc_mach_send(c->conn.send_port, reply_port,
        MACH_MSG_TYPE_MAKE_SEND_ONCE, payload, payload_len);
    free(payload);

    if (kr != KERN_SUCCESS) {
        mach_port_mod_refs(mach_task_self(), reply_port,
            MACH_PORT_RIGHT_RECEIVE, -1);
        return (void *)&_xpc_error_interrupted_obj;
    }

    /* Blocking receive on reply port (5 second timeout) */
    mach_port_t unused_reply = MACH_PORT_NULL;
    void *reply = _xpc_mach_receive(reply_port, &unused_reply, 5000);

    mach_port_mod_refs(mach_task_self(), reply_port,
        MACH_PORT_RIGHT_RECEIVE, -1);

    if (!reply)
        return (void *)&_xpc_error_interrupted_obj;

    return reply;
}

int xpc_connection_get_pid(void *connection)
{
    struct xpc_object_s *c = (struct xpc_object_s *)connection;
    if (!c || c->tag != _XPC_TAG_CONNECTION) return 0;
    return (int)c->conn.remote_pid;
}

void xpc_connection_set_target_queue(void *connection, void *queue)
{
    struct xpc_object_s *c = (struct xpc_object_s *)connection;
    if (!c || c->tag != _XPC_TAG_CONNECTION) return;
    if (c->conn.target_queue)
        dispatch_release(c->conn.target_queue);
    c->conn.target_queue = (dispatch_queue_t)queue;
    if (queue)
        dispatch_retain(c->conn.target_queue);
}

unsigned int xpc_connection_get_euid(void *connection)
{
    (void)connection;
    return 0;
}

unsigned int xpc_connection_get_egid(void *connection)
{
    (void)connection;
    return 0;
}

/* ==== Listener receive loop ==== */

static _Noreturn void
_xpc_listener_run_loop(struct xpc_object_s *c)
{
    /* Blocking receive loop on the main thread.
     * No dispatch dependency — just mach_msg + handler calls. */
    while (1) {
        mach_port_t reply_port = MACH_PORT_NULL;
        void *xpc_msg = _xpc_mach_receive(c->conn.recv_port,
            &reply_port, -1 /* block forever */);
        if (!xpc_msg) continue;

        if (c->conn.event_handler) {
            struct xpc_object_s *peer = _xpc_connection_alloc();
            if (peer) {
                peer->conn.send_port = reply_port;
                peer->conn.pending_msg = (struct xpc_object_s *)xpc_msg;
                xpc_retain(xpc_msg);

                c->conn.event_handler(peer);
                xpc_release(peer);
            }
        }
        xpc_release(xpc_msg);
    }
}

/* ==== xpc_main ==== */

/* Global handler for xpc_main — avoids block capture issues */
static void (*_xpc_main_handler)(void *) = NULL;

_Noreturn void xpc_main(void *handler)
{
    _xpc_main_handler = (void (*)(void *))handler;
    const char *service_name = getenv("XPC_SERVICE_NAME");
    if (!service_name) service_name = "com.panthera.unknown";

    struct xpc_object_s *listener = xpc_connection_create_mach_service(
        service_name, NULL, XPC_CONNECTION_MACH_SERVICE_LISTENER_FLAG);

    if (!listener)
        _exit(1);

    xpc_connection_set_event_handler(listener, ^(void *peer_event) {
        _xpc_main_handler(peer_event);
    });

    xpc_connection_resume(listener);

    /* Run the listener loop directly on the main thread. */
    _xpc_listener_run_loop(listener);
    __builtin_unreachable();
}

void xpc_transaction_begin(void)
{
}

void xpc_transaction_end(void)
{
}
