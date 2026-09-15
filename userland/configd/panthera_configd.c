/*
 * Panthera configd phase 0.
 *
 * This is intentionally not Apple's full configd. It serves the real
 * SystemConfiguration SCDynamicStore MIG protocol with a process-local store,
 * proving dynamic-store IPC before plugin loading and IP configuration.
 */

#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SystemConfiguration.h>
#include <dispatch/dispatch.h>
#include <mach/mach.h>
#include <mach/mig.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/errno.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/ucontext.h>
#include <ifaddrs.h>
#include <sys/socket.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <time.h>

#include "configServer.h"
#include "config_types.h"
#include "panthera_route_manager.h"

#ifndef VM_FLAGS_ANYWHERE
#define VM_FLAGS_ANYWHERE TRUE
#endif

#define PANTHERA_CONFIGD_SERVICE "com.apple.SystemConfiguration.configd"
#define PANTHERA_CONFIGD_MAX_MSG (256 * 1024)
#define PANTHERA_SC_PREFERENCES_PATH "/Library/Preferences/SystemConfiguration/preferences.plist"
#define PANTHERA_IPCONFIGURATION_BUNDLE_PATH "/System/Library/SystemConfiguration/IPConfiguration.bundle"
#define PANTHERA_IPCONFIGURATION_EXECUTABLE_PATH "/usr/lib/system/libIPConfiguration.dylib"
#ifndef RTLD_NOW
#define RTLD_NOW 0x2
#endif
#ifndef RTLD_LOCAL
#define RTLD_LOCAL 0x4
#endif
#ifndef AF_LINK
#define AF_LINK 18
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 6
#endif
#ifndef IFT_ETHER
#define IFT_ETHER 0x6
#endif
#ifndef IFT_L2VLAN
#define IFT_L2VLAN 0x87
#endif
#ifndef IFT_IEEE8023ADLAG
#define IFT_IEEE8023ADLAG 0x88
#endif
#ifndef PANTHERA_IFF_LOOPBACK
#define PANTHERA_IFF_LOOPBACK ((unsigned int)0x8)
#endif
#ifndef PANTHERA_IFF_POINTOPOINT
#define PANTHERA_IFF_POINTOPOINT ((unsigned int)0x10)
#endif
#ifndef PANTHERA_IFF_NOARP
#define PANTHERA_IFF_NOARP ((unsigned int)0x80)
#endif
extern int *__error(void);
#ifndef errno
#define errno (*__error())
#endif
#ifndef EINTR
#define EINTR 4
#endif

extern void __CFInitialize(void);

extern mach_port_t bootstrap_port;
extern void *dlopen(const char *path, int mode);
extern void *dlsym(void *handle, const char *symbol);
extern char *dlerror(void);

typedef char panthera_bootstrap_name_t[128];

extern kern_return_t bootstrap_check_in(mach_port_t bp, const panthera_bootstrap_name_t service_name, mach_port_t *sp);
extern mach_msg_return_t mach_msg_server(boolean_t (*demux)(mach_msg_header_t *, mach_msg_header_t *),
                                         mach_msg_size_t max_size,
                                         mach_port_t rcv_name,
                                         mach_msg_options_t options);

extern Boolean _SCSerialize(CFPropertyListRef obj, CFDataRef *xml, const void **dataRef, CFIndex *dataLen);
extern Boolean _SCUnserialize(CFPropertyListRef *obj, CFDataRef xml, const void *dataRef, CFIndex dataLen);
extern Boolean _SCSerializeString(CFStringRef str, CFDataRef *data, const void **dataRef, CFIndex *dataLen);
extern Boolean _SCUnserializeString(CFStringRef *str, CFDataRef utf8, const void *dataRef, CFIndex dataLen);
extern CFDictionaryRef _SCSerializeMultiple(CFDictionaryRef dict);
extern CFDictionaryRef _SCUnserializeMultiple(CFDictionaryRef dict);
extern int panthera_network_state_publish_dictionary(CFMutableDictionaryRef store);

static CFMutableDictionaryRef g_store;
static pthread_mutex_t g_store_mutex;
static int g_instance;
static int g_network_state_published;
static int g_setup_preferences_loaded;
static int g_launchd_network_up_notified;
static mach_port_t g_configd_port_set = MACH_PORT_NULL;

typedef struct notify_session {
    mach_port_t port;
    CFMutableArrayRef keys;
    CFMutableArrayRef patterns;
    CFMutableArrayRef changes;
    CFMutableArrayRef ports;
    struct notify_session *next;
} notify_session_t;

static pthread_mutex_t g_notify_mutex;
static notify_session_t *g_notify_sessions;

static void write_console(const char *message);
static void write_stderr(const char *message);
static void trace_console(const char *message);
static void trace_stderr(const char *message);
static void notify_launchd_network_up(void);
static void notify_launchd_network_up_now(void *context);
static void publish_global_network_state_from_pending_set(CFDictionaryRef set);
static void notify_store_key_changed(CFStringRef key);
static void notify_store_keys_changed(CFArrayRef keys);

static void
store_lock(void)
{
    (void)pthread_mutex_lock(&g_store_mutex);
}

static void
store_unlock(void)
{
    (void)pthread_mutex_unlock(&g_store_mutex);
}

static CFPropertyListRef
copy_store_value(CFPropertyListRef value)
{
    if (value == NULL) {
        return NULL;
    }
    CFRetain(value);
    return value;
}

static void
set_owned_value_for_key_locked(CFStringRef key, CFPropertyListRef ownedValue)
{
    CFDictionarySetValue(g_store, key, ownedValue);
    g_instance++;
}

static void
remove_value_for_key_locked(CFStringRef key)
{
    CFDictionaryRemoveValue(g_store, key);
    g_instance++;
}

static void
write_console(const char *message)
{
    (void)write(STDERR_FILENO, message, strlen(message));
}

static void
write_stderr(const char *message)
{
    (void)write(STDERR_FILENO, message, strlen(message));
}

static int
trace_enabled(void)
{
    const char *value = getenv("PANTHERA_CONFIGD_TRACE");
    return (value != NULL && value[0] != '\0' && strcmp(value, "0") != 0);
}

static void
trace_console(const char *message)
{
    if (trace_enabled()) {
        write_console(message);
    }
}

static void
trace_stderr(const char *message)
{
    if (trace_enabled()) {
        write_stderr(message);
    }
}

static void
configd_log(const char *fmt, ...)
{
    va_list ap;
    FILE *log;
    int trace = trace_enabled();

    if (trace) {
        va_start(ap, fmt);
        fputs("PANTHERA:configd ", stderr);
        vfprintf(stderr, fmt, ap);
        fputc('\n', stderr);
        va_end(ap);
    }

    if (getenv("PANTHERA_CONFIGD_FILE_LOG") != NULL) {
        log = fopen("/var/log/configd.log", "a");
        if (log != NULL) {
            va_start(ap, fmt);
            fputs("PANTHERA:configd ", log);
            vfprintf(log, fmt, ap);
            fputc('\n', log);
            va_end(ap);
            fclose(log);
        }
    }
}

static void
start_network_state_publisher(void)
{
    pid_t pid;

    if (getenv("PANTHERA_CONFIGD_NETWORK_STATE") == NULL) {
        return;
    }

    pid = fork();
    if (pid < 0) {
        configd_log("network-state publisher fork failed");
        return;
    }

    if (pid == 0) {
        const char *validate = getenv("PANTHERA_CONFIGD_NETWORK_STATE_VALIDATE");

        usleep(250 * 1000);
        if (validate != NULL) {
            setenv("PANTHERA_SC_NETWORK_VALIDATE", validate, 1);
        }
        configd_log("network-state publisher exec");
        execl("/usr/sbin/sc_network_state_publisher",
              "sc_network_state_publisher",
              (char *)NULL);
        write_console("PANTHERA:configd network-state publisher exec failed\n");
        _exit(127);
    }

    configd_log("network-state publisher pid=%d", pid);
}

static CFBundleRef
copy_ipconfiguration_bundle(void)
{
    CFURLRef url;
    CFBundleRef bundle = NULL;
    const UInt8 *path = (const UInt8 *)PANTHERA_IPCONFIGURATION_BUNDLE_PATH;

    url = CFURLCreateFromFileSystemRepresentation(NULL, path,
                                                  (CFIndex)strlen((const char *)path),
                                                  TRUE);
    if (url == NULL) {
        write_console("PANTHERA:configd IPConfiguration bundle url create failed\n");
        write_stderr("PANTHERA:configd IPConfiguration bundle url create failed\n");
        return NULL;
    }
    bundle = CFBundleCreate(NULL, url);
    CFRelease(url);
    if (bundle == NULL) {
        write_console("PANTHERA:configd IPConfiguration bundle create returned null\n");
        write_stderr("PANTHERA:configd IPConfiguration bundle create returned null\n");
    }
    return bundle;
}

static boolean_t
panthera_config_server(mach_msg_header_t *InHeadP, mach_msg_header_t *OutHeadP)
{
    return config_server(InHeadP, OutHeadP);
}

static void *
configd_server_thread(void *arg)
{
    mach_port_t server = (mach_port_t)(uintptr_t)arg;
    kern_return_t kr;

    configd_log("server thread running port_set=%u", server);

    kr = mach_msg_server(panthera_config_server,
                         PANTHERA_CONFIGD_MAX_MSG,
                         server,
                         MACH_RCV_TRAILER_TYPE(MACH_MSG_TRAILER_FORMAT_0) |
                         MACH_RCV_TRAILER_ELEMENTS(MACH_RCV_TRAILER_AUDIT));
    configd_log("server thread mach_msg_server returned kr=%d", kr);
    return NULL;
}

