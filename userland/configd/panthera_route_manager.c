/*
 * Panthera configd route manager.
 *
 * This follows the IPMonitor ownership shape: dynamic-store service IPv4
 * updates are the input, and the route manager applies kernel routes from
 * that service state.  The implementation is intentionally narrow for the
 * current single-interface DHCP boot path, but it uses an IPMonitor-style
 * route message with interface and interface-address addrs instead of writing
 * routes from the global-state publisher.
 */

#include "panthera_route_manager.h"

#include <SystemConfiguration/SystemConfiguration.h>
#include <arpa/inet.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef IFNAMSIZ
#define IFNAMSIZ 16
#endif

#define PANTHERA_ROUTE_MSG_ADDRS_SPACE \
    (3 * sizeof(struct sockaddr_in) + sizeof(struct sockaddr_dl) + 64)

typedef struct {
    struct rt_msghdr hdr;
    char addrs[PANTHERA_ROUTE_MSG_ADDRS_SPACE];
} panthera_route_msg_t;

static int route_seq;

extern int *__error(void);
extern unsigned int if_nametoindex(const char *);

static int
route_errno(void)
{
    return *__error();
}

static void
route_log(const char *fmt, ...)
{
    char buffer[256];
    va_list ap;
    int len;
    const char *trace = getenv("PANTHERA_CONFIGD_ROUTE_TRACE");

    if (trace == NULL || trace[0] == '\0' || strcmp(trace, "0") == 0) {
        return;
    }

    va_start(ap, fmt);
    len = vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);
    if (len <= 0) {
        return;
    }
    if ((size_t)len >= sizeof(buffer)) {
        len = (int)sizeof(buffer) - 1;
    }
    (void)write(STDERR_FILENO, buffer, (size_t)len);
}

static int
copy_cfstring(CFStringRef str, char *buf, size_t bufsize)
{
    if (str == NULL || buf == NULL || bufsize == 0 ||
        CFGetTypeID(str) != CFStringGetTypeID()) {
        return 0;
    }
    return CFStringGetCString(str, buf, (CFIndex)bufsize, kCFStringEncodingUTF8);
}

static int
copy_first_ipv4(CFDictionaryRef dict, CFStringRef key, struct in_addr *addr)
{
    CFArrayRef values;
    CFStringRef string;
    char text[64];

    if (dict == NULL || key == NULL || addr == NULL) {
        return 0;
    }
    values = (CFArrayRef)CFDictionaryGetValue(dict, key);
    if (values == NULL || CFGetTypeID(values) != CFArrayGetTypeID() ||
        CFArrayGetCount(values) <= 0) {
        return 0;
    }
    string = (CFStringRef)CFArrayGetValueAtIndex(values, 0);
    if (!copy_cfstring(string, text, sizeof(text))) {
        return 0;
    }
    return inet_pton(AF_INET, text, addr) == 1;
}

static int
copy_ipv4_value(CFDictionaryRef dict, CFStringRef key, struct in_addr *addr)
{
    CFStringRef string;
    char text[64];

    if (dict == NULL || key == NULL || addr == NULL) {
        return 0;
    }
    string = (CFStringRef)CFDictionaryGetValue(dict, key);
    if (!copy_cfstring(string, text, sizeof(text))) {
        return 0;
    }
    return inet_pton(AF_INET, text, addr) == 1;
}

static int
is_service_ipv4_key(CFStringRef key)
{
    return key != NULL &&
           CFGetTypeID(key) == CFStringGetTypeID() &&
           CFStringHasPrefix(key, CFSTR("State:/Network/Service/")) &&
           CFStringHasSuffix(key, CFSTR("/IPv4"));
}

static int
open_routing_socket(void)
{
    int fd = socket(PF_ROUTE, SOCK_RAW, PF_ROUTE);

    if (fd < 0) {
        route_log("PANTHERA:configd IPMonitor route socket failed errno=%d\n",
                  route_errno());
    }
    return fd;
}

static int
apply_default_route(const char *ifname,
                    unsigned int ifindex,
                    struct in_addr address,
                    struct in_addr router)
{
    panthera_route_msg_t msg;
    void *cursor;
    struct sockaddr_in *sin;
    struct sockaddr_dl *sdl;
    int fd;
    size_t len;
    ssize_t written;

    if (ifname == NULL || router.s_addr == INADDR_ANY ||
        router.s_addr == INADDR_BROADCAST) {
        return EINVAL;
    }

