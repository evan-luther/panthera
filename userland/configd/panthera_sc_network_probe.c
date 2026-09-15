/*
 * One-shot validation client for Panthera's phase-0 network dynamic-store keys.
 */

#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SystemConfiguration.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern void __CFInitialize(void);

static void
emit(const char *message)
{
    (void)write(STDOUT_FILENO, message, strlen(message));
    (void)write(STDOUT_FILENO, "\n", 1);
    (void)write(STDERR_FILENO, message, strlen(message));
    (void)write(STDERR_FILENO, "\n", 1);
}

static int
copy_cstring(CFStringRef value, char *out, size_t out_len)
{
    if (value == NULL || out == NULL || out_len == 0) {
        return 0;
    }
    return CFStringGetCString(value, out, out_len, kCFStringEncodingUTF8);
}

static CFStringRef
copy_dict_string(CFDictionaryRef dict, CFStringRef key)
{
    const void *value;

    if (dict == NULL || !CFDictionaryGetValueIfPresent(dict, key, &value)) {
        return NULL;
    }
    if (CFGetTypeID(value) != CFStringGetTypeID()) {
        return NULL;
    }
    CFRetain(value);
    return (CFStringRef)value;
}

static int
has_first_address(CFDictionaryRef dict, char *out, size_t out_len)
{
    const void *value;
    CFStringRef first;

    if (dict == NULL || !CFDictionaryGetValueIfPresent(dict, kSCPropNetIPv4Addresses, &value)) {
        return 0;
    }
    if (CFGetTypeID(value) != CFArrayGetTypeID() || CFArrayGetCount((CFArrayRef)value) < 1) {
        return 0;
    }
    first = (CFStringRef)CFArrayGetValueAtIndex((CFArrayRef)value, 0);
    if (CFGetTypeID(first) != CFStringGetTypeID()) {
        return 0;
    }
    return copy_cstring(first, out, out_len);
}

static const char *
type_name(CFTypeRef value)
{
    if (value == NULL) {
        return "null";
    }
    if (CFGetTypeID(value) == CFDictionaryGetTypeID()) {
        return "dictionary";
    }
    if (CFGetTypeID(value) == CFArrayGetTypeID()) {
        return "array";
    }
    if (CFGetTypeID(value) == CFStringGetTypeID()) {
        return "string";
    }
    if (CFGetTypeID(value) == CFNumberGetTypeID()) {
        return "number";
    }
    if (CFGetTypeID(value) == CFBooleanGetTypeID()) {
        return "boolean";
    }
    if (CFGetTypeID(value) == CFDataGetTypeID()) {
        return "data";
    }
    return "unknown";
}

static void
emit_probe_detail(CFTypeRef global, CFStringRef primaryInterface,
                  CFTypeRef interfaceIPv4, const char *ifname,
                  const char *address)
{
    const void *addresses = NULL;
    char line[256];

    snprintf(line, sizeof(line),
             "PANTHERA:sc-network probe detail global=%s primary=%s interface=%s address=%s",
             type_name(global),
             primaryInterface != NULL ? ifname : "missing",
             type_name(interfaceIPv4),
             address != NULL && address[0] != '\0' ? address : "missing");
    emit(line);

    if (interfaceIPv4 != NULL && CFGetTypeID(interfaceIPv4) == CFDictionaryGetTypeID()) {
        if (!CFDictionaryGetValueIfPresent((CFDictionaryRef)interfaceIPv4,
                                           kSCPropNetIPv4Addresses,
                                           &addresses)) {
            emit("PANTHERA:sc-network probe detail IPv4 Addresses missing");
        } else {
            snprintf(line, sizeof(line),
                     "PANTHERA:sc-network probe detail IPv4 Addresses type=%s count=%ld",
                     type_name(addresses),
                     CFGetTypeID(addresses) == CFArrayGetTypeID()
                         ? (long)CFArrayGetCount((CFArrayRef)addresses)
                         : -1L);
            emit(line);
        }
    }
}

