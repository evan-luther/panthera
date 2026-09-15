/*
 * test_xpc.c — Verify XPC object layer works on Panthera
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Use raw function declarations to match the actual libxpc ABI
 * (avoids needing the full xpc/xpc.h on the build host) */
extern void *xpc_dictionary_create(const char *const *keys,
    const void *const *values, size_t count);
extern void *xpc_dictionary_create_reply(void *original);
extern void xpc_dictionary_set_string(void *dict, const char *key, const char *val);
extern void xpc_dictionary_set_int64(void *dict, const char *key, int64_t val);
extern void xpc_dictionary_set_uint64(void *dict, const char *key, uint64_t val);
extern const char *xpc_dictionary_get_string(void *dict, const char *key);
extern int64_t xpc_dictionary_get_int64(void *dict, const char *key);
extern uint64_t xpc_dictionary_get_uint64(void *dict, const char *key);
extern void *xpc_dictionary_get_value(void *dict, const char *key);
extern size_t xpc_dictionary_get_count(void *dict);
extern void xpc_dictionary_set_value(void *dict, const char *key, void *value);

extern void *xpc_array_create(const void *const *objects, size_t count);
extern void xpc_array_append_value(void *array, void *value);
extern size_t xpc_array_get_count(void *array);
extern void *xpc_array_get_value(void *array, size_t index);

extern void *xpc_string_create(const char *string);
extern const char *xpc_string_get_string_ptr(void *xstring);
extern size_t xpc_string_get_length(void *xstring);

extern void *xpc_int64_create(int64_t value);
extern int64_t xpc_int64_get_value(void *xint);

extern void *xpc_uint64_create(uint64_t value);
extern uint64_t xpc_uint64_get_value(void *xuint);

extern void *xpc_bool_create(int value);
extern int xpc_bool_get_value(void *xbool);

extern void *xpc_data_create(const void *bytes, size_t length);
extern size_t xpc_data_get_length(void *xdata);
extern const void *xpc_data_get_bytes_ptr(void *xdata);

extern void *xpc_retain(void *obj);
extern void xpc_release(void *obj);
extern const void *xpc_get_type(void *obj);
extern char *xpc_copy_description(void *obj);
extern int xpc_equal(void *a, void *b);
extern const char *xpc_strerror(int error);

extern const void *_xpc_bool_true;
extern const void *_xpc_type_bool;
extern const void *_xpc_type_dictionary;
extern const void *_xpc_type_uint64;

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("  ok: %s\n", msg); } \
} while(0)

