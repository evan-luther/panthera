/*
 * Panthera compat: symbols needed by notifyd/libnotify that are
 * missing from the Panthera sysroot.
 */
#include <mach/mach.h>
#include <mach/ndr.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

/* os_unfair_lock_assert_owner — debug assertion, no-op */
#include <os/lock.h>
void os_unfair_lock_assert_owner(const os_unfair_lock *lock __attribute__((unused))) {}
void os_unfair_lock_assert_not_owner(const os_unfair_lock *lock __attribute__((unused))) {}

/*
 * audit_token_to_* convenience extractors.
 * Apple's audit_token_t is { unsigned int val[8]; }
 * Layout: val[0]=auid, val[1]=euid, val[2]=egid, val[3]=ruid,
 *         val[4]=rgid, val[5]=pid, val[6]=asid, val[7]=pidversion
 */
#include <bsm/libbsm.h>

uid_t audit_token_to_euid(audit_token_t atoken) { return (uid_t)atoken.val[1]; }
gid_t audit_token_to_egid(audit_token_t atoken) { return (gid_t)atoken.val[2]; }
uid_t audit_token_to_ruid(audit_token_t atoken) { return (uid_t)atoken.val[3]; }
gid_t audit_token_to_rgid(audit_token_t atoken) { return (gid_t)atoken.val[4]; }
pid_t audit_token_to_pid(audit_token_t atoken) { return (pid_t)atoken.val[5]; }
int   audit_token_to_asid(audit_token_t atoken) { return (int)atoken.val[6]; }

/* notify_set_options — called by notifyd to disable self-notification.
 * Only needed when linking the daemon (not the client library which has it).
 */
#ifdef PANTHERA_DAEMON_BUILD
void notify_set_options(uint32_t opts) { (void)opts; }

/* Panthera shm_open wrapper: if the real shm_open fails (no /dev/shm),
 * fall back to a regular file in /tmp. */
#include <sys/mman.h>
#include <errno.h>
int panthera_shm_open(const char *name, int oflag, int mode)
{
    int fd = shm_open(name, oflag, mode);
    if (fd >= 0) return fd;

    /* Fall back: use a regular file */
    char path[256];
    snprintf(path, sizeof(path), "/tmp/.panthera_shm_%s",
        name[0] == '/' ? name + 1 : name);
    fd = open(path, oflag | O_CLOEXEC, mode);
    return fd;
}
#endif

#ifdef PANTHERA_DAEMON_BUILD
static void
panthera_notifyd_diag(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    if (getenv("PANTHERA_NOTIFYD_TRACE") == NULL) {
        return;
    }
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n > 0) {
        size_t len = (size_t)n;
        if (len > sizeof(buf)) len = sizeof(buf);
        /* Write to fd 2 (stderr) — inherited from parent.
         * Also try /dev/console as fallback. */
        (void)write(STDERR_FILENO, buf, len);
        int cfd = open("/dev/console", O_WRONLY | O_NOCTTY | O_NONBLOCK, 0);
        if (cfd >= 0) {
            (void)write(cfd, buf, len);
            (void)close(cfd);
        }
    }
}
#else
static void
panthera_notifyd_diag(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    if (getenv("PANTHERA_NOTIFYD_TRACE") == NULL) {
        return;
    }
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        size_t len = (size_t)n;
        if (len > sizeof(buf)) len = sizeof(buf);
        (void)write(STDERR_FILENO, buf, len);
    }
}
#endif

/*
 * bootstrap_look_up: find a service port by name.
 * MIG subsystem job (base 400): look_up2 = 404, reply = 504.
 *
 * IMPORTANT: mach_msg with MACH_SEND_MSG|MACH_RCV_MSG uses a SINGLE buffer
 * for both the outgoing request and the incoming reply.  The reply overwrites
 * the request in the same buffer.
 */
typedef char name_t[128];