static int
start_configd_server_thread(mach_port_t server)
{
    pthread_t thread;
    pthread_attr_t attr;
    int err;

    err = pthread_attr_init(&attr);
    if (err != 0) {
        configd_log("server thread attr init failed err=%d", err);
        write_stderr("PANTHERA:configd server thread attr init failed\n");
        return 0;
    }

    err = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (err != 0) {
        configd_log("server thread attr setdetachstate failed err=%d", err);
        write_stderr("PANTHERA:configd server thread attr setdetachstate failed\n");
        (void)pthread_attr_destroy(&attr);
        return 0;
    }

    err = pthread_create(&thread, &attr, configd_server_thread, (void *)(uintptr_t)server);
    (void)pthread_attr_destroy(&attr);
    if (err != 0) {
        configd_log("server thread create failed err=%d", err);
        write_stderr("PANTHERA:configd server thread create failed\n");
        return 0;
    }

    configd_log("server thread active port=%u", server);
    return 1;
}
static int
is_dhcp_ethernet_interface(const struct ifaddrs *ifa)
{
    const struct sockaddr_dl *sdl;

    if (ifa == NULL || ifa->ifa_name == NULL || ifa->ifa_name[0] == '\0') {
        return 0;
    }
    if ((ifa->ifa_flags & (PANTHERA_IFF_LOOPBACK | PANTHERA_IFF_POINTOPOINT | PANTHERA_IFF_NOARP)) != 0) {
        return 0;
    }
    if (strcmp(ifa->ifa_name, "lo0") == 0) {
        return 0;
    }
    if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_LINK) {
        return 0;
    }

    sdl = (const struct sockaddr_dl *)(const void *)ifa->ifa_addr;
    if (sdl->sdl_index == 0) {
        return 0;
    }
    if (sdl->sdl_type != IFT_ETHER &&
        sdl->sdl_type != IFT_L2VLAN &&
        sdl->sdl_type != IFT_IEEE8023ADLAG) {
        return 0;
    }
    if (sdl->sdl_alen != 6) {
        return 0;
    }

    return 1;
}

static void
wait_for_ipconfiguration_interface_readiness(void)
{
    const uint64_t deadline_ns = 30ULL * 1000000000ULL;
    const uint64_t step_ns = 10ULL * 1000000ULL;
    const uint64_t max_delay_ns = 250ULL * 1000000ULL;
    uint64_t delay_ns = 10ULL * 1000000ULL;
    uint64_t accumulated_fallback_ns = 0;
    struct timespec start_ts;
    int has_monotonic = 0;
    int attempt = 0;

    trace_console("PANTHERA:IPConfiguration wait for interface enter\n");
    trace_stderr("PANTHERA:IPConfiguration wait for interface enter\n");
    configd_log("IPConfiguration wait for interface enter");

    if (clock_gettime(CLOCK_MONOTONIC, &start_ts) == 0) {
        has_monotonic = 1;
    }

    for (;;) {
        char attempt_buf[80];
        struct ifaddrs *ifap = NULL;
        struct ifaddrs *ifa;
        char ready_ifname[32] = {0};
        int ready = 0;
        uint64_t remaining_ns = 0;
        uint64_t sleep_ns = 0;

        attempt++;
        snprintf(attempt_buf, sizeof(attempt_buf),
                 "PANTHERA:IPConfiguration wait for interface attempt %d\n", attempt);
        trace_console(attempt_buf);
        trace_stderr(attempt_buf);

        if (getifaddrs(&ifap) == 0 && ifap != NULL) {
            for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
                if (is_dhcp_ethernet_interface(ifa)) {
                    (void)snprintf(ready_ifname, sizeof(ready_ifname), "%s", ifa->ifa_name);
                    ready = 1;
                    break;
                }
            }
            freeifaddrs(ifap);
        }

        if (ready) {
            char ready_buf[128];
            snprintf(ready_buf, sizeof(ready_buf),
                     "PANTHERA:IPConfiguration interface ready: %s\n", ready_ifname);
            trace_console(ready_buf);
            trace_stderr(ready_buf);
            configd_log("IPConfiguration interface ready: %s", ready_ifname);
            return;
        }

        if (has_monotonic) {
            struct timespec now_ts;
            uint64_t elapsed_ns;

            if (clock_gettime(CLOCK_MONOTONIC, &now_ts) != 0) {
                break;
            }

            if (now_ts.tv_sec < start_ts.tv_sec ||
                (now_ts.tv_sec == start_ts.tv_sec && now_ts.tv_nsec <= start_ts.tv_nsec)) {
                elapsed_ns = 0;
            } else {
                elapsed_ns = (uint64_t)(now_ts.tv_sec - start_ts.tv_sec) * 1000000000ULL +
                             (uint64_t)(now_ts.tv_nsec - start_ts.tv_nsec);
            }

            if (elapsed_ns >= deadline_ns) {
                break;
            }
            remaining_ns = deadline_ns - elapsed_ns;
        } else {
            if (accumulated_fallback_ns >= deadline_ns) {
                break;
            }
            remaining_ns = deadline_ns - accumulated_fallback_ns;
        }

        sleep_ns = (delay_ns < remaining_ns) ? delay_ns : remaining_ns;
        if (sleep_ns > 0) {
            struct timespec req, rem;
            req.tv_sec = (time_t)(sleep_ns / 1000000000ULL);
            req.tv_nsec = (long)(sleep_ns % 1000000000ULL);
            while (nanosleep(&req, &rem) != 0) {
                if (errno == EINTR) {
                    req = rem;
                } else {
                    break;
                }
            }
            if (!has_monotonic) {
                accumulated_fallback_ns += sleep_ns;
            }
        }

        if (delay_ns < max_delay_ns) {
            delay_ns += step_ns;
            if (delay_ns > max_delay_ns) {
                delay_ns = max_delay_ns;
            }
        }
    }

    write_console("PANTHERA:IPConfiguration wait for interface timeout\n");
    write_stderr("PANTHERA:IPConfiguration wait for interface timeout\n");
    configd_log("IPConfiguration wait for interface timeout");
}

static int
start_ipconfiguration_plugin(mach_port_t server)
{
    CFBundleRef bundle;
    void *plugin_handle;
    SCDynamicStoreRef store;
    void (*plugin_load)(CFBundleRef, Boolean);
    void (*plugin_start)(const char *, const char *);
    void (*plugin_prime)(void);

    trace_console("PANTHERA:configd IPConfiguration plugin start\n");
    trace_stderr("PANTHERA:configd IPConfiguration plugin start\n");

    bundle = copy_ipconfiguration_bundle();
    if (bundle == NULL) {
        configd_log("IPConfiguration bundle create failed path=%s",
                    PANTHERA_IPCONFIGURATION_BUNDLE_PATH);
        write_console("PANTHERA:configd IPConfiguration bundle create failed\n");
        write_stderr("PANTHERA:configd IPConfiguration bundle create failed\n");
        return 6;
    }
    trace_console("PANTHERA:configd IPConfiguration bundle create ok\n");
    trace_stderr("PANTHERA:configd IPConfiguration bundle create ok\n");

    if (!start_configd_server_thread(server)) {
        write_stderr("PANTHERA:configd server thread start failed\n");
        CFRelease(bundle);
        return 4;
    }

    store = SCDynamicStoreCreate(NULL, CFSTR("Panthera configd IPConfiguration preflight"), NULL, NULL);
    if (store != NULL) {
        CFRelease(store);
        trace_console("PANTHERA:IPConfiguration configd ready\n");
        trace_stderr("PANTHERA:IPConfiguration configd ready\n");
    } else {
        write_console("PANTHERA:IPConfiguration configd unavailable on plugin start\n");
        write_stderr("PANTHERA:IPConfiguration configd unavailable on plugin start\n");
    }

    plugin_handle = dlopen(PANTHERA_IPCONFIGURATION_EXECUTABLE_PATH, RTLD_NOW | RTLD_LOCAL);
    if (plugin_handle == NULL) {
        const char *loader_error = dlerror();
        write_console("PANTHERA:configd IPConfiguration dlopen failed\n");
        write_stderr("PANTHERA:configd IPConfiguration dlopen failed\n");
        if (loader_error != NULL && loader_error[0] != '\0') {
            write_stderr("PANTHERA:configd IPConfiguration dlerror=");
            write_stderr(loader_error);
            write_stderr("\n");
        }
        CFRelease(bundle);
        return 7;
    }

    plugin_load = (void (*)(CFBundleRef, Boolean))dlsym(plugin_handle, "load");
    plugin_start = (void (*)(const char *, const char *))dlsym(plugin_handle, "start");
    plugin_prime = (void (*)(void))dlsym(plugin_handle, "prime");
    if (plugin_load == NULL || plugin_start == NULL || plugin_prime == NULL) {
        write_console("PANTHERA:configd IPConfiguration symbols missing\n");
        write_stderr("PANTHERA:configd IPConfiguration symbols missing\n");
        CFRelease(bundle);
        return 8;
    }

    plugin_load(bundle, TRUE);
    trace_console("PANTHERA:IPConfiguration load returned\n");
    trace_stderr("PANTHERA:IPConfiguration load returned\n");
    plugin_start("IPConfiguration", "/System/Library/SystemConfiguration/IPConfiguration.bundle");
    trace_console("PANTHERA:IPConfiguration start returned\n");
    trace_stderr("PANTHERA:IPConfiguration start returned\n");
    wait_for_ipconfiguration_interface_readiness();
    plugin_prime();
    CFRelease(bundle);
    trace_console("PANTHERA:IPConfiguration prime queued\n");
    trace_stderr("PANTHERA:IPConfiguration prime queued\n");
    CFRunLoopRun();
    return 5;
}

static CFDictionaryRef
dictionary_value(CFDictionaryRef dict, CFStringRef key)
{
    CFTypeRef value;

    if (dict == NULL) {
        return NULL;
    }

    value = CFDictionaryGetValue(dict, key);
    if (value == NULL || CFGetTypeID(value) != CFDictionaryGetTypeID()) {
        return NULL;
    }

    return (CFDictionaryRef)value;
}

static CFPropertyListRef
copy_plist_file(const char *path)
{
    struct stat st;
    UInt8 *bytes = NULL;
    CFDataRef data = NULL;
    CFPropertyListRef plist = NULL;
    size_t len;
    size_t offset = 0;
    int fd;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return NULL;
    }

    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        close(fd);
        return NULL;
    }

    len = (size_t)st.st_size;
    bytes = malloc(len);
    if (bytes == NULL) {
        close(fd);
        return NULL;
    }

    while (offset < len) {
        ssize_t n = read(fd, bytes + offset, len - offset);
        if (n <= 0) {
            free(bytes);
            close(fd);
            return NULL;
        }
        offset += (size_t)n;
    }
    close(fd);

    data = CFDataCreate(NULL, bytes, (CFIndex)len);
    free(bytes);
    if (data == NULL) {
        return NULL;
    }

    plist = CFPropertyListCreateWithData(NULL, data, kCFPropertyListImmutable, NULL, NULL);
    CFRelease(data);
    return plist;
}

