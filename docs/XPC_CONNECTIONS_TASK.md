# XPC Connection Layer — Wire Up Mach IPC

## Context

Read `OS_BUILD_ROADMAP.md` for foundation rules.

The XPC object layer is complete (90 exports, dictionary/array/string/int64/etc, refcounting, 42 tests pass). The connection functions are currently stubs. This task makes them real.

**All the pieces exist:**
- XPC object model — `userland/libsystem/build/obj/libxpc_impl.c` (560 lines, working)
- Mach ports — kernel support working, `mach_port_allocate/insert_right/deallocate` all functional
- `mach_msg` — exported from libsystem_kernel, send/receive working
- libdispatch — 303 exports, `dispatch_source_create`, `dispatch_mach_create` available
- launchd bootstrap — `job_mig_check_in2` and `job_mig_look_up2` handle MIG requests today
- Blocks runtime — `_Block_copy`, `_Block_object_assign` available
- `tests/test_bootstrap.c` — proves raw MIG check-in/look-up/message round-trip works

## What to Implement

### 1. Serialization: XPC Dictionary ↔ Flat Buffer

XPC messages are sent as Mach messages with a serialized XPC dictionary as the payload.

**Wire format:**

```
┌─────────────────────────────┐
│ uint32_t magic  (0x58504321)│  "XPC!"
│ uint32_t version (1)        │
│ uint32_t entry_count        │
│ uint32_t total_payload_len  │
├─────────────────────────────┤
│ Entry 0:                    │
│   uint32_t type             │  XPC_TYPE_STRING, etc.
│   uint32_t key_len          │  including NUL, padded to 4-byte
│   char[]   key              │  NUL-terminated
│   uint32_t value_len        │
│   uint8_t[] value           │  padded to 4-byte boundary
├─────────────────────────────┤
│ Entry 1: ...                │
│ ...                         │
└─────────────────────────────┘
```

Value encoding by type:
- `STRING` — NUL-terminated bytes
- `INT64` — 8 bytes, little-endian
- `UINT64` — 8 bytes, little-endian
- `DOUBLE` — 8 bytes, IEEE 754
- `BOOL` — 1 byte (0 or 1)
- `DATA` — raw bytes
- `MACH_SEND` / `MACH_RECV` — NOT in payload. Sent as `mach_msg_port_descriptor_t` in the Mach message body. Record the key name so the receiver can associate the port with the right dictionary key.

Implement:
```c
/* Serialize dict to buffer. Returns malloc'd buffer and sets *out_len.
 * Mach port descriptors are collected in *ports / *port_keys for
 * the caller to attach to the Mach message. */
static void *xpc_serialize_dict(xpc_object_t dict, size_t *out_len,
    mach_port_t **ports, char ***port_keys, size_t *port_count);

/* Deserialize buffer back to XPC dictionary.
 * Port descriptors from the Mach message are passed in so they can
 * be associated with dictionary keys. */
static xpc_object_t xpc_deserialize_dict(const void *buf, size_t len,
    const mach_port_t *ports, const char *const *port_keys, size_t port_count);
```

### 2. xpc_connection_create_mach_service

```c
xpc_connection_t
xpc_connection_create_mach_service(const char *name,
    dispatch_queue_t targetq, uint64_t flags)
```

**Client mode** (flags == 0):
1. Call `bootstrap_look_up` via raw MIG to launchd's bootstrap port (use the same pattern as `raw_bootstrap_look_up()` in `tests/test_bootstrap.c` — it's proven working)
2. Receive a send right to the service's port
3. Allocate a local receive port for replies (`mach_port_allocate(MACH_PORT_RIGHT_RECEIVE, ...)`)
4. Store send port, reply port, target queue in the connection struct
5. Return the connection (not yet active — must call `xpc_connection_resume`)

**Listener mode** (flags & `XPC_CONNECTION_MACH_SERVICE_LISTENER`):
1. Call `bootstrap_check_in` via raw MIG to register the service
2. Receive the receive right for the service port
3. Store receive port and target queue
4. When resumed, create a dispatch source watching the receive port for incoming messages
5. For each incoming message, create a peer connection and call the event handler

**Connection struct:**
```c
struct xpc_connection_s {
    volatile int32_t refcount;
    mach_port_t send_port;       /* client: send messages here */
    mach_port_t recv_port;       /* replies come here / listener: accept here */
    dispatch_queue_t target_queue;
    void (^event_handler)(xpc_object_t);  /* Block */
    char *service_name;
    bool is_listener;
    bool is_active;
    pid_t remote_pid;
    mach_port_t reply_port;      /* for sync send+reply */
};
```

