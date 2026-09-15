#ifndef PANTHERA_UUID_UUID_TYPES_H
#define PANTHERA_UUID_UUID_TYPES_H

#include <stdint.h>

typedef uint32_t uuid_time_t;
typedef struct {
    char nodeID[6];
} uuid_node_t;

#endif