static CFStringRef
copy_current_set_identifier(CFDictionaryRef preferences)
{
    CFStringRef current;
    CFStringRef prefix = CFSTR("/Sets/");
    CFIndex prefixLength;

    current = (CFStringRef)CFDictionaryGetValue(preferences, CFSTR("CurrentSet"));
    if (current == NULL || CFGetTypeID(current) != CFStringGetTypeID()) {
        return NULL;
    }

    prefixLength = CFStringGetLength(prefix);
    if (CFStringHasPrefix(current, prefix) &&
        CFStringGetLength(current) > prefixLength) {
        return CFStringCreateWithSubstring(NULL, current,
                                           CFRangeMake(prefixLength,
                                                       CFStringGetLength(current) - prefixLength));
    }

    CFRetain(current);
    return current;
}

static CFDictionaryRef
copy_preferences_global_ipv4(CFDictionaryRef preferences)
{
    CFDictionaryRef sets;
    CFDictionaryRef set;
    CFDictionaryRef network;
    CFDictionaryRef global;
    CFDictionaryRef ipv4;
    CFStringRef setID;

    setID = copy_current_set_identifier(preferences);
    if (setID == NULL) {
        return NULL;
    }

    sets = dictionary_value(preferences, CFSTR("Sets"));
    set = dictionary_value(sets, setID);
    network = dictionary_value(set, CFSTR("Network"));
    global = dictionary_value(network, CFSTR("Global"));
    ipv4 = dictionary_value(global, CFSTR("IPv4"));
    CFRelease(setID);

    if (ipv4 != NULL) {
        CFRetain(ipv4);
    }
    return ipv4;
}

typedef struct {
    CFMutableDictionaryRef store;
    CFMutableArrayRef serviceOrder;
    int published;
} SetupPreferencesContext;

static int
publish_setup_service_from_preferences(CFMutableDictionaryRef store,
                                       CFStringRef service,
                                       CFDictionaryRef serviceDict)
{
    CFDictionaryRef interfaceDict;
    CFDictionaryRef ipv4Dict;
    CFStringRef key = NULL;
    int ok = 0;

    interfaceDict = dictionary_value(serviceDict, CFSTR("Interface"));
    ipv4Dict = dictionary_value(serviceDict, CFSTR("IPv4"));
    if (interfaceDict == NULL || ipv4Dict == NULL) {
        return 0;
    }

    key = SCDynamicStoreKeyCreateNetworkServiceEntity(NULL, kSCDynamicStoreDomainSetup,
                                                      service, kSCEntNetInterface);
    if (key == NULL) {
        goto done;
    }
    CFDictionarySetValue(store, key, interfaceDict);
    CFRelease(key);
    key = NULL;

    key = SCDynamicStoreKeyCreateNetworkServiceEntity(NULL, kSCDynamicStoreDomainSetup,
                                                      service, kSCEntNetIPv4);
    if (key == NULL) {
        goto done;
    }
    CFDictionarySetValue(store, key, ipv4Dict);
    ok = 1;

done:
    if (key != NULL) {
        CFRelease(key);
    }
    return ok;
}

static void
publish_setup_service_apply(const void *key, const void *value, void *context)
{
    SetupPreferencesContext *ctx = (SetupPreferencesContext *)context;

    if (CFGetTypeID(key) != CFStringGetTypeID() ||
        CFGetTypeID(value) != CFDictionaryGetTypeID()) {
        return;
    }

    if (publish_setup_service_from_preferences(ctx->store,
                                               (CFStringRef)key,
                                               (CFDictionaryRef)value)) {
        CFArrayAppendValue(ctx->serviceOrder, key);
        ctx->published++;
    }
}

static int
publish_setup_global_ipv4(CFMutableDictionaryRef store,
                          CFDictionaryRef preferences,
                          CFArrayRef fallbackServiceOrder)
{
    CFDictionaryRef preferencesGlobalIPv4 = NULL;
    CFMutableDictionaryRef generatedGlobalIPv4 = NULL;
    CFStringRef key = NULL;
    int ok = 0;

    preferencesGlobalIPv4 = copy_preferences_global_ipv4(preferences);
    if (preferencesGlobalIPv4 == NULL) {
        generatedGlobalIPv4 = CFDictionaryCreateMutable(NULL, 0,
                                                        &kCFTypeDictionaryKeyCallBacks,
                                                        &kCFTypeDictionaryValueCallBacks);
        if (generatedGlobalIPv4 == NULL) {
            goto done;
        }
        CFDictionarySetValue(generatedGlobalIPv4,
                             kSCPropNetServiceOrder,
                             fallbackServiceOrder);
        preferencesGlobalIPv4 = generatedGlobalIPv4;
    }

    key = SCDynamicStoreKeyCreateNetworkGlobalEntity(NULL,
                                                     kSCDynamicStoreDomainSetup,
                                                     kSCEntNetIPv4);
    if (key == NULL) {
        goto done;
    }

    CFDictionarySetValue(store, key, preferencesGlobalIPv4);
    ok = 1;

done:
    if (key != NULL) {
        CFRelease(key);
    }
    if (generatedGlobalIPv4 != NULL) {
        CFRelease(generatedGlobalIPv4);
    } else if (preferencesGlobalIPv4 != NULL) {
        CFRelease(preferencesGlobalIPv4);
    }
    return ok;
}

static void
load_setup_preferences(void)
{
    CFPropertyListRef plist;
    CFDictionaryRef preferences;
    CFDictionaryRef services;
    SetupPreferencesContext ctx;

    plist = copy_plist_file(PANTHERA_SC_PREFERENCES_PATH);
    if (plist == NULL) {
        configd_log("setup preferences absent path=%s", PANTHERA_SC_PREFERENCES_PATH);
        return;
    }

    if (CFGetTypeID(plist) != CFDictionaryGetTypeID()) {
        configd_log("setup preferences invalid top-level type");
        CFRelease(plist);
        return;
    }

    preferences = (CFDictionaryRef)plist;
    services = dictionary_value(preferences, CFSTR("NetworkServices"));
    if (services == NULL) {
        configd_log("setup preferences missing NetworkServices");
        CFRelease(plist);
        return;
    }

    ctx.store = g_store;
    ctx.serviceOrder = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    ctx.published = 0;
    if (ctx.serviceOrder == NULL) {
        CFRelease(plist);
        return;
    }

    CFDictionaryApplyFunction(services, publish_setup_service_apply, &ctx);
    if (ctx.published > 0 &&
        publish_setup_global_ipv4(g_store, preferences, ctx.serviceOrder)) {
        g_setup_preferences_loaded = 1;
        g_instance++;
        configd_log("setup preferences loaded services=%d", ctx.published);
    } else {
        configd_log("setup preferences loaded no services");
    }

    CFRelease(ctx.serviceOrder);
    CFRelease(plist);
}

static void
publish_initial_network_state(void)
{
    int published = 0;

    if (getenv("PANTHERA_CONFIGD_NETWORK_STATE_INTERNAL") == NULL) {
        return;
    }

    store_lock();
    if (panthera_network_state_publish_dictionary(g_store)) {
        g_instance++;
        g_network_state_published = 1;
        published = 1;
    } else {
        configd_log("network-state internal no interface");
    }
    store_unlock();

    if (published) {
        configd_log("network-state internal published");
        notify_launchd_network_up();
    }
}

static void
notify_launchd_network_up_now(void *context)
{
    (void)context;

    if (kill(1, SIGUSR1) == 0) {
        configd_log("launchd NetworkState signal sent");
    } else {
        configd_log("launchd NetworkState signal failed");
    }
}

static void
notify_launchd_network_up(void)
{
    if (g_launchd_network_up_notified) {
        return;
    }

    g_launchd_network_up_notified = 1;
    notify_launchd_network_up_now(NULL);
}

static Boolean
dictionary_bool_is_true(CFDictionaryRef dict, CFStringRef key)
{
    CFTypeRef value;

    if (dict == NULL || key == NULL ||
        CFGetTypeID(dict) != CFDictionaryGetTypeID()) {
        return FALSE;
    }

    value = CFDictionaryGetValue(dict, key);
    return value != NULL &&
        CFGetTypeID(value) == CFBooleanGetTypeID() &&
        CFBooleanGetValue((CFBooleanRef)value);
}

static Boolean
pending_link_state_is_active(CFStringRef key, CFTypeRef value)
{
    if (key == NULL || value == NULL ||
        CFGetTypeID(key) != CFStringGetTypeID() ||
        CFGetTypeID(value) != CFDictionaryGetTypeID()) {
        return FALSE;
    }

    if (!CFStringHasPrefix(key, CFSTR("State:/Network/Interface/")) ||
        !CFStringHasSuffix(key, CFSTR("/Link"))) {
        return FALSE;
    }

    return dictionary_bool_is_true((CFDictionaryRef)value, kSCPropNetLinkActive) ||
        dictionary_bool_is_true((CFDictionaryRef)value, CFSTR("LinkStatusActive"));
}

static void
refresh_network_state_if_needed(void)
{
    int published = 0;

    if (getenv("PANTHERA_CONFIGD_NETWORK_STATE_INTERNAL") == NULL ||
        g_network_state_published) {
        return;
    }

    store_lock();
    if (panthera_network_state_publish_dictionary(g_store)) {
        g_instance++;
        g_network_state_published = 1;
        published = 1;
    }
    store_unlock();

    if (published) {
        configd_log("network-state lazy published");
        notify_launchd_network_up();
    }
}

static int
copy_out_bytes(const void *src, CFIndex len, xmlDataOut_t *out, mach_msg_type_number_t *outCnt)
{
    vm_address_t dst = 0;

    if (out == NULL || outCnt == NULL) {
        return kSCStatusInvalidArgument;
    }

    *out = NULL;
    *outCnt = 0;

    if (src == NULL || len < 0) {
        return kSCStatusInvalidArgument;
    }

    if (len == 0) {
        return kSCStatusOK;
    }

    if (vm_allocate((vm_map_t)mach_task_self(), &dst, (vm_size_t)len, VM_FLAGS_ANYWHERE) != KERN_SUCCESS) {
        return kSCStatusFailed;
    }

    memcpy((void *)dst, src, (size_t)len);
    *out = (xmlDataOut_t)dst;
    *outCnt = (mach_msg_type_number_t)len;
    return kSCStatusOK;
}

