/*
 * System/sys/fileport.h — Fileport compatibility shim for Panthera.
 */
#ifndef _SYS_FILEPORT_H
#define _SYS_FILEPORT_H

#include <mach/mach.h>

typedef mach_port_t fileport_t;

#ifndef fileport_makeport
#define fileport_makeport(fd, port) (*(port) = MACH_PORT_NULL, 0)
#endif

#ifndef fileport_makefd
#define fileport_makefd(port) (-1)
#endif

#endif /* _SYS_FILEPORT_H */
