#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/types.h>

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <SystemConfiguration/SCNetworkConfigurationPrivate.h>
#include <SystemConfiguration/SCNetworkReachability.h>
#include <sysdir.h>

typedef void *xpc_object_t;

OSStatus
AuthorizationCreateFromExternalForm(const AuthorizationExternalForm *extForm __unused, AuthorizationRef *authorization)
{
    if (authorization != NULL) {
        *authorization = NULL;
    }
    return -1;
}

OSStatus
AuthorizationMakeExternalForm(AuthorizationRef authorization __unused, AuthorizationExternalForm *extForm __unused)
{
    return -1;
}

OSStatus
SecAccessCreate(CFStringRef descriptor __unused, CFArrayRef trustedlist __unused, SecAccessRef *accessRef)
{
    if (accessRef != NULL) {
        *accessRef = NULL;
    }
    return -1;
}

SecAccessRef
SecAccessCreateWithOwnerAndACL(uid_t userId __unused,
                               gid_t groupId __unused,
                               uint32_t mode __unused,
                               CFArrayRef accessList __unused,
                               CFErrorRef *error)
{
    if (error != NULL) {
        *error = NULL;
    }
    return NULL;
}

OSStatus
SecTrustedApplicationCreateFromPath(const char *path __unused, SecTrustedApplicationRef *app)
{
    if (app != NULL) {
        *app = NULL;
    }
    return -1;
}

OSStatus
SecKeychainCopyDomainDefault(uint32_t domain __unused, SecKeychainRef *keychain)
{
    if (keychain != NULL) {
        *keychain = NULL;
    }
    return -1;
}

OSStatus
SecKeychainItemCopyContent(SecKeychainItemRef itemRef __unused,
                           uint32_t *itemClass __unused,
                           void *attrList __unused,
                           UInt32 *length,
                           void **outData)
{
    if (length != NULL) {
        *length = 0;
    }
    if (outData != NULL) {
        *outData = NULL;
    }
    return -1;
}

OSStatus
SecKeychainItemCreateFromContent(uint32_t itemClass __unused,
                                 const void *attrList __unused,
                                 UInt32 length __unused,
                                 const void *data __unused,
                                 SecKeychainRef keychain __unused,
                                 SecAccessRef initialAccess __unused,
                                 SecKeychainItemRef *itemRef)
{
    if (itemRef != NULL) {
        *itemRef = NULL;
    }
    return -1;
}

OSStatus
SecItemCopyMatching(CFDictionaryRef query __unused, CFTypeRef *result)
{
    if (result != NULL) {
        *result = NULL;
    }
    return -25300;
}

OSStatus
SecKeychainItemDelete(SecKeychainItemRef itemRef __unused)
{
    return -1;
}

OSStatus
SecKeychainItemFreeContent(void *attrList __unused, void *data __unused)
{
    return 0;
}

OSStatus
SecKeychainItemModifyContent(SecKeychainItemRef itemRef __unused,
                             const void *attrList __unused,
                             UInt32 length __unused,
                             const void *data __unused)
{
    return -1;
}

Boolean
CFURLResourceIsReachable(CFURLRef url __unused, CFErrorRef *error)
{
    if (error != NULL) {
        *error = NULL;
    }
    return false;
}

SCNetworkReachabilityRef
SCNetworkReachabilityCreateWithAddress(CFAllocatorRef allocator __unused, const struct sockaddr *address __unused)
{
    return NULL;
}

SCNetworkReachabilityRef
SCNetworkReachabilityCreateWithName(CFAllocatorRef allocator __unused, const char *nodename __unused)
{
    return NULL;
}

Boolean
SCNetworkReachabilityGetFlags(SCNetworkReachabilityRef target __unused, SCNetworkReachabilityFlags *flags)
{
    if (flags != NULL) {
        *flags = 0;
    }
    return false;
}

typedef struct IPMonitorControl *IPMonitorControlRef;
typedef CFTypeRef InterfaceAdvisoryInfoRef;
typedef CFTypeRef InterfaceRankAssertionInfoRef;

IPMonitorControlRef
IPMonitorControlCreate(void)
{
    return NULL;
}

Boolean
IPMonitorControlSetInterfacePrimaryRank(IPMonitorControlRef control __unused,
                                        CFStringRef ifname __unused,
                                        SCNetworkServicePrimaryRank rank __unused)
{
    return false;
}

