#ifndef PANTHERA_OS_LOG_PRIVATE_H
#define PANTHERA_OS_LOG_PRIVATE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <os/log.h>

typedef struct panthera_os_log_pack {
	uint64_t olp_continuous_time;
	struct timespec olp_wall_time;
	const char *format;
} *os_log_pack_t;

#ifndef os_log_is_debug_enabled
#define os_log_is_debug_enabled(log) (0)
#endif

#ifndef os_log_pack_size
#define os_log_pack_size(format, ...) (1)
#endif

#ifndef os_log_pack_decl
#define os_log_pack_decl(name, size) \
	struct panthera_os_log_pack name##_storage = {0}; \
	os_log_pack_t name = &name##_storage
#endif

#ifndef os_log_pack_fill
#define os_log_pack_fill(pack, size, saved_errno, fmt, ...) \
	do { \
		(pack)->format = (fmt); \
	} while (0)
#endif

static inline void
os_log_pack_send(os_log_pack_t pack, os_log_t log, os_log_type_t type)
{
	(void)pack;
	(void)log;
	(void)type;
}

static inline char *
os_log_pack_compose(os_log_pack_t pack, os_log_t log, os_log_type_t type, char *buffer, size_t size)
{
	(void)log;
	(void)type;
	if (size > 0) {
		if ((pack != NULL) && (pack->format != NULL)) {
			(void)snprintf(buffer, size, "%s", pack->format);
		} else {
			buffer[0] = '\0';
		}
	}
	return buffer;
}

static inline char *
os_log_pack_send_and_compose(os_log_pack_t pack, os_log_t log, os_log_type_t type, char *buffer, size_t size)
{
	os_log_pack_send(pack, log, type);
	return os_log_pack_compose(pack, log, type, buffer, size);
}

#endif /* PANTHERA_OS_LOG_PRIVATE_H */