kern_return_t
bootstrap_look_up(mach_port_t bp, const char *service_name, mach_port_t *sp)
{
    if (!bp || !service_name || !sp)
        return KERN_INVALID_ARGUMENT;

    panthera_notifyd_diag("bootstrap_look_up begin bp=%u service=%s\n", bp, service_name);

    /* Single buffer large enough for both request and reply.
     * look_up2 request: Head + NDR + name_t + pid_t + uuid_t + uint64_t flags
     * Must use pack(4) to match MIG-generated struct layout. */
#pragma pack(push, 4)
    union {
        struct {
            mach_msg_header_t head;
            NDR_record_t ndr;
            name_t service_name;
            int32_t targetpid;           /* pid_t */
            unsigned char instanceid[16]; /* uuid_t */
            uint64_t flags;
        } req;
        struct {
            mach_msg_header_t head;
            mach_msg_body_t body;
            mach_msg_port_descriptor_t port;
            NDR_record_t ndr;
            uint64_t instance_id;
            mach_msg_trailer_t trailer;
        } rep;
        struct {
            mach_msg_header_t head;
            NDR_record_t ndr;
            kern_return_t RetCode;
            mach_msg_trailer_t trailer;
        } err;
    } msg;
#pragma pack(pop)

    memset(&msg, 0, sizeof(msg));
    mach_port_t reply_port = mig_get_reply_port();

    msg.req.head.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,
                                              MACH_MSG_TYPE_MAKE_SEND_ONCE);
    msg.req.head.msgh_size = sizeof(msg.req);
    msg.req.head.msgh_remote_port = bp;
    msg.req.head.msgh_local_port = reply_port;
    msg.req.head.msgh_id = 404;  /* look_up2 */
    msg.req.ndr = NDR_record;
    strlcpy(msg.req.service_name, service_name, sizeof(msg.req.service_name));
    msg.req.targetpid = 0;
    /* instanceid and flags already zeroed by memset */

    kern_return_t kr = mach_msg(&msg.req.head,
        MACH_SEND_MSG | MACH_RCV_MSG,
        sizeof(msg.req), sizeof(msg),
        reply_port,
        MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);

    panthera_notifyd_diag("bootstrap_look_up kr=%d id=%d bits=0x%x size=%u\n",
        kr, msg.rep.head.msgh_id, msg.rep.head.msgh_bits, msg.rep.head.msgh_size);

    if (kr != KERN_SUCCESS) return kr;

    /* Complex reply = success with port */
    if (msg.rep.head.msgh_bits & MACH_MSGH_BITS_COMPLEX) {
        *sp = msg.rep.port.name;
        panthera_notifyd_diag("bootstrap_look_up success port=%u\n", *sp);
        return KERN_SUCCESS;
    }
    /* Simple reply = MIG error */
    if (msg.rep.head.msgh_id == 504) {
        panthera_notifyd_diag("bootstrap_look_up MIG error=%d\n", msg.err.RetCode);
        return msg.err.RetCode ? msg.err.RetCode : KERN_FAILURE;
    }
    return KERN_FAILURE;
}

/*
 * bootstrap_check_in: check in to receive a service port from launchd.
 * MIG subsystem job (base 400): check_in2 = 402, reply = 502.
 */
