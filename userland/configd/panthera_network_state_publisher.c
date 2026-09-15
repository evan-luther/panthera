/*
 * Panthera phase-0 network state publisher.
 *
 * This is a small SystemConfiguration client that mirrors BSD interface state
 * into standard SCDynamicStore State:/Network keys. It is intentionally kept
 * outside configd so it can be replaced by Apple's KernelEventMonitor/IPMonitor
 * plugins without changing the dynamic-store server.
 */

#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SystemConfiguration.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/ioccom.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>


extern void __CFInitialize(void);
#define PANTHERA_IFNAMSIZ 16
#define PANTHERA_IFF_UP 0x1
#define PANTHERA_IFF_LOOPBACK 0x8

/*
 * Panthera's sysroot net/if.h currently exposes kernel-private headers that
 * conflict with the host SDK during cross-builds. Keep the stable BSD ioctl
 * ABI surface local until the userland header split is cleaned up.
 */
struct panthera_ifreq {
    char ifr_name[PANTHERA_IFNAMSIZ];
    union {
        struct sockaddr ifru_addr;
        short ifru_flags;
        int ifru_metric;
        int ifru_mtu;
        caddr_t ifru_data;
        int ifru_cap[2];
    } ifr_ifru;
};

struct __attribute__((packed, aligned(4))) panthera_ifconf {
    int ifc_len;
    union {
        caddr_t ifcu_buf;
        struct panthera_ifreq *ifcu_req;
    } ifc_ifcu;
};

#define ifr_addr ifr_ifru.ifru_addr
#define ifr_flags ifr_ifru.ifru_flags
#define ifc_buf ifc_ifcu.ifcu_buf

#define PANTHERA_SIOCGIFFLAGS _IOWR('i', 17, struct panthera_ifreq)
#define PANTHERA_SIOCGIFADDR _IOWR('i', 33, struct panthera_ifreq)
#define PANTHERA_SIOCGIFCONF _IOWR('i', 36, struct panthera_ifconf)
#define PANTHERA_SIOCGIFNETMASK _IOWR('i', 37, struct panthera_ifreq)

typedef struct {
    char ifname[PANTHERA_IFNAMSIZ];
    char address[INET_ADDRSTRLEN];
    char netmask[INET_ADDRSTRLEN];
    unsigned int flags;
} InterfaceState;

static void
log_line(const char *fmt, ...)
{
    FILE *log = fopen("/var/log/sc-network-state.log", "a");
    va_list ap;

    if (log == NULL) {
        return;
    }

    va_start(ap, fmt);
    fputs("sc-network-state: ", log);
    vfprintf(log, fmt, ap);
    fputc('\n', log);
    va_end(ap);
    fclose(log);
}

static void
write_marker(const char *path, const char *message)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        (void)write(fd, message, strlen(message));
        (void)close(fd);
    }
}

static void
emit_validation(const char *message)
{
    if (getenv("PANTHERA_SC_NETWORK_VALIDATE") == NULL) {
        return;
    }

    (void)write(STDERR_FILENO, message, strlen(message));
    (void)write(STDERR_FILENO, "\n", 1);
}

static int
copy_ipv4_for_interface(int fd, const char *ifname, unsigned long request, char out[INET_ADDRSTRLEN])
{
    struct panthera_ifreq ifr;
    struct sockaddr_in *sin;

    memset(&ifr, 0, sizeof(ifr));
    strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
    if (ioctl(fd, request, &ifr) != 0) {
        return 0;
    }

    sin = (struct sockaddr_in *)&ifr.ifr_addr;
    if (sin->sin_family != AF_INET) {
        return 0;
    }

    return inet_ntop(AF_INET, &sin->sin_addr, out, INET_ADDRSTRLEN) != NULL;
}

static int
copy_flags_for_interface(int fd, const char *ifname, unsigned int *flags)
{
    struct panthera_ifreq ifr;

    memset(&ifr, 0, sizeof(ifr));
    strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
    if (ioctl(fd, PANTHERA_SIOCGIFFLAGS, &ifr) != 0) {
        return 0;
    }

    *flags = (unsigned int)ifr.ifr_flags;
    return 1;
}

