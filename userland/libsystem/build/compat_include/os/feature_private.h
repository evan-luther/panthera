/* Panthera stub: os/feature_private.h */
#ifndef _OS_FEATURE_PRIVATE_H_
#define _OS_FEATURE_PRIVATE_H_
#include <stdbool.h>
static inline bool _panthera_os_feature_stub(const char *d __attribute__((unused)),
    const char *f __attribute__((unused)), bool def) { return def; }
#define os_feature_enabled_simple(domain, feature, def) \
    _panthera_os_feature_stub(#domain, #feature, (def))
#define os_feature_enabled(domain, feature) \
    _panthera_os_feature_stub(#domain, #feature, false)
#define os_feature_override_get_bool(domain, feature, ptr) (*(ptr) = false, false)
#endif
