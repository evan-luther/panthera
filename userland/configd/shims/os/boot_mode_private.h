#ifndef PANTHERA_OS_BOOT_MODE_PRIVATE_H
#define PANTHERA_OS_BOOT_MODE_PRIVATE_H

#include <stdbool.h>

#define OS_BOOT_MODE_FVUNLOCK	"fvunlock"
#define OS_BOOT_MODE_MIGRATION	"migration"

static inline bool
os_boot_mode_query(const char **mode)
{
	if (mode != NULL) {
		*mode = NULL;
	}
	return false;
}

#endif /* PANTHERA_OS_BOOT_MODE_PRIVATE_H */