static int
interface_has_address(int fd, const char *ifname, InterfaceState *state)
{
    unsigned int flags = 0;

    memset(state, 0, sizeof(*state));
    strlcpy(state->ifname, ifname, sizeof(state->ifname));

    if (!copy_flags_for_interface(fd, ifname, &flags)) {
        return 0;
    }
    if ((flags & PANTHERA_IFF_LOOPBACK) != 0) {
        return 0;
    }

    if (!copy_ipv4_for_interface(fd, ifname, PANTHERA_SIOCGIFADDR, state->address)) {
        return 0;
    }
    if (!copy_ipv4_for_interface(fd, ifname, PANTHERA_SIOCGIFNETMASK, state->netmask)) {
        strlcpy(state->netmask, "255.255.255.0", sizeof(state->netmask));
    }

    state->flags = flags;
    return 1;
}

static int
find_primary_interface(InterfaceState *state)
{
    const char *preferred = getenv("PANTHERA_SC_NETWORK_INTERFACE");
    char ifconf_buf[4096];
    struct panthera_ifconf ifc;
    int fd;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        log_line("socket failed");
        return 0;
    }

    if (preferred != NULL && preferred[0] != '\0' &&
        interface_has_address(fd, preferred, state)) {
        close(fd);
        return 1;
    }

    if (interface_has_address(fd, "en0", state)) {
        close(fd);
        return 1;
    }

    memset(&ifc, 0, sizeof(ifc));
    ifc.ifc_len = sizeof(ifconf_buf);
    ifc.ifc_buf = ifconf_buf;
    if (ioctl(fd, PANTHERA_SIOCGIFCONF, &ifc) == 0) {
        char *cursor = ifc.ifc_buf;
        char *end = ifc.ifc_buf + ifc.ifc_len;
        while (cursor + sizeof(struct panthera_ifreq) <= end) {
            struct panthera_ifreq *ifr = (struct panthera_ifreq *)cursor;
            size_t len = sizeof(ifr->ifr_name) + ifr->ifr_addr.sa_len;
            if (len < sizeof(struct panthera_ifreq)) {
                len = sizeof(struct panthera_ifreq);
            }
            if (interface_has_address(fd, ifr->ifr_name, state)) {
                close(fd);
                return 1;
            }
            cursor += len;
        }
    }

    close(fd);
    return 0;
}

static CFArrayRef
copy_single_string_array(CFStringRef value)
{
    const void *values[1] = { value };
    return CFArrayCreate(NULL, values, 1, &kCFTypeArrayCallBacks);
}

static int
publish_setup_dhcp_dictionary(CFMutableDictionaryRef store, CFStringRef ifname, CFStringRef service)
{
    CFArrayRef serviceOrder = NULL;
    CFMutableDictionaryRef interfaceDict = NULL;
    CFMutableDictionaryRef ipv4Dict = NULL;
    CFMutableDictionaryRef globalIPv4 = NULL;
    CFStringRef key = NULL;
    int ok = 0;

    serviceOrder = copy_single_string_array(service);
    interfaceDict = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                                              &kCFTypeDictionaryValueCallBacks);
    ipv4Dict = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                                         &kCFTypeDictionaryValueCallBacks);
    globalIPv4 = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                                           &kCFTypeDictionaryValueCallBacks);
    if (serviceOrder == NULL || interfaceDict == NULL || ipv4Dict == NULL || globalIPv4 == NULL) {
        goto done;
    }

    CFDictionarySetValue(interfaceDict, kSCPropNetInterfaceDeviceName, ifname);
    CFDictionarySetValue(interfaceDict, kSCPropNetInterfaceType, kSCValNetInterfaceTypeEthernet);
    CFDictionarySetValue(ipv4Dict, kSCPropNetIPv4ConfigMethod, kSCValNetIPv4ConfigMethodDHCP);
    CFDictionarySetValue(globalIPv4, kSCPropNetServiceOrder, serviceOrder);

    key = SCDynamicStoreKeyCreateNetworkServiceEntity(NULL, kSCDynamicStoreDomainSetup,
                                                      service, kSCEntNetInterface);
    if (key == NULL) goto done;
    CFDictionarySetValue(store, key, interfaceDict);
    CFRelease(key);
    key = NULL;

    key = SCDynamicStoreKeyCreateNetworkServiceEntity(NULL, kSCDynamicStoreDomainSetup,
                                                      service, kSCEntNetIPv4);
    if (key == NULL) goto done;
    CFDictionarySetValue(store, key, ipv4Dict);
    CFRelease(key);
    key = NULL;

    key = SCDynamicStoreKeyCreateNetworkGlobalEntity(NULL, kSCDynamicStoreDomainSetup,
                                                     kSCEntNetIPv4);
    if (key == NULL) goto done;
    CFDictionarySetValue(store, key, globalIPv4);

    ok = 1;