static int
serialize_out(CFPropertyListRef obj, xmlDataOut_t *out, mach_msg_type_number_t *outCnt)
{
    CFDataRef data = NULL;
    const void *bytes = NULL;
    CFIndex len = 0;
    int status;

    if (obj == NULL) {
        return kSCStatusInvalidArgument;
    }

    if (!_SCSerialize(obj, &data, &bytes, &len)) {
        return kSCStatusFailed;
    }

    status = copy_out_bytes(bytes, len, out, outCnt);
    CFRelease(data);
    return status;
}

static CFStringRef
copy_in_string(xmlData_t data, mach_msg_type_number_t dataCnt)
{
    CFDataRef bytes;
    CFStringRef string = NULL;

    if (data == NULL || dataCnt == 0) {
        return NULL;
    }

    bytes = CFDataCreateWithBytesNoCopy(NULL, data, dataCnt, kCFAllocatorNull);
    if (bytes == NULL) {
        return NULL;
    }

    string = CFStringCreateFromExternalRepresentation(NULL, bytes, kCFStringEncodingUTF8);
    CFRelease(bytes);
    return string;
}

static CFPropertyListRef
copy_in_plist(xmlData_t data, mach_msg_type_number_t dataCnt)
{
    CFDataRef bytes;
    CFErrorRef error = NULL;
    CFPropertyListRef obj = NULL;

    if (data == NULL || dataCnt == 0) {
        return NULL;
    }

    bytes = CFDataCreateWithBytesNoCopy(NULL, data, dataCnt, kCFAllocatorNull);
    if (bytes == NULL) {
        return NULL;
    }

    obj = CFPropertyListCreateWithData(NULL, bytes, kCFPropertyListImmutable, NULL, &error);
    if (error != NULL) {
        CFRelease(error);
    }
    CFRelease(bytes);
    return obj;
}

static Boolean
string_matches_exact_or_prefix(CFStringRef key, CFStringRef pattern)
{
    CFIndex patternLen;

    if (pattern == NULL || CFStringGetLength(pattern) == 0) {
        return TRUE;
    }

    if (CFStringCompare(key, pattern, 0) == kCFCompareEqualTo) {
        return TRUE;
    }

    patternLen = CFStringGetLength(pattern);
    if (patternLen > 0 && CFStringHasSuffix(pattern, CFSTR("/"))) {
        return CFStringHasPrefix(key, pattern);
    }

    return FALSE;
}

static Boolean
match_component_regex_cstr(const char *key, const char *pattern)
{
    while (*pattern != '\0') {
        if (strncmp(pattern, "[^/]+", 5) == 0) {
            if (*key == '\0' || *key == '/') {
                return FALSE;
            }
            while (*key != '\0' && *key != '/') {
                key++;
            }
            pattern += 5;
            continue;
        }

        if (*key != *pattern) {
            return FALSE;
        }
        key++;
        pattern++;
    }

    return *key == '\0';
}

static Boolean
string_matches_store_pattern(CFStringRef key, CFStringRef pattern)
{
    CFRange regexToken;
    char keyBuf[512];
    char patternBuf[512];

    if (string_matches_exact_or_prefix(key, pattern)) {
        return TRUE;
    }

    regexToken = CFStringFind(pattern, CFSTR("[^/]+"), 0);
    if (regexToken.location == kCFNotFound) {
        return FALSE;
    }

    if (!CFStringGetCString(key, keyBuf, sizeof(keyBuf), kCFStringEncodingUTF8) ||
        !CFStringGetCString(pattern, patternBuf, sizeof(patternBuf), kCFStringEncodingUTF8)) {
        return FALSE;
    }

    return match_component_regex_cstr(keyBuf, patternBuf);
}

static Boolean
array_contains_string(CFArrayRef array, CFStringRef key)
{
    if (array == NULL) {
        return FALSE;
    }

    return CFArrayContainsValue(array, CFRangeMake(0, CFArrayGetCount(array)), key);
}

static CFMutableArrayRef
copy_mutable_string_array(CFArrayRef array)
{
    CFMutableArrayRef copy;

    copy = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    if (copy == NULL) {
        return NULL;
    }

    if (array != NULL && CFGetTypeID(array) == CFArrayGetTypeID()) {
        for (CFIndex i = 0; i < CFArrayGetCount(array); i++) {
            CFTypeRef value = CFArrayGetValueAtIndex(array, i);
            if (value != NULL && CFGetTypeID(value) == CFStringGetTypeID()) {
                CFArrayAppendValue(copy, value);
            }
        }
    }

    return copy;
}

static notify_session_t *
notify_session_find_locked(mach_port_t port)
{
    notify_session_t *session = g_notify_sessions;

    while (session != NULL) {
        if (session->port == port) {
            return session;
        }
        session = session->next;
    }

    return NULL;
}

static notify_session_t *
notify_session_create_locked(mach_port_t port)
{
    notify_session_t *session;
    session = calloc(1, sizeof(*session));
    if (session == NULL) {
        write_stderr("PANTHERA:configd notify session calloc failed\n");
        return NULL;
    }

    session->port = port;
    session->keys = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    session->patterns = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    session->changes = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    session->ports = CFArrayCreateMutable(NULL, 0, NULL);
    if (session->keys == NULL || session->patterns == NULL ||
        session->changes == NULL || session->ports == NULL) {
        write_stderr("PANTHERA:configd notify session array create failed\n");
        if (session->keys != NULL) CFRelease(session->keys);
        if (session->patterns != NULL) CFRelease(session->patterns);
        if (session->changes != NULL) CFRelease(session->changes);
        if (session->ports != NULL) CFRelease(session->ports);
        free(session);
        return NULL;
    }

    session->next = g_notify_sessions;
    g_notify_sessions = session;
    return session;

}

static notify_session_t *
notify_session_get_locked(mach_port_t port, int create)
{
    notify_session_t *session = notify_session_find_locked(port);

    if (session == NULL && create) {
        session = notify_session_create_locked(port);
    }

    return session;
}

static Boolean
notify_session_matches_key_locked(notify_session_t *session, CFStringRef key)
{
    if (array_contains_string(session->keys, key)) {
        return TRUE;
    }

    for (CFIndex i = 0; i < CFArrayGetCount(session->patterns); i++) {
        CFStringRef pattern = (CFStringRef)CFArrayGetValueAtIndex(session->patterns, i);
        if (pattern != NULL && CFGetTypeID(pattern) == CFStringGetTypeID() &&
            string_matches_store_pattern(key, pattern)) {
            return TRUE;
        }
    }

    return FALSE;
}

static void
notify_send_port(mach_port_t port)
{
    mach_msg_header_t msg;

    if (port == MACH_PORT_NULL) {
        return;
    }

    memset(&msg, 0, sizeof(msg));
    msg.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
    msg.msgh_size = sizeof(msg);
    msg.msgh_remote_port = port;
    msg.msgh_local_port = MACH_PORT_NULL;
    msg.msgh_id = 0;

    (void)mach_msg(&msg,
                   MACH_SEND_MSG | MACH_SEND_TIMEOUT,
                   msg.msgh_size,
                   0,
                   MACH_PORT_NULL,
                   0,
                   MACH_PORT_NULL);
}

static void
notify_session_signal_locked(notify_session_t *session)
{
    for (CFIndex i = 0; i < CFArrayGetCount(session->ports); i++) {
        mach_port_t port = (mach_port_t)(uintptr_t)CFArrayGetValueAtIndex(session->ports, i);
        notify_send_port(port);
    }
}

static void
notify_store_key_changed(CFStringRef key)
{
    if (key == NULL || CFGetTypeID(key) != CFStringGetTypeID()) {
        return;
    }

    (void)pthread_mutex_lock(&g_notify_mutex);
    for (notify_session_t *session = g_notify_sessions; session != NULL; session = session->next) {
        if (!notify_session_matches_key_locked(session, key)) {
            continue;
        }
        if (!array_contains_string(session->changes, key)) {
            CFArrayAppendValue(session->changes, key);
        }
        notify_session_signal_locked(session);
    }
    (void)pthread_mutex_unlock(&g_notify_mutex);
}

static void
notify_store_keys_changed(CFArrayRef keys)
{
    if (keys == NULL || CFGetTypeID(keys) != CFArrayGetTypeID()) {
        return;
    }

    for (CFIndex i = 0; i < CFArrayGetCount(keys); i++) {
        CFStringRef key = (CFStringRef)CFArrayGetValueAtIndex(keys, i);
        notify_store_key_changed(key);
    }
}

static void
log_key_operation(const char *op, CFStringRef key, int status)
{
    char buf[256];

    if (key == NULL) {
        configd_log("%s key=<null> status=%d", op, status);
        return;
    }

    if (CFStringCompare(key, CFSTR("State:/Panthera/Probe"), 0) == kCFCompareEqualTo ||
        CFStringCompare(key, CFSTR("Plugin:IPConfiguration"), 0) == kCFCompareEqualTo) {
        return;
    }

    if (CFStringGetCString(key, buf, sizeof(buf), kCFStringEncodingUTF8)) {
        configd_log("%s key=%s status=%d", op, buf, status);
    } else {
        configd_log("%s key=<non-utf8> status=%d", op, status);
    }
}

static int
dynamic_store_self_test(void)
{
    SCDynamicStoreRef store;
    CFStringRef key = CFSTR("State:/Panthera/ConfigdSelfTest");
    CFStringRef value = CFSTR("ok");
    CFPropertyListRef copied;

    configd_log("self-test begin");
    store = SCDynamicStoreCreate(NULL, CFSTR("panthera-configd-self-test"), NULL, NULL);
    if (store == NULL) {
        configd_log("self-test SCDynamicStoreCreate failed");
        return 1;
    }

    if (!SCDynamicStoreSetValue(store, key, value)) {
        configd_log("self-test SCDynamicStoreSetValue failed sc=%d %s",
                    SCError(), SCErrorString(SCError()));
        CFRelease(store);
        return 2;
    }

    copied = SCDynamicStoreCopyValue(store, key);
    if (copied == NULL || CFGetTypeID(copied) != CFStringGetTypeID() ||
        CFStringCompare((CFStringRef)copied, value, 0) != kCFCompareEqualTo) {
        configd_log("self-test SCDynamicStoreCopyValue mismatch sc=%d %s",
                    SCError(), SCErrorString(SCError()));
        if (copied != NULL) {
            CFRelease(copied);
        }
        CFRelease(store);
        return 3;
    }

    CFRelease(copied);
    CFRelease(store);
    configd_log("self-test ok");
    return 0;
}

