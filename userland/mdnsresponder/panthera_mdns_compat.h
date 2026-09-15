#ifndef PANTHERA_MDNS_COMPAT_H
#define PANTHERA_MDNS_COMPAT_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stddef.h>
#include <stdint.h>

#ifndef INADDR_NONE
#define INADDR_NONE ((in_addr_t)0xffffffff)
#endif

#ifdef __cplusplus
extern "C" {
#endif

static inline in_addr_t panthera_mdns_inet_addr(const char *cp)
{
    struct in_addr _ina;
    if (cp != NULL && inet_aton(cp, &_ina)) {
        return _ina.s_addr;
    }
    return (in_addr_t)INADDR_NONE;
}

#ifdef __cplusplus
}
#endif

#ifdef inet_addr
#undef inet_addr
#endif
#define inet_addr(cp) panthera_mdns_inet_addr(cp)

#endif /* PANTHERA_MDNS_COMPAT_H */
