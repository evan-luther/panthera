/*
 * _simple.h — Stub for Apple's private simplified string/ASL API.
 * Used by core.c for logging and string formatting.
 */
#ifndef _SIMPLE_H
#define _SIMPLE_H

#include <stdarg.h>

typedef char *_SIMPLE_STRING;

static inline _SIMPLE_STRING _simple_salloc(void) { return NULL; }
static inline void _simple_sfree(_SIMPLE_STRING s) { (void)s; }
static inline int _simple_sprintf(_SIMPLE_STRING s, const char *fmt, ...) { (void)s; (void)fmt; return 0; }
static inline int _simple_vsprintf(_SIMPLE_STRING s, const char *fmt, va_list ap) { (void)s; (void)fmt; (void)ap; return 0; }
static inline const char *_simple_string(_SIMPLE_STRING s) { return s ? s : ""; }
static inline void _simple_sresize(_SIMPLE_STRING s) { (void)s; }
static inline int _simple_sappend(_SIMPLE_STRING s, const char *str) { (void)s; (void)str; return 0; }
static inline int _simple_esappend(_SIMPLE_STRING s, const char *str) { (void)s; (void)str; return 0; }
static inline void _simple_put(_SIMPLE_STRING s, int fd) { (void)s; (void)fd; }
static inline void _simple_putline(_SIMPLE_STRING s, int fd) { (void)s; (void)fd; }

/* ASL stubs */
static inline _SIMPLE_STRING _simple_asl_msg_new(void) { return NULL; }
static inline void _simple_asl_msg_set(_SIMPLE_STRING msg, const char *key, const char *val) { (void)msg; (void)key; (void)val; }
static inline void _simple_asl_send(_SIMPLE_STRING msg) { (void)msg; }
static inline int _simple_asl_log(int level, const char *facility, const char *msg) { (void)level; (void)facility; (void)msg; return 0; }
static inline char *_simple_getenv(const char **env, const char *key) { (void)env; (void)key; return NULL; }

/* vdprintf/dprintf stubs */
static inline void _simple_vdprintf(int fd, const char *fmt, va_list ap) { (void)fd; (void)fmt; (void)ap; }
static inline void _simple_dprintf(int fd, const char *fmt, ...) { (void)fd; (void)fmt; }
static inline int _simple_esprintf(_SIMPLE_STRING s, const char *fmt, ...) { (void)s; (void)fmt; return 0; }

#endif /* _SIMPLE_H */
