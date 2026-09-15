/*
 * test_xpc_e2e.c — Single-process XPC serialization + Mach IPC test.
 *
 * Checks in with launchd, sends an XPC-serialized message through the
 * looked-up port, receives on the check-in port, and verifies round-trip.
 *
 * This is test_bootstrap.c upgraded to use the full XPC wire format.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mach/mach.h>
#include <mach/ndr.h>

/* Declare XPC functions we need */
typedef void *xpc_object_t;

extern xpc_object_t xpc_dictionary_create(const char *const *keys,
    const xpc_object_t *values, size_t count);
extern void xpc_dictionary_set_string(xpc_object_t, const char *, const char *);
extern void xpc_dictionary_set_int64(xpc_object_t, const char *, int64_t);
extern void xpc_dictionary_set_bool(xpc_object_t, const char *, int);
extern const char *xpc_dictionary_get_string(xpc_object_t, const char *);
extern int64_t xpc_dictionary_get_int64(xpc_object_t, const char *);
extern int xpc_dictionary_get_bool(xpc_object_t, const char *);
extern size_t xpc_dictionary_get_count(xpc_object_t);
extern void xpc_release(xpc_object_t);

extern xpc_object_t xpc_connection_create_mach_service(const char *,
    void *, uint64_t);
extern void xpc_connection_set_event_handler(xpc_object_t, void *);
extern void xpc_connection_resume(xpc_object_t);
extern void xpc_connection_send_message(xpc_object_t, xpc_object_t);

/* Raw bootstrap MIG — proven working in test_bootstrap.c */
#define MIG_CHECK_IN2_ID  402
#define MIG_LOOK_UP2_ID   404

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
raw_check_in(mach_port_t bp, const char *name, mach_port_t *port)
{
    union check_in2_msg msg;
    mach_port_t rp = mig_get_reply_port();
    memset(&msg, 0, sizeof(msg));
    msg.req.hdr.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,
        MACH_MSG_TYPE_MAKE_SEND_ONCE);
    msg.req.hdr.msgh_size = sizeof(msg.req);
    msg.req.hdr.msgh_remote_port = bp;
    msg.req.hdr.msgh_local_port = rp;
    msg.req.hdr.msgh_id = MIG_CHECK_IN2_ID;
    msg.req.NDR = NDR_record;
    strlcpy(msg.req.servicename, name, sizeof(msg.req.servicename));
    kern_return_t kr = mach_msg(&msg.req.hdr, MACH_SEND_MSG | MACH_RCV_MSG,
        sizeof(msg.req), sizeof(msg.rep), rp, 5000, MACH_PORT_NULL);
    if (kr != MACH_MSG_SUCCESS) return kr;
    if (!(msg.rep.hdr.msgh_bits & MACH_MSGH_BITS_COMPLEX)) {
        typedef struct { mach_msg_header_t h; NDR_record_t ndr; kern_return_t ret; } err_reply;
        return ((err_reply *)&msg)->ret;
    }
    *port = msg.rep.serviceport.name;
    return KERN_SUCCESS;
}

static kern_return_t
raw_look_up(mach_port_t bp, const char *name, mach_port_t *port)
{
    union look_up2_msg msg;
    mach_port_t rp = mig_get_reply_port();
    memset(&msg, 0, sizeof(msg));
    msg.req.hdr.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,
        MACH_MSG_TYPE_MAKE_SEND_ONCE);
    msg.req.hdr.msgh_size = sizeof(msg.req);
    msg.req.hdr.msgh_remote_port = bp;
    msg.req.hdr.msgh_local_port = rp;
    msg.req.hdr.msgh_id = MIG_LOOK_UP2_ID;
    msg.req.NDR = NDR_record;
    strlcpy(msg.req.servicename, name, sizeof(msg.req.servicename));
    kern_return_t kr = mach_msg(&msg.req.hdr, MACH_SEND_MSG | MACH_RCV_MSG,
        sizeof(msg.req), sizeof(msg.rep), rp, 5000, MACH_PORT_NULL);
    if (kr != MACH_MSG_SUCCESS) return kr;
    if (!(msg.rep.hdr.msgh_bits & MACH_MSGH_BITS_COMPLEX)) {
        typedef struct { mach_msg_header_t h; NDR_record_t ndr; kern_return_t ret; } err_reply;
        return ((err_reply *)&msg)->ret;
    }
    *port = msg.rep.serviceport.name;
    return KERN_SUCCESS;
}

#define SVC_NAME "com.panthera.test.xpc.e2e"
#define XPC_WIRE_MAGIC 0x58504321
#define XPC_MSG_ID     0x58504300

