/* Panthera shim: os/trace_private.h */
#ifndef _OS_TRACE_PRIVATE_H
#define _OS_TRACE_PRIVATE_H

#define OS_TRACE_MODE_DISABLE 0

#define os_trace(...)           ((void)0)
#define os_trace_debug(...)     ((void)0)
#define os_trace_error(...)     ((void)0)
#define os_trace_fault(...)     ((void)0)

static inline void os_trace_set_mode(int mode) { (void)mode; }

#endif /* _OS_TRACE_PRIVATE_H */
