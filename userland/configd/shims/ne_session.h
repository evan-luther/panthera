#ifndef PANTHERA_NE_SESSION_H
#define PANTHERA_NE_SESSION_H

#include <CoreFoundation/CoreFoundation.h>
#include <stdint.h>

typedef struct ne_session *ne_session_t;
typedef int ne_session_status_t;

static inline ne_session_t
ne_session_create(CFStringRef serviceID __unused, CFStringRef type __unused)
{
    return NULL;
}

static inline void
ne_session_release(ne_session_t session __unused)
{
}

static inline int
ne_session_get_status(ne_session_t session __unused)
{
    return 0;
}

#endif /* PANTHERA_NE_SESSION_H */
