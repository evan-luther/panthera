/*
 * test_mach_ipc.c — Verify Mach IPC primitives work end-to-end.
 *
 * Uses inline Mach traps (same convention as libsystem_kernel stubs)
 * to bypass potential dyld resolution issues in Panthera.
 */
#include <mach/mach.h>
#include <stdio.h>
#include <string.h>

#define MAGIC 0xCAFE1234

/* Inline Mach traps — bypass dyld */
static inline mach_port_t
trap_task_self(void)
{
	mach_port_t r;
	__asm__ volatile(
		"movq %%rcx, %%r10\n\t"
		"movl $0x100001c, %%eax\n\t"
		"syscall"
		: "=a"(r) : : "rcx", "r10", "r11", "memory");
	return r;
}

static inline kern_return_t
trap_port_allocate(mach_port_t task, mach_port_right_t right, mach_port_t *name)
{
	kern_return_t r;
	__asm__ volatile(
		"movq %%rcx, %%r10\n\t"
		"movl $0x1000010, %%eax\n\t"
		"syscall"
		: "=a"(r) : "D"((uint64_t)task), "S"((uint64_t)right), "d"(name)
		: "rcx", "r10", "r11", "memory");
	return r;
}

static inline kern_return_t
trap_port_insert_right(mach_port_t task, mach_port_t name, mach_port_t poly,
    mach_msg_type_name_t disp)
{
	kern_return_t r;
	register uint64_t r10 __asm__("r10") = (uint64_t)disp;
	__asm__ volatile(
		"movl $0x1000015, %%eax\n\t"
		"syscall"
		: "=a"(r)
		: "D"((uint64_t)task), "S"((uint64_t)name),
		  "d"((uint64_t)poly), "r"(r10)
		: "rcx", "r11", "memory");
	return r;
}

static inline kern_return_t
trap_port_deallocate(mach_port_t task, mach_port_t name)
{
	kern_return_t r;
	__asm__ volatile(
		"movq %%rcx, %%r10\n\t"
		"movl $0x1000012, %%eax\n\t"
		"syscall"
		: "=a"(r) : "D"((uint64_t)task), "S"((uint64_t)name)
		: "rcx", "r10", "r11", "memory");
	return r;
}

struct test_msg {
	mach_msg_header_t hdr;
	uint32_t payload;
	/* Space for kernel trailer (appended on receive) */
	char trailer[64];
};

int main(void) {
	mach_port_t self, port;
	kern_return_t kr;
	struct test_msg send_msg, recv_msg;

	printf("test_mach_ipc: starting\n");

	self = trap_task_self();
	printf("  task self: %d\n", self);

	/* 1. Allocate a receive right */
	kr = trap_port_allocate(self, MACH_PORT_RIGHT_RECEIVE, &port);
	if (kr != KERN_SUCCESS) {
		printf("FAIL: port_allocate: %d\n", kr);
		return 1;
	}
	printf("  port allocated: %d\n", port);

	/* 2. Insert a send right */
	kr = trap_port_insert_right(self, port, port, MACH_MSG_TYPE_MAKE_SEND);
	if (kr != KERN_SUCCESS) {
		printf("FAIL: insert_right: %d\n", kr);
		return 1;
	}
	printf("  send right inserted\n");

	/* 3. Send a message to ourselves */
	memset(&send_msg, 0, sizeof(send_msg));
	send_msg.hdr.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
	send_msg.hdr.msgh_size = sizeof(mach_msg_header_t) + sizeof(uint32_t);
	send_msg.hdr.msgh_remote_port = port;
	send_msg.hdr.msgh_local_port = MACH_PORT_NULL;
	send_msg.hdr.msgh_id = 42;
	send_msg.payload = MAGIC;

	kr = mach_msg(&send_msg.hdr, MACH_SEND_MSG,
	    sizeof(mach_msg_header_t) + sizeof(uint32_t),
	    0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
	if (kr != MACH_MSG_SUCCESS) {
		printf("FAIL: mach_msg send: %d\n", kr);
		return 1;
	}
	printf("  message sent (payload=0x%X)\n", MAGIC);

	/* 4. Receive the message */
	memset(&recv_msg, 0, sizeof(recv_msg));
	kr = mach_msg(&recv_msg.hdr, MACH_RCV_MSG, 0, sizeof(recv_msg),
	    port, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
	if (kr != MACH_MSG_SUCCESS) {
		printf("FAIL: mach_msg recv: %d\n", kr);
		return 1;
	}
	printf("  message received (payload=0x%X, id=%d)\n",
	    recv_msg.payload, recv_msg.hdr.msgh_id);

	if (recv_msg.payload != MAGIC || recv_msg.hdr.msgh_id != 42) {
		printf("FAIL: payload mismatch\n");
		return 1;
	}

	/* 5. Deallocate */
	trap_port_deallocate(self, port);
	printf("  port deallocated\n");

	printf("test_mach_ipc: PASS\n");
	return 0;
}
