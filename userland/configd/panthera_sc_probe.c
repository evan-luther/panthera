/*
 * Panthera SystemConfiguration dynamic-store smoke test.
 */

#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SystemConfiguration.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


extern void __CFInitialize(void);
static void
write_console(const char *message)
{
    (void)write(STDERR_FILENO, message, strlen(message));
}

static void
append_log(const char *message)
{
    static const char prefix[] = "PANTHERA:sc-probe ";
    if (getenv("PANTHERA_SC_PROBE_TRACE") == NULL) {
        return;
    }
    (void)write(STDERR_FILENO, prefix, sizeof(prefix) - 1);
    (void)write(STDERR_FILENO, message, strlen(message));
}

static void
append_error(const char *message)
{
    static const char prefix[] = "PANTHERA:sc-probe ";
    (void)write(STDERR_FILENO, prefix, sizeof(prefix) - 1);
    (void)write(STDERR_FILENO, message, strlen(message));
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
probe_spin_delay(void)
{
    volatile unsigned long i;

    for (i = 0; i != 1000000UL; i++) {
        __asm__ volatile("" ::: "memory");
    }
}

int
main(void)
{
    SCDynamicStoreRef store;
    CFStringRef key;
    CFStringRef value;
    CFPropertyListRef copied;

    __CFInitialize();

    key = CFSTR("State:/Panthera/Probe");
    value = CFSTR("ok");

    if (getenv("PANTHERA_SC_PROBE_DELAY_US") != NULL) {
        usleep((useconds_t)strtoul(getenv("PANTHERA_SC_PROBE_DELAY_US"), NULL, 10));
    }

    store = SCDynamicStoreCreate(NULL, CFSTR("panthera-sc-probe"), NULL, NULL);
    if (store == NULL) {
        append_error("SCDynamicStoreCreate failed\n");
        write_marker("/var/run/sc_dynamic_store_probe.failed", "create failed\n");
        return 1;
    }

    if (!SCDynamicStoreSetValue(store, key, value)) {
        append_error("SCDynamicStoreSetValue failed\n");
        write_marker("/var/run/sc_dynamic_store_probe.failed", "set failed\n");
        CFRelease(store);
        return 2;
    }

    copied = SCDynamicStoreCopyValue(store, key);
    if (copied == NULL || CFGetTypeID(copied) != CFStringGetTypeID() ||
        CFStringCompare((CFStringRef)copied, value, 0) != kCFCompareEqualTo) {
        append_error("SCDynamicStoreCopyValue mismatch\n");
        write_marker("/var/run/sc_dynamic_store_probe.failed", "copy mismatch\n");
        if (copied != NULL) CFRelease(copied);
        CFRelease(store);
        return 3;
    }

    CFRelease(copied);

    if (getenv("PANTHERA_SC_PROBE_NETWORK") != NULL) {
        CFStringRef ipv4Key = CFSTR("State:/Network/Global/IPv4");
        CFStringRef dnsKey = CFSTR("State:/Network/Global/DNS");
        CFPropertyListRef ipv4 = NULL;
        CFPropertyListRef dns = NULL;
        unsigned int attempt;

        for (attempt = 0; attempt != 60; attempt++) {
            ipv4 = SCDynamicStoreCopyValue(store, ipv4Key);
            if (ipv4 != NULL && CFGetTypeID(ipv4) == CFDictionaryGetTypeID()) {
                break;
            }
            if (ipv4 != NULL) {
                CFRelease(ipv4);
                ipv4 = NULL;
            }
            probe_spin_delay();
        }

        if (ipv4 == NULL || CFGetTypeID(ipv4) != CFDictionaryGetTypeID()) {
            append_error("State:/Network/Global/IPv4 missing\n");
            write_marker("/var/run/sc_dynamic_store_network_probe.failed", "missing global IPv4\n");
            if (ipv4 != NULL) CFRelease(ipv4);
            CFRelease(store);
            return 4;
        }
        CFRelease(ipv4);

        for (attempt = 0; attempt != 60; attempt++) {
            dns = SCDynamicStoreCopyValue(store, dnsKey);
            if (dns != NULL && CFGetTypeID(dns) == CFDictionaryGetTypeID()) {
                break;
            }
            if (dns != NULL) {
                CFRelease(dns);
                dns = NULL;
            }
            probe_spin_delay();
        }

        if (dns == NULL || CFGetTypeID(dns) != CFDictionaryGetTypeID()) {
            append_error("State:/Network/Global/DNS missing\n");
            write_marker("/var/run/sc_dynamic_store_network_probe.failed", "missing global DNS\n");
            if (dns != NULL) CFRelease(dns);
            CFRelease(store);
            return 5;
        }
        CFRelease(dns);
        write_marker("/var/run/sc_dynamic_store_network_probe.ok", "ok\n");
    }

    CFRelease(store);
    append_log("probe ok\n");
    write_marker("/var/run/sc_dynamic_store_probe.ok", "ok\n");
    return 0;
}
