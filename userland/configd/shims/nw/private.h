#ifndef PANTHERA_NW_PRIVATE_H
#define PANTHERA_NW_PRIVATE_H

#include <xpc/xpc.h>

#ifndef XPC_ARRAY_APPEND
#define XPC_ARRAY_APPEND ((size_t)-1)
#endif

/*
 * KernelEventMonitor notifies IPConfiguration through this Network.framework
 * private entrypoint on Darwin. Panthera does not carry IPConfiguration yet,
 * so the implementation is a compatibility seam until the Apple bootp /
 * IPConfiguration source is recovered and staged.
 */
void network_config_check_interface_settings(xpc_object_t if_list);

#endif /* PANTHERA_NW_PRIVATE_H */
