/* Panthera shim: _simple.h
 * Provides minimal _simple_asl_log used by objc-errors.mm
 */
#ifndef __SIMPLE_H__
#define __SIMPLE_H__

#include <stdarg.h>

#ifndef ASL_LEVEL_ERR
#define ASL_LEVEL_ERR 3
#endif

static inline void _simple_asl_log(int level, const char *facility, const char *message)
{
    (void)level;
    (void)facility;
    (void)message;
}

#endif /* __SIMPLE_H__ */
