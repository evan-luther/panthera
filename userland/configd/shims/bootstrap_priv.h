#ifndef PANTHERA_CONFIGD_BOOTSTRAP_PRIV_H
#define PANTHERA_CONFIGD_BOOTSTRAP_PRIV_H

#include <mach/mach.h>
#include <servers/bootstrap.h>

#ifndef BOOTSTRAP_MAX_NAME_LEN
#define BOOTSTRAP_MAX_NAME_LEN 128
#endif

#ifndef BOOTSTRAP_STATUS_ACTIVE
#define BOOTSTRAP_STATUS_ACTIVE 1
#endif

kern_return_t bootstrap_check_in(mach_port_t bp, const name_t service_name, mach_port_t *sp);
kern_return_t bootstrap_look_up(mach_port_t bp, const name_t service_name, mach_port_t *sp);
kern_return_t bootstrap_register(mach_port_t bp, name_t service_name, mach_port_t sp);

#endif /* PANTHERA_CONFIGD_BOOTSTRAP_PRIV_H */