### 3. xpc_connection_set_event_handler

```c
void xpc_connection_set_event_handler(xpc_connection_t conn,
    xpc_handler_t handler)
{
    if (conn->event_handler)
        Block_release(conn->event_handler);
    conn->event_handler = Block_copy(handler);
}
```

### 4. xpc_connection_resume

```c
void xpc_connection_resume(xpc_connection_t conn)
{
    conn->is_active = true;
    
    if (conn->is_listener && conn->recv_port != MACH_PORT_NULL) {
        /* Create a dispatch source to watch for incoming Mach messages */
        dispatch_source_t src = dispatch_source_create(
            DISPATCH_SOURCE_TYPE_MACH_RECV,
            conn->recv_port, 0, conn->target_queue);
        
        dispatch_source_set_event_handler(src, ^{
            /* Receive the Mach message */
            struct {
                mach_msg_header_t header;
                char body[8192];
            } msg;
            
            mach_msg_return_t kr = mach_msg(&msg.header,
                MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof(msg),
                conn->recv_port, 0, MACH_PORT_NULL);
            
            if (kr != MACH_MSG_SUCCESS) return;
            
            /* Deserialize the payload to XPC dictionary */
            const void *payload = (const uint8_t *)&msg + sizeof(mach_msg_header_t);
            size_t payload_len = msg.header.msgh_size - sizeof(mach_msg_header_t);
            
            xpc_object_t xpc_msg = xpc_deserialize_dict(payload, payload_len,
                NULL, NULL, 0);
            
            if (xpc_msg && conn->event_handler) {
                /* Store reply port so xpc_dictionary_create_reply works */
                // ... attach msg.header.msgh_local_port as reply context
                conn->event_handler(xpc_msg);
                xpc_release(xpc_msg);
            }
        });
        
        dispatch_resume(src);
    }
}
```

### 5. xpc_connection_send_message

```c
void xpc_connection_send_message(xpc_connection_t conn, xpc_object_t msg)
{
    if (!conn->is_active || conn->send_port == MACH_PORT_NULL) return;
    
    /* Serialize the XPC dictionary */
    size_t payload_len = 0;
    void *payload = xpc_serialize_dict(msg, &payload_len, NULL, NULL, NULL);
    if (!payload) return;
    
    /* Build Mach message */
    size_t msg_size = sizeof(mach_msg_header_t) + payload_len;
    mach_msg_header_t *mmsg = calloc(1, msg_size);
    
    mmsg->msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
    mmsg->msgh_size = (mach_msg_size_t)msg_size;
    mmsg->msgh_remote_port = conn->send_port;
    mmsg->msgh_local_port = MACH_PORT_NULL;
    mmsg->msgh_id = 0x58504300; /* "XPC\0" */
    
    memcpy((uint8_t *)mmsg + sizeof(mach_msg_header_t), payload, payload_len);
    
    mach_msg(mmsg, MACH_SEND_MSG, (mach_msg_size_t)msg_size,
        0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    
    free(mmsg);
    free(payload);
}
```

### 6. xpc_connection_send_message_with_reply_sync

```c
xpc_object_t
xpc_connection_send_message_with_reply_sync(xpc_connection_t conn,
    xpc_object_t msg)
{
    if (!conn->is_active || conn->send_port == MACH_PORT_NULL)
        return (xpc_object_t)_xpc_error_connection_invalid;
    
    /* Allocate a reply port */
    mach_port_t reply_port = MACH_PORT_NULL;
    mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &reply_port);
    
    /* Serialize */
    size_t payload_len = 0;
    void *payload = xpc_serialize_dict(msg, &payload_len, NULL, NULL, NULL);
    
    /* Send with reply port */
    size_t msg_size = sizeof(mach_msg_header_t) + payload_len;
    mach_msg_header_t *send_msg = calloc(1, msg_size);
    send_msg->msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,
        MACH_MSG_TYPE_MAKE_SEND_ONCE);
    send_msg->msgh_size = (mach_msg_size_t)msg_size;
    send_msg->msgh_remote_port = conn->send_port;
    send_msg->msgh_local_port = reply_port;
    send_msg->msgh_id = 0x58504300;
    memcpy((uint8_t *)send_msg + sizeof(mach_msg_header_t), payload, payload_len);
    
    mach_msg(send_msg, MACH_SEND_MSG, (mach_msg_size_t)msg_size,
        0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    free(send_msg);
    free(payload);
    
    /* Blocking receive on reply port */
    struct {
        mach_msg_header_t header;
        char body[8192];
    } reply;
    
    mach_msg_return_t kr = mach_msg(&reply.header, MACH_RCV_MSG,
        0, sizeof(reply), reply_port,
        5000 /* 5s timeout */, MACH_PORT_NULL);
    
    mach_port_deallocate(mach_task_self(), reply_port);
    
    if (kr != MACH_MSG_SUCCESS)
        return (xpc_object_t)_xpc_error_connection_interrupted;
    
    /* Deserialize reply */
    const void *rpayload = (const uint8_t *)&reply + sizeof(mach_msg_header_t);
    size_t rpayload_len = reply.header.msgh_size - sizeof(mach_msg_header_t);
    
    return xpc_deserialize_dict(rpayload, rpayload_len, NULL, NULL, 0);
}
```