int main(void) {
    mach_port_t bp, svc_port, send_port;
    kern_return_t kr;
    int pass = 1;

    printf("test_xpc_e2e: starting\n");

    bp = bootstrap_port;
    if (bp == MACH_PORT_NULL)
        task_get_special_port(mach_task_self(), TASK_BOOTSTRAP_PORT, &bp);
    if (bp == MACH_PORT_NULL) {
        printf("SKIP: no bootstrap port\n");
        return 0;
    }
    printf("  bootstrap_port=%d\n", bp);

    /* === Test 1: Bootstrap check-in + look-up === */
    kr = raw_check_in(bp, SVC_NAME, &svc_port);
    if (kr != KERN_SUCCESS) {
        printf("FAIL: check_in: %d\n", kr);
        return 1;
    }
    printf("  check-in OK: port=%d\n", svc_port);

    kr = raw_look_up(bp, SVC_NAME, &send_port);
    if (kr != KERN_SUCCESS) {
        printf("FAIL: look_up: %d\n", kr);
        return 1;
    }
    printf("  look-up OK: port=%d\n", send_port);

    /* === Test 2: XPC serialization round-trip via Mach message === */

    /* Build an XPC dict, use xpc_connection_send_message to serialize
     * and send it through the looked-up port. Then receive raw on
     * the check-in port and verify the wire format. */

    /* Create a fake connection object pointing at the send port.
     * We need the internal struct layout for this. The connection tag
     * is 0xD000 (_XPC_TAG_CONNECTION). */
    struct {
        uint32_t tag;
        int32_t refcount;
        mach_port_t _reply_port;
        struct {
            mach_port_t send_port;
            mach_port_t recv_port;
            void *target_queue;
            void *event_handler;
            void *recv_source;
            char *service_name;
            void *pending_msg;
            uint8_t is_listener;
            uint8_t is_active;
            int32_t remote_pid;
        } conn;
    } fake_conn;
    memset(&fake_conn, 0, sizeof(fake_conn));
    fake_conn.tag = 0xD000;
    fake_conn.refcount = 100;
    fake_conn.conn.send_port = send_port;
    fake_conn.conn.is_active = 1;

    /* Build dict */
    xpc_object_t dict = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(dict, "message", "hello");
    xpc_dictionary_set_int64(dict, "code", 42);
    xpc_dictionary_set_bool(dict, "flag", 1);

    printf("  sending XPC dict (%zu entries)\n",
        xpc_dictionary_get_count(dict));

    /* Send through fake connection */
    xpc_connection_send_message(&fake_conn, dict);

    /* Receive raw on check-in port */
    struct {
        mach_msg_header_t hdr;
        char body[8192];
        char trailer[64];
    } rmsg;
    memset(&rmsg, 0, sizeof(rmsg));
    kr = mach_msg(&rmsg.hdr, MACH_RCV_MSG | MACH_RCV_TIMEOUT,
        0, sizeof(rmsg), svc_port, 2000, MACH_PORT_NULL);
    if (kr != MACH_MSG_SUCCESS) {
        printf("FAIL: receive: %d\n", kr);
        return 1;
    }

    printf("  received: size=%u id=0x%x\n", rmsg.hdr.msgh_size, rmsg.hdr.msgh_id);

    if (rmsg.hdr.msgh_id != XPC_MSG_ID) {
        printf("FAIL: bad msg_id\n");
        pass = 0;
    }

    size_t payload_len = rmsg.hdr.msgh_size - sizeof(mach_msg_header_t);
    uint32_t magic = *(uint32_t *)rmsg.body;
    if (magic != XPC_WIRE_MAGIC) {
        printf("FAIL: bad magic: 0x%x\n", magic);
        pass = 0;
    } else {
        printf("  wire magic: OK (0x%x)\n", magic);
    }

    uint32_t version = *(uint32_t *)(rmsg.body + 4);
    uint32_t entry_count = *(uint32_t *)(rmsg.body + 8);
    printf("  wire version=%u entries=%u payload=%zu bytes\n",
        version, entry_count, payload_len);

    if (entry_count != 3) {
        printf("FAIL: expected 3 entries, got %u\n", entry_count);
        pass = 0;
    }

    /* === Test 3: Deserialize back to XPC dict === */
    /* The library's deserialize is static, but we can test it indirectly
     * by using xpc_connection_send_message_with_reply_sync if we had
     * a two-process setup. For now, verify the wire format is correct
     * by parsing the entries manually. */

    const uint8_t *p = (const uint8_t *)rmsg.body + 16; /* skip header */
    const uint8_t *end = (const uint8_t *)rmsg.body + payload_len;
    int found_message = 0, found_code = 0, found_flag = 0;

    for (uint32_t i = 0; i < entry_count && p + 12 <= end; i++) {
        uint32_t type = *(const uint32_t *)p; p += 4;
        uint32_t key_plen = *(const uint32_t *)p; p += 4;
        const char *key = (const char *)p; p += key_plen;
        uint32_t val_len = *(const uint32_t *)p; p += 4;
        uint32_t val_pad = (val_len + 3) & ~3u;

        if (strcmp(key, "message") == 0 && type == 1) {
            if (strcmp((const char *)p, "hello") == 0)
                found_message = 1;
        } else if (strcmp(key, "code") == 0 && type == 2) {
            int64_t v; memcpy(&v, p, 8);
            if (v == 42) found_code = 1;
        } else if (strcmp(key, "flag") == 0 && type == 5) {
            if (p[0] == 1) found_flag = 1;
        }
        p += val_pad;
    }

    if (!found_message) { printf("FAIL: 'message' not found\n"); pass = 0; }
    if (!found_code)    { printf("FAIL: 'code' not found\n"); pass = 0; }
    if (!found_flag)    { printf("FAIL: 'flag' not found\n"); pass = 0; }

    if (found_message && found_code && found_flag)
        printf("  deserialization: all 3 values verified\n");

    xpc_release(dict);

    printf("test_xpc_e2e: %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