static void
start_dynamic_store_self_test(void)
{
    pid_t pid;

    configd_log("self-test fork begin");
    pid = fork();
    if (pid < 0) {
        configd_log("self-test fork failed");
        return;
    }

    if (pid == 0) {
        int rc;

        usleep(250 * 1000);
        rc = dynamic_store_self_test();
        _exit(rc);
    }

    configd_log("self-test forked pid=%d", pid);
}

static void
start_kernel_event_monitor_child(void)
{
    pid_t pid;

    if (getenv("PANTHERA_CONFIGD_KERNEL_EVENT_MONITOR") == NULL) {
        return;
    }

    pid = fork();
    if (pid < 0) {
        configd_log("KernelEventMonitor fork failed");
        return;
    }

    if (pid == 0) {
        /*
         * Keep configd's MIG server on the original main thread.  Use a clean
         * exec'd helper instead of running SystemConfiguration client code in a
         * fork-only child, which inherits partially initialized CF/dispatch
         * state from configd.
         */
        execl("/usr/libexec/kernel_event_monitor",
              "kernel_event_monitor",
              (char *)NULL);
        write_stderr("PANTHERA:configd KernelEventMonitor exec failed\n");
        configd_log("KernelEventMonitor exec failed");
        _exit(127);
    }

    configd_log("KernelEventMonitor child pid=%d", pid);
}


static CFArrayRef
copy_all_keys_matching(CFStringRef pattern, int isRegex)
{
    CFMutableArrayRef matches;
    CFIndex count;

    matches = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    if (matches == NULL) {
        return NULL;
    }

    store_lock();
    count = CFDictionaryGetCount(g_store);
    if (count > 0) {
        const void **keys = calloc((size_t)count, sizeof(*keys));
        if (keys == NULL) {
            store_unlock();
            CFRelease(matches);
            return NULL;
        }

        CFDictionaryGetKeysAndValues(g_store, keys, NULL);
        for (CFIndex i = 0; i < count; i++) {
            CFStringRef key = (CFStringRef)keys[i];

            /*
             * Phase 0 does not implement full regex matching. Regex callers get
             * broad discovery rather than a false negative.
             */
            if (isRegex || string_matches_exact_or_prefix(key, pattern)) {
                CFArrayAppendValue(matches, key);
            }
        }

        free(keys);
    }
    store_unlock();

    return matches;
}

static int
set_value_for_key(CFStringRef key, CFPropertyListRef value)
{
    CFPropertyListRef ownedValue;
    int newInstance;

    ownedValue = copy_store_value(value);
    if (ownedValue == NULL) {
        return g_instance;
    }

    store_lock();
    set_owned_value_for_key_locked(key, ownedValue);
    newInstance = g_instance;
    store_unlock();
    panthera_route_manager_store_value_changed(key, ownedValue);
    notify_store_key_changed(key);
    CFRelease(ownedValue);
    return newInstance;
}

static int
remove_value_for_key(CFStringRef key)
{
    int newInstance;

    store_lock();
    remove_value_for_key_locked(key);
    newInstance = g_instance;
    store_unlock();
    notify_store_key_changed(key);
    return newInstance;
}

int
panthera_configd_store_set_value(CFStringRef key, CFPropertyListRef value)
{
    static int traced;

    if (!traced) {
        configd_log("in-process store set_value");
        traced = 1;
    }

    if (g_store == NULL || key == NULL || value == NULL ||
        CFGetTypeID(key) != CFStringGetTypeID()) {
        return kSCStatusInvalidArgument;
    }

    set_value_for_key(key, value);
    return kSCStatusOK;
}

int
panthera_configd_store_set_multiple(CFDictionaryRef keysToSet,
                                    CFArrayRef keysToRemove,
                                    CFArrayRef keysToNotify)
{
    static int traced;

    if (!traced) {
        configd_log("in-process store set_multiple");
        traced = 1;
    }

    if (g_store == NULL) {
        return kSCStatusNoStoreSession;
    }

    if (keysToSet != NULL) {
        CFIndex count;
        const void **keysToSetRaw;
        const void **valuesToSetRaw;

        if (CFGetTypeID(keysToSet) != CFDictionaryGetTypeID()) {
            return kSCStatusInvalidArgument;
        }

        count = CFDictionaryGetCount(keysToSet);
        keysToSetRaw = calloc((size_t)count, sizeof(*keysToSetRaw));
        valuesToSetRaw = calloc((size_t)count, sizeof(*valuesToSetRaw));
        if (keysToSetRaw == NULL || valuesToSetRaw == NULL) {
            free(keysToSetRaw);
            free(valuesToSetRaw);
            return kSCStatusFailed;
        }

        CFDictionaryGetKeysAndValues(keysToSet, keysToSetRaw, valuesToSetRaw);
        for (CFIndex i = 0; i < count; i++) {
            CFStringRef key = (CFStringRef)keysToSetRaw[i];
            CFPropertyListRef value = (CFPropertyListRef)valuesToSetRaw[i];

            if (key != NULL && CFGetTypeID(key) == CFStringGetTypeID() && value != NULL) {
                set_value_for_key(key, value);
            }
        }
        publish_global_network_state_from_pending_set(keysToSet);
        free(keysToSetRaw);
        free(valuesToSetRaw);
    }

    if (keysToRemove != NULL) {
        if (CFGetTypeID(keysToRemove) != CFArrayGetTypeID()) {
            return kSCStatusInvalidArgument;
        }
        for (CFIndex i = 0; i < CFArrayGetCount(keysToRemove); i++) {
            CFStringRef key = (CFStringRef)CFArrayGetValueAtIndex(keysToRemove, i);
            if (key != NULL && CFGetTypeID(key) == CFStringGetTypeID()) {
                remove_value_for_key(key);
            }
        }
    }

    notify_store_keys_changed(keysToNotify);
    return kSCStatusOK;
}

static CFStringRef
copy_service_entity_id(CFStringRef key, CFStringRef entity)
{
    CFStringRef prefix = CFSTR("State:/Network/Service/");
    CFStringRef suffix;
    CFIndex prefixLength;
    CFIndex keyLength;
    CFIndex suffixLength;
    CFStringRef serviceID = NULL;

    if (key == NULL || entity == NULL ||
        CFGetTypeID(key) != CFStringGetTypeID()) {
        return NULL;
    }

    suffix = CFStringCreateWithFormat(NULL, NULL, CFSTR("/%@"), entity);
    if (suffix == NULL) {
        return NULL;
    }

    prefixLength = CFStringGetLength(prefix);
    keyLength = CFStringGetLength(key);
    suffixLength = CFStringGetLength(suffix);
    if (CFStringHasPrefix(key, prefix) &&
        CFStringHasSuffix(key, suffix) &&
        keyLength > prefixLength + suffixLength) {
        serviceID = CFStringCreateWithSubstring(NULL, key,
                                                CFRangeMake(prefixLength,
                                                            keyLength - prefixLength - suffixLength));
    }

    CFRelease(suffix);
    return serviceID;
}

static CFStringRef
copy_service_entity_key(CFStringRef serviceID, CFStringRef entity)
{
    if (serviceID == NULL || entity == NULL) {
        return NULL;
    }
    return SCDynamicStoreKeyCreateNetworkServiceEntity(NULL,
                                                       kSCDynamicStoreDomainState,
                                                       serviceID,
                                                       entity);
}

static CFStringRef
copy_ipconfiguration_service_id(CFStringRef key)
{
    CFStringRef prefix = CFSTR("Plugin:IPConfigurationService:");
    CFIndex prefixLength;

    if (key == NULL || CFGetTypeID(key) != CFStringGetTypeID() ||
        !CFStringHasPrefix(key, prefix)) {
        return NULL;
    }

    prefixLength = CFStringGetLength(prefix);
    if (CFStringGetLength(key) <= prefixLength) {
        return NULL;
    }

    return CFStringCreateWithSubstring(NULL, key,
                                       CFRangeMake(prefixLength,
                                                   CFStringGetLength(key) - prefixLength));
}

static CFDictionaryRef
copy_pending_or_stored_dictionary(CFDictionaryRef pending, CFStringRef key)
{
    CFTypeRef value = NULL;

    if (key == NULL) {
        return NULL;
    }
    if (pending != NULL) {
        value = CFDictionaryGetValue(pending, key);
    }
    if (value == NULL && g_store != NULL) {
        value = CFDictionaryGetValue(g_store, key);
    }
    if (value == NULL || CFGetTypeID(value) != CFDictionaryGetTypeID()) {
        return NULL;
    }
    return (CFDictionaryRef)value;
}

static CFDictionaryRef
copy_pending_or_stored_service_entity(CFDictionaryRef pending,
                                      CFStringRef serviceID,
                                      CFStringRef entity)
{
    CFStringRef key;
    CFDictionaryRef value;

    key = copy_service_entity_key(serviceID, entity);
    if (key == NULL) {
        return NULL;
    }

    value = copy_pending_or_stored_dictionary(pending, key);
    CFRelease(key);
    return value;
}

static CFDictionaryRef
copy_pending_or_stored_ipconfiguration_entity(CFDictionaryRef pending,
                                              CFStringRef serviceID,
                                              CFStringRef entity)
{
    CFStringRef key;
    CFDictionaryRef serviceDict;
    CFTypeRef value;

    key = CFStringCreateWithFormat(NULL, NULL,
                                   CFSTR("Plugin:IPConfigurationService:%@"),
                                   serviceID);
    if (key == NULL) {
        return NULL;
    }

    serviceDict = copy_pending_or_stored_dictionary(pending, key);
    CFRelease(key);
    if (serviceDict == NULL) {
        return NULL;
    }

    value = CFDictionaryGetValue(serviceDict, entity);
    if (value == NULL || CFGetTypeID(value) != CFDictionaryGetTypeID()) {
        return NULL;
    }
    return (CFDictionaryRef)value;
}