kern_return_t
bootstrap_check_in(mach_port_t bp, const char *service_name, mach_port_t *sp)
{
    if (!bp || !service_name || !sp)
        return KERN_INVALID_ARGUMENT;

    panthera_notifyd_diag("bootstrap_check_in begin bp=%u service=%s\n", bp, service_name);

    union {
        struct {
            mach_msg_header_t head;
            NDR_record_t ndr;
            name_t service_name;
            uint64_t flags;      /* MIG check_in2 has a flags field */
        } req;
        struct {
            mach_msg_header_t head;
            mach_msg_body_t body;
            mach_msg_port_descriptor_t port;
            NDR_record_t ndr;
            uint64_t instance_id;
            mach_msg_trailer_t trailer;
        } rep;
        struct {
            mach_msg_header_t head;
            NDR_record_t ndr;
            kern_return_t RetCode;
            mach_msg_trailer_t trailer;
        } err;
    } msg;

    memset(&msg, 0, sizeof(msg));
    mach_port_t reply_port = mig_get_reply_port();

    msg.req.head.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,
                                              MACH_MSG_TYPE_MAKE_SEND_ONCE);
    msg.req.head.msgh_size = sizeof(msg.req);
    msg.req.head.msgh_remote_port = bp;
    msg.req.head.msgh_local_port = reply_port;
    msg.req.head.msgh_id = 402;  /* check_in2 */
    msg.req.ndr = NDR_record;
    strlcpy(msg.req.service_name, service_name, sizeof(msg.req.service_name));
    msg.req.flags = 0;

    kern_return_t kr = mach_msg(&msg.req.head,
        MACH_SEND_MSG | MACH_RCV_MSG,
        sizeof(msg.req), sizeof(msg),
        reply_port,
        MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);

    panthera_notifyd_diag("bootstrap_check_in kr=%d id=%d bits=0x%x size=%u\n",
        kr, msg.rep.head.msgh_id, msg.rep.head.msgh_bits, msg.rep.head.msgh_size);

    if (kr != KERN_SUCCESS) return kr;

    /* Complex reply = success with port */
    if (msg.rep.head.msgh_bits & MACH_MSGH_BITS_COMPLEX) {
        *sp = msg.rep.port.name;
        panthera_notifyd_diag("bootstrap_check_in success port=%u desc_count=%u\n",
            *sp, msg.rep.body.msgh_descriptor_count);
        return KERN_SUCCESS;
    }
    /* Simple reply = MIG error */
    if (msg.rep.head.msgh_id == 502) {
        panthera_notifyd_diag("bootstrap_check_in MIG error=%d\n", msg.err.RetCode);
        return msg.err.RetCode ? msg.err.RetCode : KERN_FAILURE;
    }
    panthera_notifyd_diag("bootstrap_check_in unexpected id=%d\n", msg.rep.head.msgh_id);
    return KERN_FAILURE;
}

/* bootstrap_register: deprecated, no-op */
kern_return_t
bootstrap_register(mach_port_t bp __attribute__((unused)),
                   const char *service_name __attribute__((unused)),
                   mach_port_t sp __attribute__((unused)))
{
    return KERN_SUCCESS;
}

#ifdef PANTHERA_DAEMON_BUILD
/*
 * dispatch_mach SPI stubs for notifyd daemon.
 *
 * Panthera's pthread_create is broken (threads never get scheduled),
 * so we cannot use background threads for Mach message reception.
 * Instead, we collect Mach ports from dispatch_mach_connect calls and
 * replace dispatch_main() with a port-set based mach_msg loop on the
 * main thread.
 */
#include <dispatch/dispatch.h>
#include <stdlib.h>
#include <mach/mig.h>

#define PANTHERA_MAX_MACH_CHANNELS 8

typedef struct dispatch_mach_s {
    void *context;
    void (*handler)(void *, unsigned long, struct dispatch_mach_msg_s *, int);
    mach_port_t recv_port;
} *dispatch_mach_t;

typedef struct dispatch_mach_msg_s {
    mach_msg_header_t *msg;
    size_t size;
    char buf[0];
} *dispatch_mach_msg_t;

/* Global registry of mach channels for the main-thread loop */
static struct {
    dispatch_mach_t channels[PANTHERA_MAX_MACH_CHANNELS];
    int count;
    mach_port_t portset;
} panthera_mach_registry;

dispatch_mach_t
dispatch_mach_create_f(const char *label, dispatch_queue_t queue,
    void *context,
    void (*handler)(void *, unsigned long, dispatch_mach_msg_t, int))
{
    (void)label; (void)queue;
    dispatch_mach_t dm = calloc(1, sizeof(struct dispatch_mach_s));
    dm->context = context;
    dm->handler = handler;
    return dm;
}

