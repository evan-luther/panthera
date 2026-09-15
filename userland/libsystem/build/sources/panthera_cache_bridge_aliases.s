/*
 * panthera_cache_bridge_aliases.s
 *
 * Assembly aliases for ___platform_* symbols needed by cache-resident
 * libsystem_c. The canonical provider (libsystem_platform) exports
 * __platform_foo, but libc expects ___platform_foo (extra underscore).
 * These aliases bridge the naming gap within the cache.
 */
.text

.globl ___platform_bzero
___platform_bzero: jmp __platform_bzero

.globl ___platform_memcmp
___platform_memcmp: jmp __platform_memcmp

.globl ___platform_memmove
___platform_memmove: jmp __platform_memmove

.globl ___platform_memset
___platform_memset: jmp __platform_memset

.globl ___platform_strchr
___platform_strchr: jmp __platform_strchr

.globl ___platform_strcmp
___platform_strcmp: jmp __platform_strcmp

.globl ___platform_strcpy
___platform_strcpy: jmp __platform_strcpy

.globl ___platform_strlcat
___platform_strlcat: jmp __platform_strlcat

.globl ___platform_strlcpy
___platform_strlcpy: jmp __platform_strlcpy

.globl ___platform_strlen
___platform_strlen: jmp __platform_strlen

.globl ___platform_strncmp
___platform_strncmp: jmp __platform_strncmp

.globl ___platform_strncpy
___platform_strncpy: jmp __platform_strncpy

.globl ___platform_strnlen
___platform_strnlen: jmp __platform_strnlen

.globl ___platform_strstr
___platform_strstr: jmp __platform_strstr
