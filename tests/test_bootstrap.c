/*
 * test_bootstrap.c — Verify bootstrap check-in and look-up work
 * using raw MIG messages (no libbootstrap dependency).
 *
 * Single-process test: checks in a service, looks it up, sends
 * a message through the looked-up port and receives on the service port.
 */
#include <mach/mach.h>
#include <mach/ndr.h>
#include <stdio.h>
#include <string.h>

#define TEST_SERVICE "com.panthera.test.bootstrap"

/* MIG message IDs for job subsystem (base 400) */
#define MIG_CHECK_IN2_ID     402
#define MIG_LOOK_UP2_ID      404

/*
 * Union buffer for MIG send+receive.
 * mach_msg with SEND|RCV uses the same buffer for both.
 */
#pragma pack(push, 4)
union check_in2_msg {
	struct {
		mach_msg_header_t hdr;
		NDR_record_t NDR;
		char servicename[128];
		uint64_t flags;
	} req;
	struct {
		mach_msg_header_t hdr;
		mach_msg_body_t body;
		mach_msg_port_descriptor_t serviceport;
		NDR_record_t NDR;
		unsigned char instanceid[16];
		char trailer[64];
	} rep;
};
#pragma pack(pop)

#pragma pack(push, 4)
union look_up2_msg {
	struct {
		mach_msg_header_t hdr;
		NDR_record_t NDR;
		char servicename[128];
		int32_t targetpid;
		unsigned char instanceid[16];
		uint64_t flags;
	} req;
	struct {
		mach_msg_header_t hdr;
		mach_msg_body_t body;
		mach_msg_port_descriptor_t serviceport;
		NDR_record_t NDR;
		char _pad[64];
		char trailer[64];
	} rep;
};
#pragma pack(pop)

static kern_return_t
raw_bootstrap_check_in(mach_port_t bp, const char *name, mach_port_t *port)
{
	union check_in2_msg msg;
	mach_port_t reply_port;
	kern_return_t kr;

	reply_port = mig_get_reply_port();

	memset(&msg, 0, sizeof(msg));
	msg.req.hdr.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,
	    MACH_MSG_TYPE_MAKE_SEND_ONCE);
	msg.req.hdr.msgh_size = sizeof(msg.req);
	msg.req.hdr.msgh_remote_port = bp;
	msg.req.hdr.msgh_local_port = reply_port;
	msg.req.hdr.msgh_id = MIG_CHECK_IN2_ID;
	msg.req.NDR = NDR_record;
	strlcpy(msg.req.servicename, name, sizeof(msg.req.servicename));
	msg.req.flags = 0;

	kr = mach_msg(&msg.req.hdr, MACH_SEND_MSG | MACH_RCV_MSG,
	    sizeof(msg.req), sizeof(msg.rep), reply_port, 5000, MACH_PORT_NULL);
	if (kr != MACH_MSG_SUCCESS) {
		printf("  check_in mach_msg failed: %d\n", kr);
		return kr;
	}

	/* After receive, reply is in msg.rep (same memory) */
	if (!(msg.rep.hdr.msgh_bits & MACH_MSGH_BITS_COMPLEX)) {
		/* Non-complex reply = error */
		typedef struct {
			mach_msg_header_t h;
			NDR_record_t ndr;
			kern_return_t ret;
		} err_reply;
		err_reply *er = (err_reply *)&msg;
		return er->ret;
	}

	*port = msg.rep.serviceport.name;
	return KERN_SUCCESS;
}

