#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOReturn.h>
#include <Security/SecTask.h>
#include <bsm/libbsm.h>
#include <dispatch/dispatch.h>
#include <net/ethernet.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include "ipconfigd_threads.h"
#include "wireless.h"

#ifdef bcmp
#undef bcmp
#endif

const CFStringRef kSCPropNetPvDIdentifier = CFSTR("Identifier");
const CFStringRef kSCPropNetPvDHTTPSupported = CFSTR("HTTPSupported");
const CFStringRef kSCPropNetPvDSequenceNumber = CFSTR("SequenceNumber");
const CFStringRef kSCPropNetPvDDelay = CFSTR("Delay");
const CFStringRef kSCPropNetPvDAdditionalInformation = CFSTR("AdditionalInformation");
const CFStringRef kSCPropNetPvDLegacy = CFSTR("Legacy");

const CFStringRef kIPConfigurationServiceOptionEnableDAD = CFSTR("EnableDAD");
const CFStringRef kIPConfigurationServiceOptionEnableCLAT46 = CFSTR("EnableCLAT46");
const CFStringRef kIPConfigurationServiceOptionEnableDHCPv6 = CFSTR("EnableDHCPv6");
const CFStringRef kIPConfigurationServiceOptionMTU = CFSTR("MTU");
const CFStringRef kIPConfigurationServiceOptionPerformNUD = CFSTR("PerformNUD");
const CFStringRef kIPConfigurationServiceOptionAPNName = CFSTR("APNName");
const CFStringRef kIPConfigurationServiceOptionClearState = CFSTR("ClearState");
const CFStringRef kIPConfigurationServiceOptionEnableL4S = CFSTR("EnableL4S");

int
bcmp(const void *s1, const void *s2, size_t n)
{
	return memcmp(s1, s2, n);
}

char *
ether_ntoa(const struct ether_addr *addr)
{
	static char buf[18];
	if (addr == NULL) {
		return NULL;
	}
	snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
		 addr->octet[0], addr->octet[1], addr->octet[2],
		 addr->octet[3], addr->octet[4], addr->octet[5]);
	return buf;
}

io_registry_entry_t
IORegistryEntryFromPath(mach_port_t mainPort, const io_string_t path)
{
	(void)mainPort;
	(void)path;
	return 0;
}

Boolean
__SCNetworkInterfaceIsTetheredHotspot(CFTypeRef interface)
{
	(void)interface;
	return FALSE;
}

Boolean
_SCNetworkInterfaceIsTetheredHotspot(CFTypeRef interface)
{
	return __SCNetworkInterfaceIsTetheredHotspot(interface);
}

SecTaskRef
SecTaskCreateWithAuditToken(CFAllocatorRef allocator, audit_token_t token)
{
	(void)allocator;
	(void)token;
	return NULL;
}

void
audit_token_to_au32(audit_token_t atoken,
		    uid_t *auidp,
		    uid_t *euidp,
		    gid_t *egidp,
		    uid_t *ruidp,
		    gid_t *rgidp,
		    pid_t *pidp,
		    au_asid_t *asidp,
		    au_tid_t *tidp)
{
	(void)atoken;
	if (auidp != NULL) *auidp = 0;
	if (euidp != NULL) *euidp = 0;
	if (egidp != NULL) *egidp = 0;
	if (ruidp != NULL) *ruidp = 0;
	if (rgidp != NULL) *rgidp = 0;
	if (pidp != NULL) *pidp = 0;
	if (asidp != NULL) *asidp = 0;
	if (tidp != NULL) memset(tidp, 0, sizeof(*tidp));
}

bool
os_variant_is_darwinos(const char *subsystem)
{
	(void)subsystem;
	return true;
}

IOReturn
IOPMRequestSysWake(CFDictionaryRef request)
{
	(void)request;
	return kIOReturnUnsupported;
}

void
wd_endpoint_add_queue(dispatch_queue_t queue_to_monitor)
{
	(void)queue_to_monitor;
}

bool
report_address_acquisition_symptom(int ifindex, bool success)
{
	(void)ifindex;
	(void)success;
	return false;
}

const char *
WiFiAuthTypeGetString(WiFiAuthType auth_type)
{
	return (auth_type == kWiFiAuthTypeNone) ? "none" : "unknown";
}

const char *
WiFiInfoComparisonResultGetString(WiFiInfoComparisonResult result)
{
	switch (result) {
	case kWiFiInfoComparisonResultSameNetwork:
		return "same";
	case kWiFiInfoComparisonResultNetworkChanged:
		return "network-changed";
	case kWiFiInfoComparisonResultBSSIDChanged:
		return "bssid-changed";
	default:
		return "unknown";
	}
}

WiFiInfoRef
WiFiInfoCopy(CFStringRef ifname)
{
	(void)ifname;
	return NULL;
}

CFStringRef
WiFiInfoGetSSID(WiFiInfoRef w)
{
	(void)w;
	return NULL;
}

const struct ether_addr *
WiFiInfoGetBSSID(WiFiInfoRef w)
{
	(void)w;
	return NULL;
}

CFStringRef
WiFiInfoGetBSSIDString(WiFiInfoRef w)
{
	(void)w;
	return NULL;
}

WiFiAuthType
WiFiInfoGetAuthType(WiFiInfoRef w)
{
	(void)w;
	return kWiFiAuthTypeUnknown;
}

CFStringRef
WiFiInfoGetNetworkID(WiFiInfoRef w)
{
	(void)w;
	return NULL;
}

WiFiInfoComparisonResult
WiFiInfoCompare(WiFiInfoRef info1, WiFiInfoRef info2)
{
	(void)info1;
	(void)info2;
	return kWiFiInfoComparisonResultUnknown;
}

bool
WiFiInfoAllowSharingDeviceType(WiFiInfoRef info)
{
	(void)info;
	return false;
}

WiFiConnectionID
WiFiInfoGetConnectionID(WiFiInfoRef w)
{
	(void)w;
	return 0;
}

bool
WiFiAcknowledgeConnectionID(CFStringRef ifname, WiFiConnectionID cid)
{
	(void)ifname;
	(void)cid;
	return false;
}

ipconfig_status_t
rtadv_thread(ServiceRef service_p, IFEventID_t evid, void *event_data)
{
	(void)service_p;
	(void)evid;
	(void)event_data;
	return ipconfig_status_operation_not_supported_e;
}

ipconfig_status_t
stf_thread(ServiceRef service_p, IFEventID_t evid, void *event_data)
{
	(void)service_p;
	(void)evid;
	(void)event_data;
	return ipconfig_status_operation_not_supported_e;
}

ipconfig_status_t
manual_v6_thread(ServiceRef service_p, IFEventID_t evid, void *event_data)
{
	(void)service_p;
	(void)evid;
	(void)event_data;
	return ipconfig_status_operation_not_supported_e;
}

ipconfig_status_t
linklocal_v6_thread(ServiceRef service_p, IFEventID_t evid, void *event_data)
{
	(void)service_p;
	(void)evid;
	(void)event_data;
	return ipconfig_status_operation_not_supported_e;
}