void
dispatch_mach_connect(dispatch_mach_t dm, mach_port_t recv,
    mach_port_t send __attribute__((unused)),
    dispatch_mach_msg_t checkin __attribute__((unused)))
{
    dm->recv_port = recv;
    if (recv == MACH_PORT_NULL) return;

    /* Create port set on first use */
    if (panthera_mach_registry.portset == MACH_PORT_NULL) {
        mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_PORT_SET,
            &panthera_mach_registry.portset);
    }

    /* Add this port to the set */
    mach_port_insert_member(mach_task_self(), recv,
        panthera_mach_registry.portset);

    /* Register the channel */
    if (panthera_mach_registry.count < PANTHERA_MAX_MACH_CHANNELS) {
        panthera_mach_registry.channels[panthera_mach_registry.count++] = dm;
    }

    panthera_notifyd_diag("dispatch_mach_connect recv=%u portset=%u count=%d\n",
        recv, panthera_mach_registry.portset, panthera_mach_registry.count);
}

mach_msg_header_t *
dispatch_mach_msg_get_msg(dispatch_mach_msg_t msg, size_t *size)
{
    if (size) *size = msg->size;
    return msg->msg;
}

_Bool
dispatch_mach_mig_demux(void *context __attribute__((unused)),
    const struct mig_subsystem *const *subsystems, size_t count,
    dispatch_mach_msg_t dmsg)
{
    mach_msg_header_t *request = dmsg->msg;
    mach_msg_id_t id = request->msgh_id;

    for (size_t i = 0; i < count; i++) {
        const struct mig_subsystem *sub = subsystems[i];
        if (id >= sub->start && id < sub->end) {
            int idx = id - sub->start;
            mig_routine_t routine = sub->routine[idx].stub_routine;
            if (routine) {
                char reply_buf[sub->maxsize];
                memset(reply_buf, 0, sizeof(reply_buf));
                mach_msg_header_t *reply = (mach_msg_header_t *)reply_buf;

                /* MIG reply defaults — normally set by foo_server() */
                reply->msgh_bits = MACH_MSGH_BITS(
                    MACH_MSGH_BITS_REMOTE(request->msgh_bits), 0);
                reply->msgh_remote_port = request->msgh_remote_port;
                reply->msgh_local_port = MACH_PORT_NULL;
                reply->msgh_size = (mach_msg_size_t)sizeof(mig_reply_error_t);
                reply->msgh_id = request->msgh_id + 100;

                routine(request, reply);

                panthera_notifyd_diag("MIG demux: id=%d->%d remote=%u size=%u rc=%d\n",
                    id, reply->msgh_id, reply->msgh_remote_port,
                    reply->msgh_size, ((mig_reply_error_t *)reply)->RetCode);

                if (reply->msgh_remote_port != MACH_PORT_NULL) {
                    panthera_notifyd_diag("MIG send: bits=0x%x remote=%u size=%u\n",
                        reply->msgh_bits, reply->msgh_remote_port, reply->msgh_size);
                    kern_return_t skr = mach_msg(reply,
                        MACH_SEND_MSG,
                        reply->msgh_size, 0,
                        MACH_PORT_NULL,
                        MACH_MSG_TIMEOUT_NONE,
                        MACH_PORT_NULL);
                    panthera_notifyd_diag("MIG send kr=%d\n", skr);
                } else {
                    panthera_notifyd_diag("MIG send: SKIPPED (remote=NULL)\n");
                }
                return true;
            }
        }
    }
    return false;
}

/*
 * Override dispatch_main: instead of the (broken) dispatch event loop,
 * run a blocking mach_msg loop on the port set that collects all
 * registered Mach channels.
 */
/* Register implementations — call _notify_lib_register_plain to actually
 * record the token in global.notify_state.  Skip shared memory and process
 * monitoring (the parts that crash on Panthera). */
#include <stdint.h>
#include "notifyd.h"
#include "libnotify.h"

