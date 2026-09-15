#ifndef _PANTHERA_LIBDISPATCH_MACH_MESSAGE_H
#define _PANTHERA_LIBDISPATCH_MACH_MESSAGE_H

#include_next <mach/message.h>

#ifndef LIBSYSCALL_MSGV_AUX_MAX_SIZE
typedef enum {
	MACH_MSGV_IDX_MSG = 0,
	MACH_MSGV_IDX_AUX = 1,
} mach_msgv_index_t;

#define MACH_MSGV_MAX_COUNT (MACH_MSGV_IDX_AUX + 1)
#define LIBSYSCALL_MSGV_AUX_MAX_SIZE 128

typedef struct {
	mach_vm_address_t msgv_data;
	mach_vm_address_t msgv_rcv_addr;
	mach_msg_size_t msgv_send_size;
	mach_msg_size_t msgv_rcv_size;
} mach_msg_vector_t;

typedef struct {
	mach_msg_size_t msgdh_size;
	uint32_t msgdh_reserved;
} mach_msg_aux_header_t;
#endif

#endif
