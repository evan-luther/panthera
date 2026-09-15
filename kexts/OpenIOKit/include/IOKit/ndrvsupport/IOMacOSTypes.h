/*
 * Forward to Apple's real NDRV header when a kext build needs the classic
 * Mac type surface. The local stub was enough for earlier kexts, but
 * IOGraphicsFamily's I2C/NDRV path includes the full header chain.
 */
#ifndef PANTHERA_FORWARD_IOKIT_IOMACOSTYPES_H
#define PANTHERA_FORWARD_IOKIT_IOMACOSTYPES_H

#include_next <IOKit/ndrvsupport/IOMacOSTypes.h>

#endif