static kern_return_t
raw_bootstrap_look_up(mach_port_t bp, const char *name, mach_port_t *port)
{
	union look_up2_msg msg;
	mach_port_t reply_port;
	kern_return_t kr;

	reply_port = mig_get_reply_port();

	memset(&msg, 0, sizeof(msg));
	msg.req.hdr.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,
	    MACH_MSG_TYPE_MAKE_SEND_ONCE);
	msg.req.hdr.msgh_size = sizeof(msg.req);
	msg.req.hdr.msgh_remote_port = bp;
	msg.req.hdr.msgh_local_port = reply_port;
	msg.req.hdr.msgh_id = MIG_LOOK_UP2_ID;
	msg.req.NDR = NDR_record;
	strlcpy(msg.req.servicename, name, sizeof(msg.req.servicename));
	msg.req.targetpid = 0;
	msg.req.flags = 0;

	kr = mach_msg(&msg.req.hdr, MACH_SEND_MSG | MACH_RCV_MSG,
	    sizeof(msg.req), sizeof(msg.rep), reply_port, 5000, MACH_PORT_NULL);
	if (kr != MACH_MSG_SUCCESS) {
		printf("  look_up mach_msg failed: %d\n", kr);
		return kr;
	}

	if (!(msg.rep.hdr.msgh_bits & MACH_MSGH_BITS_COMPLEX)) {
		typedef struct {
			mach_msg_header_t h;
			NDR_record_t ndr;
			kern_return_t ret;
		} err_reply;
		err_reply *er = (err_reply *)&msg;
		return er->ret;
	}

	*port = msg.rep.serviceport.name;
	return KERN_SUCCESS;
}

int main(void) {
	mach_port_t bp;
	mach_port_t svc_port = MACH_PORT_NULL;
	mach_port_t looked_up_port = MACH_PORT_NULL;
	kern_return_t kr;

	printf("test_bootstrap: starting\n");

	bp = bootstrap_port;
	if (bp == MACH_PORT_NULL) {
		kr = task_get_special_port(mach_task_self(),
		    TASK_BOOTSTRAP_PORT, &bp);
		printf("  fetched bootstrap_port=%d (kr=%d)\n", bp, kr);
	} else {
		printf("  bootstrap_port=%d\n", bp);
	}

	if (bp == MACH_PORT_NULL) {
		printf("SKIP: no bootstrap port\n");
		return 0;
	}

	/* Test 1: check in */
	printf("  checking in as %s\n", TEST_SERVICE);
	kr = raw_bootstrap_check_in(bp, TEST_SERVICE, &svc_port);
	if (kr != KERN_SUCCESS) {
		printf("FAIL: check_in returned %d\n", kr);
		return 1;
	}
	printf("  check-in OK, service port=%d\n", svc_port);

	/* Test 2: look up */
	printf("  looking up %s\n", TEST_SERVICE);
	kr = raw_bootstrap_look_up(bp, TEST_SERVICE, &looked_up_port);
	if (kr != KERN_SUCCESS) {
		printf("FAIL: look_up returned %d\n", kr);
		return 1;
	}
	printf("  look-up OK, port=%d\n", looked_up_port);

	/* Test 3: round-trip message through looked-up port */
	{
		struct {
			mach_msg_header_t hdr;
			uint32_t value;
			char trailer[64];
		} tmsg;

		memset(&tmsg, 0, sizeof(tmsg));
		tmsg.hdr.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
		tmsg.hdr.msgh_size = sizeof(mach_msg_header_t) + sizeof(uint32_t);
		tmsg.hdr.msgh_remote_port = looked_up_port;
		tmsg.hdr.msgh_local_port = MACH_PORT_NULL;
		tmsg.hdr.msgh_id = 42;
		tmsg.value = 0xCAFE1234;

		kr = mach_msg(&tmsg.hdr, MACH_SEND_MSG,
		    sizeof(mach_msg_header_t) + sizeof(uint32_t),
		    0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
		if (kr != MACH_MSG_SUCCESS) {
			printf("FAIL: send: %d\n", kr);
			return 1;
		}
		printf("  message sent\n");

		memset(&tmsg, 0, sizeof(tmsg));
		kr = mach_msg(&tmsg.hdr, MACH_RCV_MSG | MACH_RCV_TIMEOUT,
		    0, sizeof(tmsg), svc_port, 1000, MACH_PORT_NULL);
		if (kr != MACH_MSG_SUCCESS) {
			printf("FAIL: receive: %d\n", kr);
			return 1;
		}
		printf("  received: value=0x%X id=%d\n", tmsg.value, tmsg.hdr.msgh_id);

		if (tmsg.value != 0xCAFE1234 || tmsg.hdr.msgh_id != 42) {
			printf("FAIL: payload mismatch\n");
			return 1;
		}
	}

	printf("test_bootstrap: PASS\n");
	return 0;
}