### 7. xpc_main

```c
_Noreturn void xpc_main(xpc_connection_handler_t handler)
{
    /* Get the service name from launchd environment or MachServices.
     * For Panthera, read from an environment variable set by launchd. */
    const char *service_name = getenv("XPC_SERVICE_NAME");
    if (!service_name) service_name = "com.panthera.unknown";
    
    /* Create a listener connection */
    xpc_connection_t listener = xpc_connection_create_mach_service(
        service_name, NULL, XPC_CONNECTION_MACH_SERVICE_LISTENER);
    
    if (!listener) {
        _exit(1);
    }
    
    /* Set the new-connection handler */
    xpc_connection_set_event_handler(listener, ^(xpc_object_t peer_event) {
        /* peer_event is actually a new connection in listener mode */
        xpc_connection_t peer = (xpc_connection_t)peer_event;
        handler(peer);
    });
    
    xpc_connection_resume(listener);
    
    /* Never returns */
    dispatch_main();
}
```

### 8. xpc_dictionary_create_reply

This needs to work with the connection infrastructure. When a message is received, the reply port from the Mach message header must be stored so `create_reply` can address the response:

```c
xpc_object_t xpc_dictionary_create_reply(xpc_object_t original)
{
    /* Create a new dictionary that, when sent via
     * xpc_connection_send_message, goes to the reply port
     * of the original message. */
    xpc_object_t reply = xpc_dictionary_create(NULL, NULL, 0);
    
    /* Copy the reply port from the original message's metadata */
    mach_port_t reply_port = /* extract from original's internal metadata */;
    /* Store reply_port in the reply dict's internal state */
    
    return reply;
}
```

You'll need to add a `_reply_port` field to the XPC dictionary struct, or attach it as metadata. Set it when deserializing incoming messages from the Mach header's `msgh_remote_port`.

## Bootstrap MIG Helpers

Use the same raw MIG pattern from `tests/test_bootstrap.c` (proven working). Extract into reusable functions:

```c
/* These are thin wrappers around mach_msg that speak the
 * job_mig protocol (subsystem 400). */
static kern_return_t
panthera_bootstrap_look_up(mach_port_t bp, const char *name, mach_port_t *port);

static kern_return_t
panthera_bootstrap_check_in(mach_port_t bp, const char *name, mach_port_t *port);
```

Copy the implementation from `tests/test_bootstrap.c` — the `raw_bootstrap_look_up()` and `raw_bootstrap_check_in()` functions work today.

## Testing

### Test 1: Serialization Round-Trip

```c
xpc_object_t dict = xpc_dictionary_create(NULL, NULL, 0);
xpc_dictionary_set_string(dict, "message", "hello");
xpc_dictionary_set_int64(dict, "code", 42);
xpc_dictionary_set_bool(dict, "flag", true);

size_t len;
void *buf = xpc_serialize_dict(dict, &len, NULL, NULL, NULL);
xpc_object_t restored = xpc_deserialize_dict(buf, len, NULL, NULL, 0);

assert(strcmp(xpc_dictionary_get_string(restored, "message"), "hello") == 0);
assert(xpc_dictionary_get_int64(restored, "code") == 42);
assert(xpc_dictionary_get_bool(restored, "flag") == true);
printf("Serialization: PASS\n");
```

### Test 2: XPC Service + Client (end-to-end)

