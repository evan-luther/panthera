#ifndef _PANTHERA_LIBDISPATCH_SYS_EVENT_PRIVATE_H
#define _PANTHERA_LIBDISPATCH_SYS_EVENT_PRIVATE_H

#ifndef PRIVATE
#define PANTHERA_EVENT_PRIVATE_WAS_UNSET 1
#define PRIVATE 1
#endif

#include <bsd/sys/event.h>
#include <bsd/sys/event_private.h>

#ifdef PANTHERA_EVENT_PRIVATE_WAS_UNSET
#undef PANTHERA_EVENT_PRIVATE_WAS_UNSET
#undef PRIVATE
#endif

#endif
