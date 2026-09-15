#ifndef PANTHERA_LIBDISPATCH_CONFIG_H
#define PANTHERA_LIBDISPATCH_CONFIG_H

#include "../../../../../../src/libdispatch-1462.0.4/config/config.h"

#undef HAVE_LIBPROC_INTERNAL_H
#define HAVE_LIBPROC_INTERNAL_H 0
#undef HAVE_OBJC
#define HAVE_OBJC 0
#undef HAVE_PTHREAD_MACHDEP_H
#define HAVE_PTHREAD_MACHDEP_H 0
#undef HAVE_SYS_GUARDED_H
#define HAVE_SYS_GUARDED_H 0

#endif
