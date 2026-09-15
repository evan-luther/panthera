#ifndef PANTHERA_OS_FEATURE_PRIVATE_H
#define PANTHERA_OS_FEATURE_PRIVATE_H

#include <stdbool.h>

/*
 * Panthera feature flags compatibility shim.
 * Panthera does not implement an Apple feature flag service; all features
 * evaluate to disabled (false) or to the caller-supplied default value.
 * Domain and feature arguments are unquoted tokens and are not evaluated.
 */
#ifndef os_feature_enabled
#define os_feature_enabled(domain, feature) (false)
#endif

#ifndef os_feature_enabled_simple
#define os_feature_enabled_simple(domain, feature, default_val) (default_val)
#endif

#endif /* PANTHERA_OS_FEATURE_PRIVATE_H */