static void
publish_global_network_state_from_service(CFStringRef serviceID,
                                          CFDictionaryRef ipv4,
                                          CFDictionaryRef dns)
{
    CFMutableDictionaryRef globalIPv4;
    CFStringRef interfaceName;
    CFStringRef key = NULL;
    CFTypeRef router;

    if (serviceID == NULL || ipv4 == NULL || g_store == NULL ||
        CFGetTypeID(ipv4) != CFDictionaryGetTypeID()) {
        return;
    }

    interfaceName = (CFStringRef)CFDictionaryGetValue(ipv4, kSCPropInterfaceName);
    if (interfaceName == NULL || CFGetTypeID(interfaceName) != CFStringGetTypeID()) {
        interfaceName = (CFStringRef)CFDictionaryGetValue(ipv4, CFSTR("InterfaceName"));
    }
    if (interfaceName == NULL || CFGetTypeID(interfaceName) != CFStringGetTypeID()) {
        configd_log("service-to-global skipped missing interface");
        return;
    }

    key = SCDynamicStoreKeyCreateNetworkInterfaceEntity(NULL,
                                                        kSCDynamicStoreDomainState,
                                                        interfaceName,
                                                        kSCEntNetIPv4);
    if (key != NULL) {
        set_value_for_key(key, ipv4);
        configd_log("service-to-global interface IPv4 published");
        write_console("PANTHERA:configd service-to-global interface IPv4 published\n");
        CFRelease(key);
        key = NULL;
    }

    globalIPv4 = CFDictionaryCreateMutable(NULL, 0,
                                           &kCFTypeDictionaryKeyCallBacks,
                                           &kCFTypeDictionaryValueCallBacks);
    if (globalIPv4 == NULL) {
        return;
    }

    CFDictionarySetValue(globalIPv4,
                         kSCDynamicStorePropNetPrimaryInterface,
                         interfaceName);
    CFDictionarySetValue(globalIPv4,
                         kSCDynamicStorePropNetPrimaryService,
                         serviceID);
    router = CFDictionaryGetValue(ipv4, kSCPropNetIPv4Router);
    if (router != NULL) {
        CFDictionarySetValue(globalIPv4, kSCPropNetIPv4Router, router);
    }

    key = SCDynamicStoreKeyCreateNetworkGlobalEntity(NULL,
                                                     kSCDynamicStoreDomainState,
                                                     kSCEntNetIPv4);
    if (key != NULL) {
        set_value_for_key(key, globalIPv4);
        configd_log("service-to-global Global IPv4 published");
        write_console("PANTHERA:configd service-to-global Global IPv4 published\n");
        notify_launchd_network_up();
        CFRelease(key);
        key = NULL;
    }

    if (dns != NULL && CFGetTypeID(dns) == CFDictionaryGetTypeID()) {
        key = SCDynamicStoreKeyCreateNetworkGlobalEntity(NULL,
                                                         kSCDynamicStoreDomainState,
                                                         kSCEntNetDNS);
        if (key != NULL) {
            set_value_for_key(key, dns);
            configd_log("service-to-global Global DNS published");
            write_console("PANTHERA:configd service-to-global Global DNS published\n");
            CFRelease(key);
        }
    }

    CFRelease(globalIPv4);
}

static void
publish_global_network_state_from_pending_set(CFDictionaryRef set)
{
    CFIndex count;
    const void **keys;
    const void **values;

    if (set == NULL || CFGetTypeID(set) != CFDictionaryGetTypeID()) {
        return;
    }

    count = CFDictionaryGetCount(set);
    if (count <= 0) {
        return;
    }

    keys = calloc((size_t)count, sizeof(*keys));
    values = calloc((size_t)count, sizeof(*values));
    if (keys == NULL || values == NULL) {
        free(keys);
        free(values);
        return;
    }

    CFDictionaryGetKeysAndValues(set, keys, values);
    for (CFIndex i = 0; i < count; i++) {
        CFStringRef key = (CFStringRef)keys[i];
        CFStringRef serviceID;
        CFDictionaryRef ipv4;
        CFDictionaryRef dns;

        if (pending_link_state_is_active(key, values[i])) {
            configd_log("link-state network up");
            notify_launchd_network_up();
        }

        serviceID = copy_service_entity_id(key, kSCEntNetIPv4);
        if (serviceID != NULL) {
            ipv4 = (values[i] != NULL && CFGetTypeID(values[i]) == CFDictionaryGetTypeID())
                ? (CFDictionaryRef)values[i]
                : copy_pending_or_stored_service_entity(set, serviceID, kSCEntNetIPv4);
            dns = copy_pending_or_stored_service_entity(set, serviceID, kSCEntNetDNS);
            publish_global_network_state_from_service(serviceID, ipv4, dns);
            CFRelease(serviceID);
            continue;
        }

        serviceID = copy_ipconfiguration_service_id(key);
        if (serviceID == NULL) {
            continue;
        }
        ipv4 = copy_pending_or_stored_ipconfiguration_entity(set, serviceID, kSCEntNetIPv4);
        dns = copy_pending_or_stored_ipconfiguration_entity(set, serviceID, kSCEntNetDNS);
        publish_global_network_state_from_service(serviceID, ipv4, dns);
        CFRelease(serviceID);
    }

    free(keys);
    free(values);
}

kern_return_t
_configopen(mach_port_t server,
            xmlData_t name,
            mach_msg_type_number_t nameCnt,
            xmlData_t options,
            mach_msg_type_number_t optionsCnt,
            mach_port_t *session,
            int *status,
            audit_token_t audit_token)
{
    CFStringRef sessionName;

    refresh_network_state_if_needed();

    (void)options;
    (void)optionsCnt;
    (void)audit_token;

    if (session == NULL || status == NULL) {
        write_stderr("PANTHERA:configd configopen invalid args\n");
        return KERN_INVALID_ARGUMENT;
    }

    *session = MACH_PORT_NULL;
    *status = kSCStatusOK;

    sessionName = copy_in_string(name, nameCnt);
    if (sessionName != NULL) {
        char buf[256];
        if (CFStringGetCString(sessionName, buf, sizeof(buf), kCFStringEncodingUTF8)) {
            configd_log("open session=%s", buf);
        }
        CFRelease(sessionName);
    }

    mach_port_t session_port = MACH_PORT_NULL;
    if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE,
                           &session_port) != KERN_SUCCESS) {
        configd_log("open session port allocate failed");
        write_stderr("PANTHERA:configd configopen port allocate failed\n");
        *status = kSCStatusFailed;
        return KERN_SUCCESS;
    }

    if (mach_port_insert_right(mach_task_self(), session_port, session_port,
                               MACH_MSG_TYPE_MAKE_SEND) != KERN_SUCCESS) {
        configd_log("open session insert-right failed session=%u", session_port);
        write_stderr("PANTHERA:configd configopen insert right failed\n");
        mach_port_destroy(mach_task_self(), session_port);
        *status = kSCStatusFailed;
        return KERN_SUCCESS;
    }

    if (g_configd_port_set != MACH_PORT_NULL) {
        if (mach_port_move_member(mach_task_self(), session_port,
                                  g_configd_port_set) != KERN_SUCCESS) {
            configd_log("open session port-set add failed session=%u", session_port);
            write_stderr("PANTHERA:configd configopen move member failed\n");
            mach_port_destroy(mach_task_self(), session_port);
            *status = kSCStatusFailed;
            return KERN_SUCCESS;
        }
    }

    (void)pthread_mutex_lock(&g_notify_mutex);
    if (notify_session_get_locked(session_port, 1) == NULL) {
        write_stderr("PANTHERA:configd configopen notify get failed\n");
        (void)pthread_mutex_unlock(&g_notify_mutex);
        mach_port_destroy(mach_task_self(), session_port);
        *status = kSCStatusFailed;
        return KERN_SUCCESS;
    }
    (void)pthread_mutex_unlock(&g_notify_mutex);

    *session = session_port;
    configd_log("open returning session=%u status=%d", *session, *status);
    return KERN_SUCCESS;
}

kern_return_t
_configlist(mach_port_t server,
            xmlData_t xmlData,
            mach_msg_type_number_t xmlDataCnt,
            int isRegex,
            xmlDataOut_t *list,
            mach_msg_type_number_t *listCnt,
            int *status)
{
    CFStringRef pattern;
    CFArrayRef matches;

    (void)server;

    if (status == NULL) {
        return KERN_INVALID_ARGUMENT;
    }

    refresh_network_state_if_needed();

    pattern = copy_in_string(xmlData, xmlDataCnt);
    matches = copy_all_keys_matching(pattern, isRegex);
    if (pattern != NULL) {
        CFRelease(pattern);
    }

    if (matches == NULL) {
        *status = kSCStatusFailed;
        return KERN_SUCCESS;
    }

    *status = serialize_out(matches, list, listCnt);
    CFRelease(matches);
    return KERN_SUCCESS;
}

kern_return_t
_configadd(mach_port_t server,
           xmlData_t key,
           mach_msg_type_number_t keyCnt,
           xmlData_t data,
           mach_msg_type_number_t dataCnt,
           int *newInstance,
           int *status)
{
    CFStringRef cfKey;
    CFPropertyListRef value;

    (void)server;

    if (newInstance == NULL || status == NULL) {
        return KERN_INVALID_ARGUMENT;
    }

    *newInstance = g_instance;
    cfKey = copy_in_string(key, keyCnt);
    value = copy_in_plist(data, dataCnt);
    if (cfKey == NULL || value == NULL) {
        if (cfKey != NULL) CFRelease(cfKey);
        if (value != NULL) CFRelease(value);
        *status = kSCStatusInvalidArgument;
        return KERN_SUCCESS;
    }

    store_lock();
    if (CFDictionaryContainsKey(g_store, cfKey)) {
        *status = kSCStatusKeyExists;
    } else {
        set_owned_value_for_key_locked(cfKey, value);
        *newInstance = g_instance;
        *status = kSCStatusOK;
    }
    store_unlock();
    if (*status == kSCStatusOK) {
        panthera_route_manager_store_value_changed(cfKey, value);
        notify_store_key_changed(cfKey);
    }

    CFRelease(cfKey);
    CFRelease(value);
    return KERN_SUCCESS;
}

kern_return_t
_configadd_s(mach_port_t server,
             xmlData_t key,
             mach_msg_type_number_t keyCnt,
             xmlData_t data,
             mach_msg_type_number_t dataCnt,
             int *newInstance,
             int *status)
{
    return _configadd(server, key, keyCnt, data, dataCnt, newInstance, status);
}

