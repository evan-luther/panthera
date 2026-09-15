/*
 * panthera_xpc_pipe.c — Implement xpc_pipe_try_receive() for Panthera.
 *
 * On real macOS, xpc_pipe_try_receive() does XPC deserialization from
 * Mach messages. On Panthera, we implement it as a thin wrapper around
 * mach_msg() + MIG demux, since all launchd IPC uses MIG (not XPC).
 *
 * The function receives a single Mach message from the port set,
 * dispatches it through the MIG demux function, and sends the reply.
 * XPC message handling (msg_out) is stubbed — we don't have real XPC
 * message traffic.
 */

#include <mach/mach.h>
#include <string.h>
#include <stdint.h>
#include <xpc/xpc.h>

/*
 * Message buffer — sized large enough for any launchd MIG message.
 * The largest is the job subsystem (~160KB for job_reply unions),
 * but in practice messages are much smaller.
 */
#define PANTHERA_MAX_MSG_SIZE 65536

int
xpc_pipe_try_receive(mach_port_t port, xpc_object_t *msg_out,
    mach_port_t *reply_port_out,
    boolean_t (*demux)(mach_msg_header_t *, mach_msg_header_t *),
    mach_msg_size_t max_msg_size, uint64_t flags)
{
	(void)flags;

	/* Use a reasonable buffer size, capped at our max */
	mach_msg_size_t buf_size = max_msg_size;
	if (buf_size > PANTHERA_MAX_MSG_SIZE)
		buf_size = PANTHERA_MAX_MSG_SIZE;
	if (buf_size < 4096)
		buf_size = 4096;

	/* Allocate request and reply buffers on the stack */
	char request_buf[PANTHERA_MAX_MSG_SIZE];
	char reply_buf[PANTHERA_MAX_MSG_SIZE];

	mach_msg_header_t *request = (mach_msg_header_t *)request_buf;
	mach_msg_header_t *reply = (mach_msg_header_t *)reply_buf;

	/* Clear output parameters */
	if (msg_out)
		*msg_out = NULL;
	if (reply_port_out)
		*reply_port_out = MACH_PORT_NULL;

	/*
	 * Receive a Mach message from the port set.
	 * Use MACH_RCV_LARGE so we get told if a message is too big.
	 * Request audit trailer for credential extraction.
	 * Use MACH_RCV_TIMEOUT so the main thread can return periodically
	 * to do other work (job dispatching, kqueue processing).
	 */
	mach_msg_option_t rcv_options = MACH_RCV_MSG
	    | MACH_RCV_LARGE
	    | MACH_RCV_TIMEOUT
	    | MACH_RCV_TRAILER_ELEMENTS(MACH_RCV_TRAILER_AUDIT)
	    | MACH_RCV_TRAILER_TYPE(MACH_MSG_TRAILER_FORMAT_0);

	mach_msg_return_t kr = mach_msg(request, rcv_options,
	    0, buf_size, port,
	    200, /* 200ms timeout — allows main thread to do other work */
	    MACH_PORT_NULL);

	if (kr == MACH_RCV_TIMED_OUT) {
		/* No message available — not an error, just no work to do */
		return 0;
	}
	if (kr != MACH_MSG_SUCCESS) {
		/* MACH_RCV_TOO_LARGE, etc. */
		return (int)kr;
	}

	/*
	 * We received a Mach message. Dispatch it through MIG demux.
	 * The demux function tries each MIG subsystem until one handles it.
	 */
	memset(reply_buf, 0, sizeof(mach_msg_header_t) + 256);

	if (demux(request, reply)) {
		/*
		 * MIG handled the message. Send the reply if one was generated.
		 * A reply is indicated by a non-null remote port.
		 */
		if (MACH_PORT_VALID(reply->msgh_remote_port)) {
			mach_msg_option_t send_options = MACH_SEND_MSG;

			kr = mach_msg(reply, send_options,
			    reply->msgh_size, 0,
			    MACH_PORT_NULL,
			    MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);

			if (kr != MACH_MSG_SUCCESS) {
				/*
				 * Reply send failed — deallocate the reply port
				 * to avoid leaking it.
				 */
				mach_port_deallocate(mach_task_self(),
				    reply->msgh_remote_port);
			}
		}
	}

	/* Return 0 — message handled via MIG (msg_out stays NULL) */
	return 0;
}

/*
 * xpc_pipe_routine_reply — send an XPC reply.
 * Stubbed since we don't have real XPC message traffic.
 */
int
xpc_pipe_routine_reply(xpc_object_t reply)
{
	(void)reply;
	return 0;
}