kern_return_t panthera_register_plain_2(mach_port_t s, char *name, int token, audit_token_t a) {
    (void)s;
    pid_t pid = (pid_t)-1;
    uint64_t nid = 0;

    audit_token_to_au32(a, NULL, NULL, NULL, NULL, NULL, &pid, NULL, NULL);
    _notify_lib_register_plain(&global.notify_state, name, pid, token, SLOT_NONE, 0, 0, &nid);
    return KERN_SUCCESS;
}

kern_return_t panthera_register_check_2(mach_port_t s, char *name, int token,
    int *size, int *slot, uint64_t *nid, int *status, audit_token_t a) {
    (void)s;
    pid_t pid = (pid_t)-1;

    audit_token_to_au32(a, NULL, NULL, NULL, NULL, NULL, &pid, NULL, NULL);
    *status = _notify_lib_register_plain(&global.notify_state, name, pid, token, SLOT_NONE, 0, 0, nid);
    *size = -1;   /* No shared memory */
    *slot = -1;   /* No slot */

    panthera_notifyd_diag("register_check_2: name=%s token=%d pid=%d nid=%llu status=%d\n",
        name ? name : "(null)", token, pid, (unsigned long long)*nid, *status);
    return KERN_SUCCESS;
}

/* dispatch_activate, dispatch_set_context, etc. are no-op'd via
 * panthera_dispatch_stubs.h which is force-included in the build. */

void
panthera_dispatch_main(void)
{
    mach_port_t portset = panthera_mach_registry.portset;
    if (portset == MACH_PORT_NULL) {
        panthera_notifyd_diag("dispatch_main: no portset, sleeping forever\n");
        for (;;) sleep(3600);
    }

    panthera_notifyd_diag("dispatch_main: entering mach_msg loop on portset=%u (%d channels)\n",
        portset, panthera_mach_registry.count);

    for (;;) {
        size_t bufsz = 8192;
        dispatch_mach_msg_t dmsg = calloc(1, sizeof(struct dispatch_mach_msg_s) + bufsz);
        if (!dmsg) break;
        dmsg->msg = (mach_msg_header_t *)dmsg->buf;
        dmsg->size = bufsz;

        mach_msg_option_t rcv_opts = MACH_RCV_MSG | MACH_RCV_LARGE |
            MACH_RCV_TRAILER_TYPE(MACH_MSG_TRAILER_FORMAT_0) |
            MACH_RCV_TRAILER_ELEMENTS(MACH_RCV_TRAILER_AUDIT);

        kern_return_t kr = mach_msg(dmsg->msg, rcv_opts,
            0, (mach_msg_size_t)bufsz, portset,
            MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);

        if (kr == MACH_RCV_TOO_LARGE) {
            mach_msg_size_t needed = dmsg->msg->msgh_size + MAX_TRAILER_SIZE;
            free(dmsg);
            dmsg = calloc(1, sizeof(struct dispatch_mach_msg_s) + needed);
            if (!dmsg) break;
            dmsg->msg = (mach_msg_header_t *)dmsg->buf;
            dmsg->size = needed;
            kr = mach_msg(dmsg->msg, rcv_opts,
                0, needed, portset,
                MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
        }

        if (kr != KERN_SUCCESS) {
            panthera_notifyd_diag("dispatch_main: recv failed kr=%d\n", kr);
            free(dmsg);
            continue;
        }

        /* Find the channel whose port matches msgh_local_port */
        mach_port_t local = dmsg->msg->msgh_local_port;
        dispatch_mach_t target = NULL;
        for (int i = 0; i < panthera_mach_registry.count; i++) {
            if (panthera_mach_registry.channels[i]->recv_port == local) {
                target = panthera_mach_registry.channels[i];
                break;
            }
        }

        if (target) {
            target->handler(target->context, 2, dmsg, 0);
        } else {
            panthera_notifyd_diag("dispatch_main: no handler for port %u msg_id=%d\n",
                local, dmsg->msg->msgh_id);
        }

        free(dmsg);
    }
}
#endif /* PANTHERA_DAEMON_BUILD */