kern_return_t
_configget(mach_port_t server,
           xmlData_t key,
           mach_msg_type_number_t keyCnt,
           xmlDataOut_t *data,
           mach_msg_type_number_t *dataCnt,
           int *newInstance,
           int *status)
{
    CFStringRef cfKey;
    CFPropertyListRef value;

    (void)server;

    if (newInstance == NULL || status == NULL) {
        return KERN_INVALID_ARGUMENT;
    }

    refresh_network_state_if_needed();

    if (data != NULL) *data = NULL;
    if (dataCnt != NULL) *dataCnt = 0;
    *newInstance = g_instance;

    cfKey = copy_in_string(key, keyCnt);
    if (cfKey == NULL) {
        *status = kSCStatusInvalidArgument;
        return KERN_SUCCESS;
    }

    store_lock();
    value = (CFPropertyListRef)CFDictionaryGetValue(g_store, cfKey);
    if (value == NULL) {
        *status = kSCStatusNoKey;
    } else {
        *status = serialize_out(value, data, dataCnt);
    }
    store_unlock();

    log_key_operation("get", cfKey, *status);
    CFRelease(cfKey);
    return KERN_SUCCESS;
}

kern_return_t
_configset(mach_port_t server,
           xmlData_t key,
           mach_msg_type_number_t keyCnt,
           xmlData_t data,
           mach_msg_type_number_t dataCnt,
           int instance,
           int *newInstance,
           int *status)
{
    CFStringRef cfKey;
    CFPropertyListRef value;

    (void)server;
    (void)instance;

    if (newInstance == NULL || status == NULL) {
        return KERN_INVALID_ARGUMENT;
    }

    *newInstance = g_instance;
    cfKey = copy_in_string(key, keyCnt);
    value = copy_in_plist(data, dataCnt);
    if (cfKey == NULL || value == NULL) {
        if (cfKey != NULL) CFRelease(cfKey);
        if (value != NULL) CFRelease(value);
        *status = kSCStatusInvalidArgument;
        return KERN_SUCCESS;
    }

    *newInstance = set_value_for_key(cfKey, value);
    *status = kSCStatusOK;

    log_key_operation("set", cfKey, *status);
    CFRelease(cfKey);
    CFRelease(value);
    return KERN_SUCCESS;
}

kern_return_t
_configremove(mach_port_t server, xmlData_t key, mach_msg_type_number_t keyCnt, int *status)
{
    CFStringRef cfKey;

    (void)server;

    if (status == NULL) {
        return KERN_INVALID_ARGUMENT;
    }

    cfKey = copy_in_string(key, keyCnt);
    if (cfKey == NULL) {
        *status = kSCStatusInvalidArgument;
        return KERN_SUCCESS;
    }

    store_lock();
    if (!CFDictionaryContainsKey(g_store, cfKey)) {
        *status = kSCStatusNoKey;
    } else {
        remove_value_for_key_locked(cfKey);
        *status = kSCStatusOK;
    }
    store_unlock();
    if (*status == kSCStatusOK) {
        notify_store_key_changed(cfKey);
    }

    CFRelease(cfKey);
    return KERN_SUCCESS;
}

kern_return_t
_confignotify(mach_port_t server, xmlData_t key, mach_msg_type_number_t keyCnt, int *status)
{
    (void)server;
    (void)key;
    (void)keyCnt;
    if (status == NULL) return KERN_INVALID_ARGUMENT;
    *status = kSCStatusOK;
    return KERN_SUCCESS;
}

kern_return_t
_configget_m(mach_port_t server,
             xmlData_t keys,
             mach_msg_type_number_t keysCnt,
             xmlData_t patterns,
             mach_msg_type_number_t patternsCnt,
             xmlDataOut_t *data,
             mach_msg_type_number_t *dataCnt,
             int *status)
{
    CFArrayRef keyArray = NULL;
    CFArrayRef patternArray = NULL;
    CFMutableDictionaryRef result;
    CFDictionaryRef serialized;

    (void)server;

    if (status == NULL) {
        return KERN_INVALID_ARGUMENT;
    }

    refresh_network_state_if_needed();

    if (keys != NULL && keysCnt > 0) {
        keyArray = (CFArrayRef)copy_in_plist(keys, keysCnt);
        if (keyArray != NULL && CFGetTypeID(keyArray) != CFArrayGetTypeID()) {
            CFRelease(keyArray);
            keyArray = NULL;
        }
    }
    if (patterns != NULL && patternsCnt > 0) {
        patternArray = (CFArrayRef)copy_in_plist(patterns, patternsCnt);
        if (patternArray != NULL && CFGetTypeID(patternArray) != CFArrayGetTypeID()) {
            CFRelease(patternArray);
            patternArray = NULL;
        }
    }

    result = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (result == NULL) {
        if (keyArray != NULL) CFRelease(keyArray);
        if (patternArray != NULL) CFRelease(patternArray);
        *status = kSCStatusFailed;
        return KERN_SUCCESS;
    }

    store_lock();
    CFIndex count = CFDictionaryGetCount(g_store);
    if (count > 0) {
        const void **storeKeys = calloc((size_t)count, sizeof(*storeKeys));
        const void **storeValues = calloc((size_t)count, sizeof(*storeValues));
        if (storeKeys == NULL || storeValues == NULL) {
            free(storeKeys);
            free(storeValues);
            store_unlock();
            CFRelease(result);
            if (keyArray != NULL) CFRelease(keyArray);
            if (patternArray != NULL) CFRelease(patternArray);
            *status = kSCStatusFailed;
            return KERN_SUCCESS;
        }

        CFDictionaryGetKeysAndValues(g_store, storeKeys, storeValues);
        for (CFIndex i = 0; i < count; i++) {
            CFStringRef storeKey = (CFStringRef)storeKeys[i];
            Boolean match = array_contains_string(keyArray, storeKey);

            if (!match && patternArray != NULL) {
                for (CFIndex j = 0; j < CFArrayGetCount(patternArray); j++) {
                    CFStringRef pattern = (CFStringRef)CFArrayGetValueAtIndex(patternArray, j);
                    if (CFGetTypeID(pattern) == CFStringGetTypeID() &&
                        string_matches_store_pattern(storeKey, pattern)) {
                        match = TRUE;
                        break;
                    }
                }
            }

            if (match || (keyArray == NULL && patternArray == NULL)) {
                CFDictionarySetValue(result, storeKey, storeValues[i]);
            }
        }

        free(storeKeys);
        free(storeValues);
    }
    store_unlock();

    serialized = _SCSerializeMultiple(result);
    CFRelease(result);
    if (keyArray != NULL) CFRelease(keyArray);
    if (patternArray != NULL) CFRelease(patternArray);

    if (serialized == NULL) {
        *status = kSCStatusFailed;
        return KERN_SUCCESS;
    }

    *status = serialize_out(serialized, data, dataCnt);
    CFRelease(serialized);
    return KERN_SUCCESS;
}

kern_return_t
_configset_m(mach_port_t server,
             xmlData_t data,
             mach_msg_type_number_t dataCnt,
             xmlData_t remove,
             mach_msg_type_number_t removeCnt,
             xmlData_t notify,
             mach_msg_type_number_t notifyCnt,
             int *status)
{
    CFDictionaryRef serializedSet = NULL;
    CFDictionaryRef set = NULL;
    CFArrayRef removals = NULL;
    CFArrayRef notifications = NULL;

    (void)server;

    if (status == NULL) {
        return KERN_INVALID_ARGUMENT;
    }

    if (data != NULL && dataCnt > 0) {
        serializedSet = (CFDictionaryRef)copy_in_plist(data, dataCnt);
        if (serializedSet != NULL && CFGetTypeID(serializedSet) == CFDictionaryGetTypeID()) {
            set = _SCUnserializeMultiple(serializedSet);
        }
        if (serializedSet != NULL) CFRelease(serializedSet);
        if (set == NULL) {
            *status = kSCStatusInvalidArgument;
            return KERN_SUCCESS;
        }
    }

    if (remove != NULL && removeCnt > 0) {
        removals = (CFArrayRef)copy_in_plist(remove, removeCnt);
        if (removals != NULL && CFGetTypeID(removals) != CFArrayGetTypeID()) {
            CFRelease(removals);
            removals = NULL;
            if (set != NULL) CFRelease(set);
            *status = kSCStatusInvalidArgument;
            return KERN_SUCCESS;
        }
    }

    if (notify != NULL && notifyCnt > 0) {
        notifications = (CFArrayRef)copy_in_plist(notify, notifyCnt);
        if (notifications != NULL && CFGetTypeID(notifications) != CFArrayGetTypeID()) {
            CFRelease(notifications);
            notifications = NULL;
            if (set != NULL) CFRelease(set);
            if (removals != NULL) CFRelease(removals);
            *status = kSCStatusInvalidArgument;
            return KERN_SUCCESS;
        }
    }

    if (set != NULL) {
        CFIndex count = CFDictionaryGetCount(set);
        const void **keysToSet = calloc((size_t)count, sizeof(*keysToSet));
        const void **valuesToSet = calloc((size_t)count, sizeof(*valuesToSet));
        if (keysToSet == NULL || valuesToSet == NULL) {
            free(keysToSet);
            free(valuesToSet);
            CFRelease(set);
            if (removals != NULL) CFRelease(removals);
            if (notifications != NULL) CFRelease(notifications);
            *status = kSCStatusFailed;
            return KERN_SUCCESS;
        }
        CFDictionaryGetKeysAndValues(set, keysToSet, valuesToSet);
        for (CFIndex i = 0; i < count; i++) {
            set_value_for_key((CFStringRef)keysToSet[i], (CFPropertyListRef)valuesToSet[i]);
        }
        publish_global_network_state_from_pending_set(set);
        free(keysToSet);
        free(valuesToSet);
        CFRelease(set);
    }

    if (removals != NULL) {
        for (CFIndex i = 0; i < CFArrayGetCount(removals); i++) {
            CFStringRef key = (CFStringRef)CFArrayGetValueAtIndex(removals, i);
            if (CFGetTypeID(key) == CFStringGetTypeID()) {
                remove_value_for_key(key);
            }
        }
        CFRelease(removals);
    }

    if (notifications != NULL) {
        notify_store_keys_changed(notifications);
        CFRelease(notifications);
    }

    *status = kSCStatusOK;
    return KERN_SUCCESS;
}

