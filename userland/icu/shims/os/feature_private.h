/* Shim for os/feature_private.h — Apple feature flags are not available on Panthera */
#ifndef _OS_FEATURE_PRIVATE_H
#define _OS_FEATURE_PRIVATE_H

/* os_feature_enabled(domain, feature) — always returns false on Panthera */
#define os_feature_enabled(domain, feature) (false)

#endif /* _OS_FEATURE_PRIVATE_H */
