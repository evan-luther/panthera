/* Panthera shim: os/feature_private.h
 * Feature flag checking — all features disabled.
 */
#ifndef _OS_FEATURE_PRIVATE_H
#define _OS_FEATURE_PRIVATE_H

#include <stdbool.h>

#define os_feature_enabled_simple(domain, feature, default_val) (default_val)
#define os_feature_enabled(domain, feature) false

#endif /* _OS_FEATURE_PRIVATE_H */