kern_return_t
_notifyadd(mach_port_t server, xmlData_t key, mach_msg_type_number_t keyCnt, int isRegex, int *status)
{
    CFStringRef cfKey;
    notify_session_t *session;

    if (status == NULL) return KERN_INVALID_ARGUMENT;

    cfKey = copy_in_string(key, keyCnt);
    if (cfKey == NULL) {
        *status = kSCStatusInvalidArgument;
        return KERN_SUCCESS;
    }

    (void)pthread_mutex_lock(&g_notify_mutex);
    session = notify_session_get_locked(server, 1);
    if (session == NULL) {
        (void)pthread_mutex_unlock(&g_notify_mutex);
        CFRelease(cfKey);
        *status = kSCStatusFailed;
        return KERN_SUCCESS;
    }
    if (isRegex) {
        if (!array_contains_string(session->patterns, cfKey)) {
            CFArrayAppendValue(session->patterns, cfKey);
        }
    } else {
        if (!array_contains_string(session->keys, cfKey)) {
            CFArrayAppendValue(session->keys, cfKey);
        }
    }
    (void)pthread_mutex_unlock(&g_notify_mutex);

    CFRelease(cfKey);
    *status = kSCStatusOK;
    return KERN_SUCCESS;
}

kern_return_t
_notifyremove(mach_port_t server, xmlData_t key, mach_msg_type_number_t keyCnt, int isRegex, int *status)
{
    CFStringRef cfKey;
    notify_session_t *session;
    CFMutableArrayRef array;

    if (status == NULL) return KERN_INVALID_ARGUMENT;

    cfKey = copy_in_string(key, keyCnt);
    if (cfKey == NULL) {
        *status = kSCStatusInvalidArgument;
        return KERN_SUCCESS;
    }

    (void)pthread_mutex_lock(&g_notify_mutex);
    session = notify_session_get_locked(server, 0);
    if (session != NULL) {
        array = isRegex ? session->patterns : session->keys;
        CFIndex index = CFArrayGetFirstIndexOfValue(array,
                                                    CFRangeMake(0, CFArrayGetCount(array)),
                                                    cfKey);
        if (index != kCFNotFound) {
            CFArrayRemoveValueAtIndex(array, index);
        }
    }
    (void)pthread_mutex_unlock(&g_notify_mutex);

    CFRelease(cfKey);
    *status = kSCStatusOK;
    return KERN_SUCCESS;
}

kern_return_t
_notifychanges(mach_port_t server, xmlDataOut_t *list, mach_msg_type_number_t *listCnt, int *status)
{
    CFArrayRef changes;
    notify_session_t *session;

    (void)server;
    if (status == NULL) return KERN_INVALID_ARGUMENT;

    (void)pthread_mutex_lock(&g_notify_mutex);
    session = notify_session_get_locked(server, 1);
    if (session == NULL) {
        (void)pthread_mutex_unlock(&g_notify_mutex);
        *status = kSCStatusFailed;
        return KERN_SUCCESS;
    }
    changes = CFArrayCreateCopy(NULL, session->changes);
    CFArrayRemoveAllValues(session->changes);
    (void)pthread_mutex_unlock(&g_notify_mutex);

    if (changes == NULL) {
        *status = kSCStatusFailed;
        return KERN_SUCCESS;
    }

    *status = serialize_out(changes, list, listCnt);
    CFRelease(changes);
    return KERN_SUCCESS;
}

kern_return_t
_notifyviaport(mach_port_t server, mach_port_t port, mach_msg_id_t msgid, int *status)
{
    if (status == NULL) return KERN_INVALID_ARGUMENT;
    if (msgid != 0) {
        *status = kSCStatusInvalidArgument;
        return KERN_SUCCESS;
    }

    (void)pthread_mutex_lock(&g_notify_mutex);
    notify_session_t *session = notify_session_get_locked(server, 1);
    if (session == NULL) {
        (void)pthread_mutex_unlock(&g_notify_mutex);
        *status = kSCStatusFailed;
        return KERN_SUCCESS;
    }
    if (port != MACH_PORT_NULL) {
        const void *boxed = (const void *)(uintptr_t)port;
        if (!CFArrayContainsValue(session->ports,
                                  CFRangeMake(0, CFArrayGetCount(session->ports)),
                                  boxed)) {
            CFArrayAppendValue(session->ports, boxed);
        }
    }
    (void)pthread_mutex_unlock(&g_notify_mutex);

    *status = kSCStatusOK;
    return KERN_SUCCESS;
}

kern_return_t
_notifycancel(mach_port_t server, int *status)
{
    if (status == NULL) return KERN_INVALID_ARGUMENT;
    (void)pthread_mutex_lock(&g_notify_mutex);
    notify_session_t *session = notify_session_get_locked(server, 0);
    if (session != NULL) {
        CFArrayRemoveAllValues(session->keys);
        CFArrayRemoveAllValues(session->patterns);
        CFArrayRemoveAllValues(session->changes);
        CFArrayRemoveAllValues(session->ports);
    }
    (void)pthread_mutex_unlock(&g_notify_mutex);
    *status = kSCStatusOK;
    return KERN_SUCCESS;
}

kern_return_t
_notifyset(mach_port_t server,
           xmlData_t keys,
           mach_msg_type_number_t keysCnt,
           xmlData_t patterns,
           mach_msg_type_number_t patternsCnt,
           int *status)
{
    CFArrayRef keyArray = NULL;
    CFArrayRef patternArray = NULL;
    CFMutableArrayRef newKeys;
    CFMutableArrayRef newPatterns;
    notify_session_t *session;

    if (status == NULL) return KERN_INVALID_ARGUMENT;

    if (keys != NULL && keysCnt > 0) {
        keyArray = (CFArrayRef)copy_in_plist(keys, keysCnt);
        if (keyArray != NULL && CFGetTypeID(keyArray) != CFArrayGetTypeID()) {
            CFRelease(keyArray);
            keyArray = NULL;
            *status = kSCStatusInvalidArgument;
            return KERN_SUCCESS;
        }
    }
    if (patterns != NULL && patternsCnt > 0) {
        patternArray = (CFArrayRef)copy_in_plist(patterns, patternsCnt);
        if (patternArray != NULL && CFGetTypeID(patternArray) != CFArrayGetTypeID()) {
            if (keyArray != NULL) CFRelease(keyArray);
            CFRelease(patternArray);
            *status = kSCStatusInvalidArgument;
            return KERN_SUCCESS;
        }
    }

    newKeys = copy_mutable_string_array(keyArray);
    newPatterns = copy_mutable_string_array(patternArray);
    if (keyArray != NULL) CFRelease(keyArray);
    if (patternArray != NULL) CFRelease(patternArray);
    if (newKeys == NULL || newPatterns == NULL) {
        if (newKeys != NULL) CFRelease(newKeys);
        if (newPatterns != NULL) CFRelease(newPatterns);
        *status = kSCStatusFailed;
        return KERN_SUCCESS;
    }

    (void)pthread_mutex_lock(&g_notify_mutex);
    session = notify_session_get_locked(server, 1);
    if (session == NULL) {
        (void)pthread_mutex_unlock(&g_notify_mutex);
        CFRelease(newKeys);
        CFRelease(newPatterns);
        *status = kSCStatusFailed;
        return KERN_SUCCESS;
    }
    CFRelease(session->keys);
    CFRelease(session->patterns);
    session->keys = newKeys;
    session->patterns = newPatterns;
    CFArrayRemoveAllValues(session->changes);
    (void)pthread_mutex_unlock(&g_notify_mutex);

    *status = kSCStatusOK;
    return KERN_SUCCESS;
}

kern_return_t
_notifyviafd(mach_port_t server, mach_port_t fileport, int identifier, int *status)
{
    (void)server; (void)fileport; (void)identifier;
    if (status == NULL) return KERN_INVALID_ARGUMENT;
    *status = kSCStatusOK;
    return KERN_SUCCESS;
}

kern_return_t
_snapshot(mach_port_t server, int *status)
{
    (void)server;
    if (status == NULL) return KERN_INVALID_ARGUMENT;
    *status = kSCStatusOK;
    return KERN_SUCCESS;
}

int
main(void)
{
    mach_port_t server = MACH_PORT_NULL;
    kern_return_t kr;

    trace_console("PANTHERA:configd main entered\n");
    trace_stderr("PANTHERA:configd main entered\n");
    configd_log("main entered");

    __CFInitialize();

    (void)pthread_mutex_init(&g_store_mutex, NULL);
    (void)pthread_mutex_init(&g_notify_mutex, NULL);

    configd_log("creating store");
    g_store = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    configd_log("created store ptr=%p", g_store);
    if (g_store == NULL) {
        configd_log("failed to create store");
        write_stderr("PANTHERA:configd failed to create store\n");
        return 1;
    }
    trace_stderr("PANTHERA:configd store create ok\n");

    load_setup_preferences();

    kr = bootstrap_check_in(bootstrap_port, PANTHERA_CONFIGD_SERVICE, &server);
    if (kr != KERN_SUCCESS) {
        configd_log("bootstrap_check_in failed kr=%d", kr);
        write_stderr("PANTHERA:configd bootstrap_check_in failed\n");
        return 2;
    }

    kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_PORT_SET,
                            &g_configd_port_set);
    if (kr != KERN_SUCCESS) {
        configd_log("port set allocate failed kr=%d", kr);
        return 2;
    }
    kr = mach_port_move_member(mach_task_self(), server, g_configd_port_set);
    if (kr != KERN_SUCCESS) {
        configd_log("bootstrap port set add failed kr=%d", kr);
        return 2;
    }

    publish_initial_network_state();

    configd_log("serving %s port=%u port-set=%u",
                PANTHERA_CONFIGD_SERVICE, server, g_configd_port_set);
    start_network_state_publisher();
    if (getenv("PANTHERA_CONFIGD_SELF_TEST") != NULL) {
        start_dynamic_store_self_test();
    }
    start_kernel_event_monitor_child();

    if (getenv("PANTHERA_CONFIGD_IPCONFIGURATION") != NULL) {
        return start_ipconfiguration_plugin(g_configd_port_set);
    }

    kr = mach_msg_server(panthera_config_server,
                         PANTHERA_CONFIGD_MAX_MSG,
                         g_configd_port_set,
                         MACH_RCV_TRAILER_TYPE(MACH_MSG_TRAILER_FORMAT_0) |
                         MACH_RCV_TRAILER_ELEMENTS(MACH_RCV_TRAILER_AUDIT));
    configd_log("mach_msg_server returned kr=%d", kr);
    return kr == KERN_SUCCESS ? 0 : 3;
}
