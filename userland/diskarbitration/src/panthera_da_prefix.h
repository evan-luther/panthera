#ifndef PANTHERA_DISKARBITRATION_PREFIX_H
#define PANTHERA_DISKARBITRATION_PREFIX_H

/* Panthera headers can carry bridgeOS annotations newer than the host SDK. */
#ifndef __API_AVAILABLE_PLATFORM_bridgeos
#define __API_AVAILABLE_PLATFORM_bridgeos(x) bridgeos,introduced=x
#endif

#endif