done:
    if (key != NULL) CFRelease(key);
    if (serviceOrder != NULL) CFRelease(serviceOrder);
    if (interfaceDict != NULL) CFRelease(interfaceDict);
    if (ipv4Dict != NULL) CFRelease(ipv4Dict);
    if (globalIPv4 != NULL) CFRelease(globalIPv4);
    return ok;
}

static CFStringRef
copy_cfstr(const char *value)
{
    return CFStringCreateWithCString(NULL, value, kCFStringEncodingUTF8);
}

static int
publish_network_state(SCDynamicStoreRef store, const InterfaceState *state)
{
    CFStringRef ifname = NULL;
    CFStringRef address = NULL;
    CFStringRef netmask = NULL;
    CFStringRef router = NULL;
    CFStringRef dns = NULL;
    CFStringRef service = NULL;
    CFArrayRef interfaces = NULL;
    CFArrayRef addresses = NULL;
    CFArrayRef netmasks = NULL;
    CFArrayRef dnsServers = NULL;
    CFMutableDictionaryRef interfaceDict = NULL;
    CFMutableDictionaryRef ipv4Dict = NULL;
    CFMutableDictionaryRef globalIPv4 = NULL;
    CFMutableDictionaryRef dnsDict = NULL;
    CFStringRef key = NULL;
    const char *routerText = getenv("PANTHERA_SC_NETWORK_ROUTER");
    const char *dnsText = getenv("PANTHERA_SC_NETWORK_DNS");
    int ok = 0;

    if (routerText == NULL || routerText[0] == '\0') {
        routerText = "10.0.2.2";
    }
    if (dnsText == NULL || dnsText[0] == '\0') {
        dnsText = "10.0.2.3";
    }

    ifname = copy_cfstr(state->ifname);
    address = copy_cfstr(state->address);
    netmask = copy_cfstr(state->netmask);
    router = copy_cfstr(routerText);
    dns = copy_cfstr(dnsText);
    service = CFStringCreateWithFormat(NULL, NULL, CFSTR("Panthera-%@"), ifname);
    if (ifname == NULL || address == NULL || netmask == NULL ||
        router == NULL || dns == NULL || service == NULL) {
        goto done;
    }

    interfaces = copy_single_string_array(ifname);
    addresses = copy_single_string_array(address);
    netmasks = copy_single_string_array(netmask);
    dnsServers = copy_single_string_array(dns);
    interfaceDict = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                                              &kCFTypeDictionaryValueCallBacks);
    ipv4Dict = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                                         &kCFTypeDictionaryValueCallBacks);
    globalIPv4 = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                                           &kCFTypeDictionaryValueCallBacks);
    dnsDict = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                                        &kCFTypeDictionaryValueCallBacks);
    if (interfaces == NULL || addresses == NULL || netmasks == NULL || dnsServers == NULL ||
        interfaceDict == NULL || ipv4Dict == NULL || globalIPv4 == NULL || dnsDict == NULL) {
        goto done;
    }

    CFDictionarySetValue(interfaceDict, kSCPropNetInterfaces, interfaces);

    CFDictionarySetValue(ipv4Dict, kSCPropNetIPv4Addresses, addresses);
    CFDictionarySetValue(ipv4Dict, kSCPropNetIPv4SubnetMasks, netmasks);
    CFDictionarySetValue(ipv4Dict, CFSTR("InterfaceName"), ifname);

    CFDictionarySetValue(globalIPv4, kSCDynamicStorePropNetPrimaryInterface, ifname);
    CFDictionarySetValue(globalIPv4, kSCDynamicStorePropNetPrimaryService, service);
    CFDictionarySetValue(globalIPv4, kSCPropNetIPv4Router, router);

    CFDictionarySetValue(dnsDict, kSCPropNetDNSServerAddresses, dnsServers);

    key = SCDynamicStoreKeyCreateNetworkInterface(NULL, kSCDynamicStoreDomainState);
    if (key == NULL || !SCDynamicStoreSetValue(store, key, interfaceDict)) goto done;
    CFRelease(key);
    key = NULL;

    key = SCDynamicStoreKeyCreateNetworkInterfaceEntity(NULL, kSCDynamicStoreDomainState,
                                                        ifname, kSCEntNetIPv4);
    if (key == NULL || !SCDynamicStoreSetValue(store, key, ipv4Dict)) goto done;
    CFRelease(key);
    key = NULL;

    key = SCDynamicStoreKeyCreateNetworkServiceEntity(NULL, kSCDynamicStoreDomainState,
                                                      service, kSCEntNetIPv4);
    if (key == NULL || !SCDynamicStoreSetValue(store, key, ipv4Dict)) goto done;
    CFRelease(key);
    key = NULL;

    key = SCDynamicStoreKeyCreateNetworkGlobalEntity(NULL, kSCDynamicStoreDomainState,
                                                     kSCEntNetIPv4);
    if (key == NULL || !SCDynamicStoreSetValue(store, key, globalIPv4)) goto done;
    CFRelease(key);
    key = NULL;

    key = SCDynamicStoreKeyCreateNetworkGlobalEntity(NULL, kSCDynamicStoreDomainState,
                                                     kSCEntNetDNS);
    if (key == NULL || !SCDynamicStoreSetValue(store, key, dnsDict)) goto done;
    CFRelease(key);
    key = NULL;

    ok = 1;
    log_line("published %s address=%s netmask=%s router=%s dns=%s",
             state->ifname, state->address, state->netmask, routerText, dnsText);
    write_marker("/var/run/sc_network_state.ok", state->ifname);