int main(void)
{
    printf("=== XPC Object Layer Test ===\n\n");

    /* 1. Type constants exist */
    printf("[Type constants]\n");
    CHECK(_xpc_bool_true != NULL, "_xpc_bool_true is non-NULL");
    CHECK(_xpc_type_bool != NULL, "_xpc_type_bool is non-NULL");
    CHECK(_xpc_type_dictionary != NULL, "_xpc_type_dictionary is non-NULL");
    CHECK(_xpc_type_uint64 != NULL, "_xpc_type_uint64 is non-NULL");

    /* 2. Bool */
    printf("\n[Bool]\n");
    void *bt = xpc_bool_create(1);
    void *bf = xpc_bool_create(0);
    CHECK(bt != NULL, "bool true created");
    CHECK(bf != NULL, "bool false created");
    CHECK(xpc_bool_get_value(bt) == 1, "bool true value is 1");
    CHECK(xpc_bool_get_value(bf) == 0, "bool false value is 0");
    CHECK(bt == (void *)_xpc_bool_true, "bool true is singleton");

    /* 3. Int64 */
    printf("\n[Int64]\n");
    void *i = xpc_int64_create(-42);
    CHECK(i != NULL, "int64 created");
    CHECK(xpc_int64_get_value(i) == -42, "int64 value is -42");
    xpc_release(i);

    /* 4. Uint64 */
    printf("\n[Uint64]\n");
    void *u = xpc_uint64_create(123456789ULL);
    CHECK(u != NULL, "uint64 created");
    CHECK(xpc_uint64_get_value(u) == 123456789ULL, "uint64 value correct");
    xpc_release(u);

    /* 5. String */
    printf("\n[String]\n");
    void *s = xpc_string_create("Panthera");
    CHECK(s != NULL, "string created");
    CHECK(xpc_string_get_length(s) == 8, "string length is 8");
    CHECK(strcmp(xpc_string_get_string_ptr(s), "Panthera") == 0, "string value is 'Panthera'");
    xpc_release(s);

    /* 6. Data */
    printf("\n[Data]\n");
    const uint8_t blob[] = {0xDE, 0xAD, 0xBE, 0xEF};
    void *d = xpc_data_create(blob, 4);
    CHECK(d != NULL, "data created");
    CHECK(xpc_data_get_length(d) == 4, "data length is 4");
    CHECK(memcmp(xpc_data_get_bytes_ptr(d), blob, 4) == 0, "data bytes match");
    xpc_release(d);

    /* 7. Dictionary */
    printf("\n[Dictionary]\n");
    void *dict = xpc_dictionary_create(NULL, NULL, 0);
    CHECK(dict != NULL, "empty dict created");
    CHECK(xpc_dictionary_get_count(dict) == 0, "empty dict count is 0");

    xpc_dictionary_set_string(dict, "name", "Panthera");
    xpc_dictionary_set_int64(dict, "version", 1);
    xpc_dictionary_set_uint64(dict, "year", 2026);

    CHECK(xpc_dictionary_get_count(dict) == 3, "dict count is 3 after 3 sets");
    CHECK(strcmp(xpc_dictionary_get_string(dict, "name"), "Panthera") == 0,
        "dict get_string 'name' = 'Panthera'");
    CHECK(xpc_dictionary_get_int64(dict, "version") == 1,
        "dict get_int64 'version' = 1");
    CHECK(xpc_dictionary_get_uint64(dict, "year") == 2026,
        "dict get_uint64 'year' = 2026");

    /* Overwrite existing key */
    xpc_dictionary_set_int64(dict, "version", 2);
    CHECK(xpc_dictionary_get_int64(dict, "version") == 2,
        "dict overwrite 'version' = 2");
    CHECK(xpc_dictionary_get_count(dict) == 3, "dict count still 3 after overwrite");

    /* Missing key returns 0/NULL */
    CHECK(xpc_dictionary_get_string(dict, "missing") == NULL,
        "dict get missing key returns NULL");
    CHECK(xpc_dictionary_get_int64(dict, "missing") == 0,
        "dict get missing int64 returns 0");

    /* 8. Array */
    printf("\n[Array]\n");
    void *arr = xpc_array_create(NULL, 0);
    CHECK(arr != NULL, "empty array created");
    CHECK(xpc_array_get_count(arr) == 0, "empty array count is 0");

    xpc_array_append_value(arr, xpc_string_create("hello"));
    xpc_array_append_value(arr, xpc_int64_create(42));
    xpc_array_append_value(arr, xpc_uint64_create(99));
    CHECK(xpc_array_get_count(arr) == 3, "array count is 3");

    void *elem0 = xpc_array_get_value(arr, 0);
    CHECK(strcmp(xpc_string_get_string_ptr(elem0), "hello") == 0,
        "array[0] is 'hello'");
    CHECK(xpc_int64_get_value(xpc_array_get_value(arr, 1)) == 42,
        "array[1] is 42");

    /* 9. Type introspection */
    printf("\n[Type introspection]\n");
    CHECK(xpc_get_type(dict) == _xpc_type_dictionary,
        "dict type is XPC_TYPE_DICTIONARY");
    void *u2 = xpc_uint64_create(7);
    CHECK(xpc_get_type(u2) == _xpc_type_uint64,
        "uint64 type is XPC_TYPE_UINT64");
    CHECK(xpc_get_type((void *)_xpc_bool_true) == _xpc_type_bool,
        "bool true type is XPC_TYPE_BOOL");
    xpc_release(u2);

    /* 10. Description */
    printf("\n[Description]\n");
    char *desc = xpc_copy_description(dict);
    CHECK(desc != NULL, "dict description is non-NULL");
    printf("  dict description: %s\n", desc);
    free(desc);

    desc = xpc_copy_description(xpc_string_create("test"));
    CHECK(desc != NULL && strstr(desc, "test") != NULL,
        "string description contains value");
    free(desc);

    /* 11. Equality */
    printf("\n[Equality]\n");
    void *s1 = xpc_string_create("same");
    void *s2 = xpc_string_create("same");
    void *s3 = xpc_string_create("diff");
    CHECK(xpc_equal(s1, s2), "equal strings are equal");
    CHECK(!xpc_equal(s1, s3), "different strings are not equal");
    xpc_release(s1);
    xpc_release(s2);
    xpc_release(s3);

    /* 12. Retain/release */
    printf("\n[Retain/release]\n");
    void *obj = xpc_string_create("retained");
    xpc_retain(obj);
    xpc_release(obj); /* refcount 2 -> 1, should not free */
    CHECK(strcmp(xpc_string_get_string_ptr(obj), "retained") == 0,
        "object survives retain+release");
    xpc_release(obj); /* refcount 1 -> 0, frees */

    /* 13. Strerror */
    printf("\n[Strerror]\n");
    CHECK(xpc_strerror(0) != NULL, "strerror(0) returns string");
    printf("  strerror(0): %s\n", xpc_strerror(0));

    /* 14. Dictionary reply */
    printf("\n[Dictionary reply]\n");
    void *reply = xpc_dictionary_create_reply(dict);
    CHECK(reply != NULL, "create_reply returns dict");
    xpc_release(reply);

    /* Cleanup */
    xpc_release(arr);
    xpc_release(dict);

    printf("\n=== Results: %d failures ===\n",  failures);
    if (failures == 0)
        printf("XPC objects: PASS\n");
    else
        printf("XPC objects: FAIL\n");

    return failures;
}
