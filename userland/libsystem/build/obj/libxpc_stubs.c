#include <stdint.h>
#include <stddef.h>

const void *_xpc_bool_true = (const void *)1;
const void *_xpc_type_bool = (const void *)0x1001;
const void *_xpc_type_dictionary = (const void *)0x1002;
const void *_xpc_type_uint64 = (const void *)0x1003;

void *xpc_dictionary_create(const char *const *keys, const void *const *values, size_t count)
{
	(void)keys;
	(void)values;
	(void)count;
	return NULL;
}

void *xpc_dictionary_create_reply(void *original)
{
	(void)original;
	return NULL;
}

void *xpc_dictionary_get_value(void *dict, const char *key)
{
	(void)dict;
	(void)key;
	return NULL;
}

const char *xpc_dictionary_get_string(void *dict, const char *key)
{
	(void)dict;
	(void)key;
	return NULL;
}

int64_t xpc_dictionary_get_int64(void *dict, const char *key)
{
	(void)dict;
	(void)key;
	return 0;
}

uint64_t xpc_dictionary_get_uint64(void *dict, const char *key)
{
	(void)dict;
	(void)key;
	return 0;
}

void xpc_dictionary_set_value(void *dict, const char *key, void *value)
{
	(void)dict;
	(void)key;
	(void)value;
}

void xpc_dictionary_set_string(void *dict, const char *key, const char *val)
{
	(void)dict;
	(void)key;
	(void)val;
}

void xpc_dictionary_set_int64(void *dict, const char *key, int64_t val)
{
	(void)dict;
	(void)key;
	(void)val;
}

void xpc_dictionary_set_uint64(void *dict, const char *key, uint64_t val)
{
	(void)dict;
	(void)key;
	(void)val;
}

void xpc_dictionary_get_audit_token(void *dict, void *token)
{
	(void)dict;
	(void)token;
}

unsigned int xpc_dictionary_copy_mach_send(void *dict, const char *key)
{
	(void)dict;
	(void)key;
	return 0;
}

void xpc_dictionary_set_mach_send(void *dict, const char *key, unsigned int port)
{
	(void)dict;
	(void)key;
	(void)port;
}

void xpc_dictionary_set_mach_recv(void *dict, const char *key, unsigned int port)
{
	(void)dict;
	(void)key;
	(void)port;
}

void *xpc_array_create(const void *const *objects, size_t count)
{
	(void)objects;
	(void)count;
	return NULL;
}

void xpc_array_append_value(void *array, void *value)
{
	(void)array;
	(void)value;
}

void xpc_array_set_string(void *array, size_t index, const char *val)
{
	(void)array;
	(void)index;
	(void)val;
}

void xpc_array_set_uint64(void *array, size_t index, uint64_t val)
{
	(void)array;
	(void)index;
	(void)val;
}

void *xpc_retain(void *obj)
{
	return obj;
}

void xpc_release(void *obj)
{
	(void)obj;
}

const void *xpc_get_type(void *obj)
{
	(void)obj;
	return NULL;
}

int xpc_bool_get_value(void *obj)
{
	(void)obj;
	return 0;
}

uint64_t xpc_uint64_get_value(void *obj)
{
	(void)obj;
	return 0;
}

char *xpc_copy_description(void *obj)
{
	(void)obj;
	return (char *)"xpc_stub";
}

void *xpc_copy_entitlements_for_pid(int pid)
{
	(void)pid;
	return NULL;
}

const char *xpc_strerror(int error)
{
	(void)error;
	return "xpc not available";
}