**Service** (`tests/test_xpc_service.c`):
```c
#include <xpc/xpc.h>
#include <stdio.h>

static void handle_peer(xpc_connection_t peer) {
    xpc_connection_set_event_handler(peer, ^(xpc_object_t msg) {
        const char *greeting = xpc_dictionary_get_string(msg, "greeting");
        printf("[service] received: %s\n", greeting);
        
        xpc_object_t reply = xpc_dictionary_create_reply(msg);
        xpc_dictionary_set_string(reply, "response", "hello from XPC service");
        xpc_connection_send_message(peer, reply);
        xpc_release(reply);
    });
    xpc_connection_resume(peer);
}

int main(void) {
    xpc_main(handle_peer);
    return 0;
}
```

**Launchd plist** for test service:
```xml
<key>Label</key>
<string>com.panthera.test.xpc</string>
<key>ProgramArguments</key>
<array><string>/usr/bin/test_xpc_service</string></array>
<key>MachServices</key>
<dict>
    <key>com.panthera.test.xpc</key>
    <true/>
</dict>
<key>RunAtLoad</key>
<true/>
```

**Client** (`tests/test_xpc_client.c`):
```c
#include <xpc/xpc.h>
#include <stdio.h>

int main(void) {
    xpc_connection_t conn = xpc_connection_create_mach_service(
        "com.panthera.test.xpc", NULL, 0);
    xpc_connection_set_event_handler(conn, ^(xpc_object_t event) {
        printf("[client] event: %s\n", xpc_copy_description(event));
    });
    xpc_connection_resume(conn);
    
    xpc_object_t msg = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(msg, "greeting", "hello from client");
    
    xpc_object_t reply = xpc_connection_send_message_with_reply_sync(conn, msg);
    
    const char *response = xpc_dictionary_get_string(reply, "response");
    printf("[client] reply: %s\n", response);
    
    if (response && strcmp(response, "hello from XPC service") == 0)
        printf("XPC end-to-end: PASS\n");
    else
        printf("XPC end-to-end: FAIL\n");
    
    xpc_release(reply);
    xpc_release(msg);
    return 0;
}
```

**Run:** Boot with both service and client plists, or run the client manually from the shell while the service is running via launchd.

## Build

Add the new code to `libxpc_impl.c` (or split into `libxpc_connection.c` and `libxpc_serialize.c` for clarity). Rebuild:

```bash
bash userland/libsystem/build/relink_libSystem.sh  # or however libxpc is rebuilt
bash tools/build_shared_cache.sh
bash userland/libsystem/verify_exports.sh
bash rootfs/create_hfs_root_image.sh --force
```

## New Exports

The following symbols must be added (in addition to the existing 90):

```
_xpc_connection_create_mach_service
_xpc_connection_create
_xpc_connection_set_event_handler
_xpc_connection_resume
_xpc_connection_cancel
_xpc_connection_send_message
_xpc_connection_send_message_with_reply
_xpc_connection_send_message_with_reply_sync
_xpc_connection_get_pid
_xpc_connection_set_target_queue
_xpc_main
_xpc_string_create
_xpc_string_get_string_ptr
_xpc_int64_create
_xpc_int64_get_value
_xpc_uint64_create
_xpc_double_create
_xpc_double_get_value
_xpc_bool_create
_xpc_data_create
_xpc_data_get_bytes_ptr
_xpc_data_get_length
_xpc_null_create
_xpc_array_get_count
_xpc_array_get_value
_xpc_dictionary_apply
_xpc_dictionary_get_bool
_xpc_dictionary_set_bool
_xpc_dictionary_set_data
_xpc_dictionary_get_data
_xpc_dictionary_get_count
```

## Rules

- Read `OS_BUILD_ROADMAP.md` foundation rules
- libxpc is UNFROZEN for this task
- Use raw MIG for bootstrap operations (copy from `tests/test_bootstrap.c`)
- Use `dispatch_source_create(DISPATCH_SOURCE_TYPE_MACH_RECV, ...)` for async receive
- Use `Block_copy` / `Block_release` for handler blocks
- All existing 90 symbols must remain compatible
- Boot test and read output — don't ask the user
- No deferred work — serialization + connection + xpc_main must all work

## Success Criteria

1. Serialization round-trip test passes (dict → buffer → dict, values match)
2. `xpc_connection_create_mach_service` successfully looks up a launchd service
3. `xpc_connection_send_message_with_reply_sync` does a Mach message round-trip
4. `xpc_main` hosts a service that receives and replies to messages
5. End-to-end test: client sends "hello", service receives, replies "hello from XPC service", client reads reply
6. All existing tests still pass (test_bootstrap, test_mach_ipc, test_xpc objects)
7. System boots, all services start, SSH works
