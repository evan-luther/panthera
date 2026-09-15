#ifndef _PANTHERA_LIBDISPATCH_SYS_KDEBUG_H
#define _PANTHERA_LIBDISPATCH_SYS_KDEBUG_H

#include <stdint.h>
#include_next <sys/kdebug.h>

__BEGIN_DECLS
int kdebug_trace(uint32_t code, uint64_t arg1, uint64_t arg2, uint64_t arg3,
		uint64_t arg4);
__END_DECLS

#endif