done:
    if (key != NULL) CFRelease(key);
    if (ifname != NULL) CFRelease(ifname);
    if (address != NULL) CFRelease(address);
    if (netmask != NULL) CFRelease(netmask);
    if (router != NULL) CFRelease(router);
    if (dns != NULL) CFRelease(dns);
    if (service != NULL) CFRelease(service);
    if (interfaces != NULL) CFRelease(interfaces);
    if (addresses != NULL) CFRelease(addresses);
    if (netmasks != NULL) CFRelease(netmasks);
    if (dnsServers != NULL) CFRelease(dnsServers);
    if (interfaceDict != NULL) CFRelease(interfaceDict);
    if (ipv4Dict != NULL) CFRelease(ipv4Dict);
    if (globalIPv4 != NULL) CFRelease(globalIPv4);
    if (dnsDict != NULL) CFRelease(dnsDict);
    return ok;
}

int
panthera_network_state_publish_dictionary(CFMutableDictionaryRef store)
{
    InterfaceState state;
    CFStringRef ifname = NULL;
    CFStringRef address = NULL;
    CFStringRef netmask = NULL;
    CFStringRef router = NULL;
    CFStringRef dns = NULL;
    CFStringRef service = NULL;
    CFArrayRef interfaces = NULL;
    CFArrayRef addresses = NULL;
    CFArrayRef netmasks = NULL;
    CFArrayRef dnsServers = NULL;
    CFMutableDictionaryRef interfaceDict = NULL;
    CFMutableDictionaryRef ipv4Dict = NULL;
    CFMutableDictionaryRef globalIPv4 = NULL;
    CFMutableDictionaryRef dnsDict = NULL;
    CFStringRef key = NULL;
    const char *routerText = getenv("PANTHERA_SC_NETWORK_ROUTER");
    const char *dnsText = getenv("PANTHERA_SC_NETWORK_DNS");
    int ok = 0;

    if (store == NULL) {
        return 0;
    }
    if (!find_primary_interface(&state)) {
        log_line("no primary interface address; setup preferences unchanged");
        return 0;
    }
    if (routerText == NULL || routerText[0] == '\0') {
        routerText = "10.0.2.2";
    }
    if (dnsText == NULL || dnsText[0] == '\0') {
        dnsText = "10.0.2.3";
    }

    ifname = copy_cfstr(state.ifname);
    address = copy_cfstr(state.address);
    netmask = copy_cfstr(state.netmask);
    router = copy_cfstr(routerText);
    dns = copy_cfstr(dnsText);
    service = CFStringCreateWithFormat(NULL, NULL, CFSTR("Panthera-%@"), ifname);
    if (ifname == NULL || address == NULL || netmask == NULL ||
        router == NULL || dns == NULL || service == NULL) {
        goto done;
    }

    interfaces = copy_single_string_array(ifname);
    addresses = copy_single_string_array(address);
    netmasks = copy_single_string_array(netmask);
    dnsServers = copy_single_string_array(dns);
    interfaceDict = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                                              &kCFTypeDictionaryValueCallBacks);
    ipv4Dict = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                                         &kCFTypeDictionaryValueCallBacks);
    globalIPv4 = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                                           &kCFTypeDictionaryValueCallBacks);
    dnsDict = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                                        &kCFTypeDictionaryValueCallBacks);
    if (interfaces == NULL || addresses == NULL || netmasks == NULL || dnsServers == NULL ||
        interfaceDict == NULL || ipv4Dict == NULL || globalIPv4 == NULL || dnsDict == NULL) {
        goto done;
    }

    CFDictionarySetValue(interfaceDict, kSCPropNetInterfaces, interfaces);

    CFDictionarySetValue(ipv4Dict, kSCPropNetIPv4Addresses, addresses);
    CFDictionarySetValue(ipv4Dict, kSCPropNetIPv4SubnetMasks, netmasks);
    CFDictionarySetValue(ipv4Dict, CFSTR("InterfaceName"), ifname);

    CFDictionarySetValue(globalIPv4, kSCDynamicStorePropNetPrimaryInterface, ifname);
    CFDictionarySetValue(globalIPv4, kSCDynamicStorePropNetPrimaryService, service);
    CFDictionarySetValue(globalIPv4, kSCPropNetIPv4Router, router);

    CFDictionarySetValue(dnsDict, kSCPropNetDNSServerAddresses, dnsServers);

    key = SCDynamicStoreKeyCreateNetworkInterface(NULL, kSCDynamicStoreDomainState);
    if (key == NULL) goto done;
    CFDictionarySetValue(store, key, interfaceDict);
    CFRelease(key);
    key = NULL;

    key = SCDynamicStoreKeyCreateNetworkInterfaceEntity(NULL, kSCDynamicStoreDomainState,
                                                        ifname, kSCEntNetIPv4);
    if (key == NULL) goto done;
    CFDictionarySetValue(store, key, ipv4Dict);
    CFRelease(key);
    key = NULL;

    key = SCDynamicStoreKeyCreateNetworkServiceEntity(NULL, kSCDynamicStoreDomainState,
                                                      service, kSCEntNetIPv4);
    if (key == NULL) goto done;
    CFDictionarySetValue(store, key, ipv4Dict);
    CFRelease(key);
    key = NULL;

    key = SCDynamicStoreKeyCreateNetworkGlobalEntity(NULL, kSCDynamicStoreDomainState,
                                                     kSCEntNetIPv4);
    if (key == NULL) goto done;
    CFDictionarySetValue(store, key, globalIPv4);
    CFRelease(key);
    key = NULL;

    key = SCDynamicStoreKeyCreateNetworkGlobalEntity(NULL, kSCDynamicStoreDomainState,
                                                     kSCEntNetDNS);
    if (key == NULL) goto done;
    CFDictionarySetValue(store, key, dnsDict);
    CFRelease(key);
    key = NULL;

    if (!publish_setup_dhcp_dictionary(store, ifname, service)) goto done;

    ok = 1;
    log_line("published dictionary %s address=%s netmask=%s router=%s dns=%s setup=dhcp",
             state.ifname, state.address, state.netmask, routerText, dnsText);
    write_marker("/var/run/sc_network_state.ok", state.ifname);

