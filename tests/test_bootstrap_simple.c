/*
 * test_bootstrap_simple.c — Minimal bootstrap port test.
 * Just verifies the bootstrap port is reachable via mach_msg.
 */
#include <mach/mach.h>
#include <stdio.h>
#include <string.h>

/* Inline Mach traps */
static inline mach_port_t
trap_task_self(void)
{
	mach_port_t r;
	__asm__ volatile("movq %%rcx, %%r10\n\t"
	    "movl $0x100001c, %%eax\n\t" "syscall"
	    : "=a"(r) : : "rcx", "r10", "r11", "memory");
	return r;
}

static inline kern_return_t
trap_port_allocate(mach_port_t task, mach_port_right_t right,
    mach_port_t *name)
{
	kern_return_t r;
	__asm__ volatile("movq %%rcx, %%r10\n\t"
	    "movl $0x1000010, %%eax\n\t" "syscall"
	    : "=a"(r) : "D"((uint64_t)task), "S"((uint64_t)right), "d"(name)
	    : "rcx", "r10", "r11", "memory");
	return r;
}

int main(void) {
	mach_port_t bp = MACH_PORT_NULL;
	mach_port_t rp = MACH_PORT_NULL;
	kern_return_t kr;

	printf("test_bootstrap_simple: starting\n");

	/* Get bootstrap port */
	if (bootstrap_port == MACH_PORT_NULL) {
		kr = task_get_special_port(trap_task_self(),
		    TASK_BOOTSTRAP_PORT, &bp);
		printf("  task_get_special_port kr=%d port=%d\n", kr, bp);
	} else {
		bp = bootstrap_port;
		printf("  bootstrap_port=%d (global)\n", bp);
	}

	if (bp == MACH_PORT_NULL) {
		printf("SKIP: no bootstrap port\n");
		return 0;
	}

	/* Check what type of right we have */
	mach_port_type_t ptype = 0;
	kr = mach_port_type(trap_task_self(), bp, &ptype);
	printf("  mach_port_type kr=%d type=0x%x\n", kr, ptype);
	printf("    SEND=%d RECEIVE=%d DEAD=%d\n",
	    !!(ptype & MACH_PORT_TYPE_SEND),
	    !!(ptype & MACH_PORT_TYPE_RECEIVE),
	    !!(ptype & MACH_PORT_TYPE_DEAD_NAME));

	/* Try to allocate a reply port */
	kr = trap_port_allocate(trap_task_self(), MACH_PORT_RIGHT_RECEIVE, &rp);
	printf("  reply port: kr=%d port=%d\n", kr, rp);

	/* Try sending a simple (non-MIG) message to the bootstrap port */
	struct {
		mach_msg_header_t hdr;
		uint32_t payload;
	} msg;
	memset(&msg, 0, sizeof(msg));
	msg.hdr.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
	msg.hdr.msgh_size = sizeof(msg);
	msg.hdr.msgh_remote_port = bp;
	msg.hdr.msgh_local_port = MACH_PORT_NULL;
	msg.hdr.msgh_id = 9999; /* arbitrary test ID */
	msg.payload = 0xBEEF;

	kr = mach_msg(&msg.hdr, MACH_SEND_MSG | MACH_SEND_TIMEOUT,
	    sizeof(msg), 0, MACH_PORT_NULL, 1000, MACH_PORT_NULL);
	printf("  mach_msg send kr=%d\n", kr);

	if (kr == MACH_MSG_SUCCESS) {
		printf("test_bootstrap_simple: PASS (message sent to bootstrap port)\n");
	} else {
		printf("test_bootstrap_simple: FAIL (kr=%d)\n", kr);
	}

	return (kr == MACH_MSG_SUCCESS) ? 0 : 1;
}
