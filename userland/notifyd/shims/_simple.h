/* Panthera shim: _simple.h
 * Simple ASL logging stubs.
 */
#ifndef _SIMPLE_H_
#define _SIMPLE_H_

#include <stdint.h>
#include <asl.h>

static inline void
_simple_asl_log(int level, const char *facility, const char *message) {
    (void)level; (void)facility; (void)message;
}

static inline void
_simple_asl_log_prog(int level, const char *facility, const char *message, const char *prog) {
    (void)level; (void)facility; (void)message; (void)prog;
}

#endif /* _SIMPLE_H_ */