SCNetworkServicePrimaryRank
IPMonitorControlGetInterfacePrimaryRank(IPMonitorControlRef control __unused, CFStringRef ifname __unused)
{
    return kSCNetworkServicePrimaryRankDefault;
}

CFStringRef
IPMonitorControlCopyInterfaceRankAssertionNotificationKey(CFStringRef ifname __unused)
{
    return NULL;
}

CFArrayRef
IPMonitorControlCopyInterfaceRankAssertionInfo(IPMonitorControlRef control __unused, CFStringRef ifname __unused)
{
    return NULL;
}

CFArrayRef
IPMonitorControlCopyInterfaceRankAssertionInterfaceNames(IPMonitorControlRef control __unused)
{
    return NULL;
}

SCNetworkServicePrimaryRank
InterfaceRankAssertionInfoGetPrimaryRank(InterfaceRankAssertionInfoRef info __unused)
{
    return kSCNetworkServicePrimaryRankDefault;
}

pid_t
InterfaceRankAssertionInfoGetProcessID(InterfaceRankAssertionInfoRef info __unused)
{
    return 0;
}

CFStringRef
InterfaceRankAssertionInfoGetProcessName(InterfaceRankAssertionInfoRef info __unused)
{
    return NULL;
}

Boolean
IPMonitorControlSetInterfaceAdvisory(IPMonitorControlRef control __unused,
                                     CFStringRef ifname __unused,
                                     SCNetworkInterfaceAdvisory advisory __unused,
                                     CFStringRef reason __unused)
{
    return false;
}

Boolean
IPMonitorControlIsInterfaceAdvisorySet(IPMonitorControlRef control __unused,
                                       CFStringRef ifname __unused,
                                       SCNetworkInterfaceAdvisory advisory __unused)
{
    return false;
}

CFStringRef
IPMonitorControlCopyInterfaceAdvisoryNotificationKey(CFStringRef ifname __unused)
{
    return NULL;
}

Boolean
IPMonitorControlAnyInterfaceAdvisoryIsSet(IPMonitorControlRef control __unused)
{
    return false;
}

CFArrayRef
IPMonitorControlCopyInterfaceAdvisoryInfo(IPMonitorControlRef control __unused, CFStringRef ifname __unused)
{
    return NULL;
}

CFArrayRef
IPMonitorControlCopyInterfaceAdvisoryInterfaceNames(IPMonitorControlRef control __unused)
{
    return NULL;
}

SCNetworkInterfaceAdvisory
InterfaceAdvisoryInfoGetAdvisory(InterfaceAdvisoryInfoRef info __unused)
{
    return kSCNetworkInterfaceAdvisoryNone;
}

pid_t
InterfaceAdvisoryInfoGetProcessID(InterfaceAdvisoryInfoRef info __unused)
{
    return 0;
}

CFStringRef
InterfaceAdvisoryInfoGetProcessName(InterfaceAdvisoryInfoRef info __unused)
{
    return NULL;
}

void *_xpc_type_error;
const char *_xpc_error_key_description = "description";

void
xpc_connection_set_context(void *connection __unused, void *context __unused)
{
}

void
xpc_connection_set_finalizer_f(void *connection __unused, void (*finalizer)(void *) __unused)
{
}

Boolean
ne_session_always_on_vpn_configs_present(void)
{
    return false;
}

void
os_log_with_args(void *log __unused, uint8_t type __unused, const char *format __unused, va_list args __unused)
{
}

int
pthread_setcanceltype(int type __unused, int *oldtype)
{
    if (oldtype != NULL) {
        *oldtype = 0;
    }
    return 0;
}

void
pthread_testcancel(void)
{
}

Boolean
WiFiIsExpensive(void)
{
    return false;
}

void
network_config_check_interface_settings(xpc_object_t if_list __unused)
{
}

sysdir_search_path_enumeration_state
sysdir_start_search_path_enumeration(sysdir_search_path_directory_t dir __unused,
                                     sysdir_search_path_domain_mask_t domainMask __unused)
{
    return 0;
}

sysdir_search_path_enumeration_state
sysdir_get_next_search_path_enumeration(sysdir_search_path_enumeration_state state __unused, char *path __unused)
{
    return 0;
}