static int
is_usable_primary_ipv4(const char *address)
{
    struct in_addr addr;
    uint32_t host_order;

    if (address == NULL || inet_pton(AF_INET, address, &addr) != 1) {
        return 0;
    }
    host_order = ntohl(addr.s_addr);
    if (host_order == 0 || (host_order >> 24) == 127) {
        return 0;
    }
    if ((host_order & 0xffff0000U) == 0xa9fe0000U) {
        return 0;
    }
    if ((host_order & 0xf0000000U) == 0xe0000000U) {
        return 0;
    }
    return 1;
}

int
main(void)
{
    SCDynamicStoreRef store;
    CFStringRef globalKey = NULL;
    CFStringRef interfaceKey = NULL;
    CFDictionaryRef global = NULL;
    CFDictionaryRef interfaceIPv4 = NULL;
    CFStringRef primaryInterface = NULL;
    char ifname[64] = "";
    char address[64] = "";
    char line[192];
    int detailed = 0;
    int rc = 1;

    __CFInitialize();
    emit("PANTHERA:sc-network probe start");
    usleep(2 * 1000 * 1000);
    emit("PANTHERA:sc-network probe before-store-create");

    store = SCDynamicStoreCreate(NULL, CFSTR("panthera-network-probe"), NULL, NULL);
    if (store == NULL) {
        emit("PANTHERA:sc-network probe failed create-store");
        return 1;
    }
    emit("PANTHERA:sc-network probe store-create ok");

    for (int i = 0; i < 30; i++) {
        globalKey = SCDynamicStoreKeyCreateNetworkGlobalEntity(NULL, kSCDynamicStoreDomainState,
                                                               kSCEntNetIPv4);
        if (globalKey != NULL) {
            global = (CFDictionaryRef)SCDynamicStoreCopyValue(store, globalKey);
        }
        if (global != NULL && CFGetTypeID(global) == CFDictionaryGetTypeID()) {
            primaryInterface = copy_dict_string(global, kSCDynamicStorePropNetPrimaryInterface);
            if (copy_cstring(primaryInterface, ifname, sizeof(ifname))) {
                interfaceKey = SCDynamicStoreKeyCreateNetworkInterfaceEntity(NULL,
                                                                             kSCDynamicStoreDomainState,
                                                                             primaryInterface,
                                                                             kSCEntNetIPv4);
                if (interfaceKey != NULL) {
                    interfaceIPv4 = (CFDictionaryRef)SCDynamicStoreCopyValue(store, interfaceKey);
                    if (interfaceIPv4 != NULL &&
                        CFGetTypeID(interfaceIPv4) == CFDictionaryGetTypeID() &&
                        has_first_address(interfaceIPv4, address, sizeof(address)) &&
                        is_usable_primary_ipv4(address)) {
                        snprintf(line, sizeof(line),
                                 "PANTHERA:sc-network probe ok interface=%s address=%s",
                                 ifname, address);
                        emit(line);
                        rc = 0;
                        break;
                    }
                }
            }
        }

        if (!detailed &&
            (i == 0 || (global != NULL && CFGetTypeID(global) == CFDictionaryGetTypeID()))) {
            emit_probe_detail(global, primaryInterface, interfaceIPv4, ifname, address);
            detailed = 1;
        }

        if (interfaceIPv4 != NULL) CFRelease(interfaceIPv4);
        if (interfaceKey != NULL) CFRelease(interfaceKey);
        if (primaryInterface != NULL) CFRelease(primaryInterface);
        if (global != NULL) CFRelease(global);
        if (globalKey != NULL) CFRelease(globalKey);
        interfaceIPv4 = NULL;
        interfaceKey = NULL;
        primaryInterface = NULL;
        global = NULL;
        globalKey = NULL;
        if (i == 0 || i == 5 || i == 15 || i == 29) {
            emit("PANTHERA:sc-network probe waiting");
        }
        usleep(1000 * 1000);
    }

    if (rc != 0) {
        emit("PANTHERA:sc-network probe failed missing-global-ipv4");
    }

    if (interfaceIPv4 != NULL) CFRelease(interfaceIPv4);
    if (interfaceKey != NULL) CFRelease(interfaceKey);
    if (primaryInterface != NULL) CFRelease(primaryInterface);
    if (global != NULL) CFRelease(global);
    if (globalKey != NULL) CFRelease(globalKey);
    CFRelease(store);
    return rc;
}
