#ifndef PANTHERA_OS_STATE_PRIVATE_H
#define PANTHERA_OS_STATE_PRIVATE_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t os_state_hints_t;

typedef struct os_state_data_s {
    uint32_t osd_type;
    uint32_t osd_data_size;
    char osd_title[64];
    uint8_t osd_data[];
} *os_state_data_t;

typedef os_state_data_t (^os_state_block_t)(os_state_hints_t hints);

#define OS_STATE_DATA_SERIALIZED_NSCF_OBJECT 1
#define MAX_STATEDUMP_SIZE (1024 * 1024)
#define OS_STATE_DATA_SIZE_NEEDED(len) (sizeof(struct os_state_data_s) + (len))

static inline int
os_state_add_handler(void *queue __unused, os_state_block_t block __unused)
{
    return 0;
}

#endif /* PANTHERA_OS_STATE_PRIVATE_H */