done:
    if (key != NULL) CFRelease(key);
    if (ifname != NULL) CFRelease(ifname);
    if (address != NULL) CFRelease(address);
    if (netmask != NULL) CFRelease(netmask);
    if (router != NULL) CFRelease(router);
    if (dns != NULL) CFRelease(dns);
    if (service != NULL) CFRelease(service);
    if (interfaces != NULL) CFRelease(interfaces);
    if (addresses != NULL) CFRelease(addresses);
    if (netmasks != NULL) CFRelease(netmasks);
    if (dnsServers != NULL) CFRelease(dnsServers);
    if (interfaceDict != NULL) CFRelease(interfaceDict);
    if (ipv4Dict != NULL) CFRelease(ipv4Dict);
    if (globalIPv4 != NULL) CFRelease(globalIPv4);
    if (dnsDict != NULL) CFRelease(dnsDict);
    return ok;
}

static SCDynamicStoreRef
create_store_with_retry(void)
{
    SCDynamicStoreRef store;

    for (int i = 0; i < 30; i++) {
        store = SCDynamicStoreCreate(NULL, CFSTR("panthera-network-state"), NULL, NULL);
        if (store != NULL) {
            return store;
        }

        if (i == 0 || i == 4 || i == 14 || i == 29) {
            log_line("SCDynamicStoreCreate waiting sc=%d", SCError());
            emit_validation("PANTHERA:sc-network publisher store-create waiting");
        }
        usleep(1000 * 1000);
    }

    return NULL;
}