    memset(&msg, 0, sizeof(msg));
    msg.hdr.rtm_type = RTM_ADD;
    msg.hdr.rtm_version = RTM_VERSION;
    msg.hdr.rtm_seq = ++route_seq;
    msg.hdr.rtm_flags = RTF_UP | RTF_GATEWAY | RTF_STATIC;
    msg.hdr.rtm_index = ifindex;
    msg.hdr.rtm_addrs = RTA_DST | RTA_GATEWAY | RTA_NETMASK;
    if (ifindex != 0) {
        msg.hdr.rtm_addrs |= RTA_IFP;
    }
    if (ifindex != 0 && address.s_addr != INADDR_ANY) {
        msg.hdr.rtm_addrs |= RTA_IFA;
    }

    cursor = msg.addrs;

    sin = (struct sockaddr_in *)cursor;
    sin->sin_len = sizeof(*sin);
    sin->sin_family = AF_INET;
    sin->sin_addr.s_addr = htonl(INADDR_ANY);
    cursor = (char *)cursor + sizeof(*sin);

    sin = (struct sockaddr_in *)cursor;
    sin->sin_len = sizeof(*sin);
    sin->sin_family = AF_INET;
    sin->sin_addr = router;
    cursor = (char *)cursor + sizeof(*sin);

    sin = (struct sockaddr_in *)cursor;
    sin->sin_len = sizeof(*sin);
    sin->sin_family = AF_INET;
    sin->sin_addr.s_addr = htonl(INADDR_ANY);
    cursor = (char *)cursor + sizeof(*sin);

    if (ifindex != 0) {
        sdl = (struct sockaddr_dl *)cursor;
        sdl->sdl_len = sizeof(*sdl);
        sdl->sdl_family = AF_LINK;
        sdl->sdl_index = ifindex;
        cursor = (char *)cursor + sizeof(*sdl);
    }

    if (ifindex != 0 && address.s_addr != INADDR_ANY) {
        sin = (struct sockaddr_in *)cursor;
        sin->sin_len = sizeof(*sin);
        sin->sin_family = AF_INET;
        sin->sin_addr = address;
        cursor = (char *)cursor + sizeof(*sin);
    }

    len = sizeof(msg.hdr) + (size_t)((char *)cursor - msg.addrs);
    msg.hdr.rtm_msglen = (unsigned short)len;

    fd = open_routing_socket();
    if (fd < 0) {
        return route_errno();
    }
    *(__error()) = 0;
    written = write(fd, &msg, len);
    route_log("PANTHERA:configd IPMonitor default route write=%ld errno=%d\n",
              (long)written, route_errno());
    if (written < 0) {
        int saved_errno = route_errno();
        close(fd);
        return saved_errno;
    }
    close(fd);
    return 0;
}

void
panthera_route_manager_store_value_changed(CFStringRef key,
                                           CFPropertyListRef value)
{
    CFDictionaryRef ipv4;
    CFStringRef interface_name;
    char ifname[IFNAMSIZ];
    struct in_addr address = { 0 };
    struct in_addr router = { 0 };
    unsigned int ifindex;

    if (!is_service_ipv4_key(key) || value == NULL ||
        CFGetTypeID(value) != CFDictionaryGetTypeID()) {
        return;
    }

    ipv4 = (CFDictionaryRef)value;
    interface_name = (CFStringRef)CFDictionaryGetValue(ipv4, kSCPropInterfaceName);
    if (!copy_cfstring(interface_name, ifname, sizeof(ifname))) {
        interface_name = (CFStringRef)CFDictionaryGetValue(ipv4, CFSTR("InterfaceName"));
        if (!copy_cfstring(interface_name, ifname, sizeof(ifname))) {
            route_log("PANTHERA:configd IPMonitor route skipped missing interface\n");
            return;
        }
    }

    ifindex = if_nametoindex(ifname);
    if (ifindex == 0) {
        route_log("PANTHERA:configd IPMonitor route unscoped ifindex %s\n",
                  ifname);
    }

    if (!copy_ipv4_value(ipv4, kSCPropNetIPv4Router, &router) ||
        router.s_addr == INADDR_ANY ||
        router.s_addr == INADDR_BROADCAST) {
        route_log("PANTHERA:configd IPMonitor route skipped no router\n");
        return;
    }
    (void)copy_first_ipv4(ipv4, kSCPropNetIPv4Addresses, &address);
    (void)apply_default_route(ifname, ifindex, address, router);
}
