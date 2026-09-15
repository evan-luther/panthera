/* Panthera shim: os/variant_private.h */
#ifndef _OS_VARIANT_PRIVATE_H
#define _OS_VARIANT_PRIVATE_H

#include <stdbool.h>

static inline bool
os_variant_allows_internal_security_policies(const char *subsystem)
{
    (void)subsystem;
    return true;
}

static inline bool
os_variant_has_internal_content(const char *subsystem)
{
    (void)subsystem;
    return false;
}

static inline bool
os_variant_has_internal_diagnostics(const char *subsystem)
{
    (void)subsystem;
    return false;
}

#endif /* _OS_VARIANT_PRIVATE_H */
