/* Panthera shim: fileport types
 * fileport_t is a mach_port_t used to represent file descriptors as ports.
 */
#ifndef _PANTHERA_FILEPORT_H
#define _PANTHERA_FILEPORT_H

#include <mach/mach.h>

typedef mach_port_t fileport_t;
#define FILEPORT_NULL MACH_PORT_NULL

/* fileport_makeport converts an fd to a mach port */
extern int fileport_makeport(int fd, fileport_t *port);

#endif /* _PANTHERA_FILEPORT_H */