int
panthera_network_state_publisher_main(void)
{
    SCDynamicStoreRef store;
    InterfaceState state;
    char lastAddress[INET_ADDRSTRLEN] = "";
    char lastInterface[PANTHERA_IFNAMSIZ] = "";

    emit_validation("PANTHERA:sc-network publisher start");

    store = create_store_with_retry();
    if (store == NULL) {
        log_line("SCDynamicStoreCreate failed sc=%d", SCError());
        write_marker("/var/run/sc_network_state.failed", "store create failed\n");
        emit_validation("PANTHERA:sc-network publisher store-create failed");
        return 1;
    }
    emit_validation("PANTHERA:sc-network publisher store-create ok");

    for (;;) {
        if (find_primary_interface(&state)) {
            char line[192];
            snprintf(line, sizeof(line),
                     "PANTHERA:sc-network publisher found interface=%s address=%s",
                     state.ifname, state.address);
            emit_validation(line);
            if (strcmp(lastInterface, state.ifname) != 0 ||
                strcmp(lastAddress, state.address) != 0) {
                if (publish_network_state(store, &state)) {
                    emit_validation("PANTHERA:sc-network publisher published");
                    strlcpy(lastInterface, state.ifname, sizeof(lastInterface));
                    strlcpy(lastAddress, state.address, sizeof(lastAddress));
                } else {
                    log_line("publish failed sc=%d", SCError());
                    write_marker("/var/run/sc_network_state.failed", "publish failed\n");
                    emit_validation("PANTHERA:sc-network publisher publish failed");
                }
            }
        }
        usleep(5 * 1000 * 1000);
    }
}

#ifndef PANTHERA_NETWORK_STATE_NO_MAIN
int
main(void)
{
    __CFInitialize();
    return panthera_network_state_publisher_main();
}
#endif
