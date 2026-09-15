# Task 3: Real XPC Implementation for Panthera

## Context

Read `OS_BUILD_ROADMAP.md` for foundation rules.

CoreFoundation is live (1,562 exports). launchd uses CFPropertyList for plist parsing. Mach IPC works (bootstrap server, port allocation, mach_msg). libdispatch has a full `dispatch_mach_*` API surface designed for XPC. The Blocks runtime (`_Block_copy`, `_Block_object_assign`) is available.

**Permission granted:** libxpc.dylib is UNFROZEN for this task. Replace the 177-line stub with a real implementation.

## What Exists

Current: `userland/libsystem/build/obj/libxpc_stubs.c` (177 lines) — every function returns NULL/0/no-op.

32 exported symbols:
- 4 type constants (`_xpc_bool_true`, `_xpc_type_bool`, `_xpc_type_dictionary`, `_xpc_type_uint64`)
- 14 dictionary functions (all return NULL/0)
- 5 array functions (all return NULL/0)
- 9 utility functions (retain, release, get_type, bool_get_value, etc.)

**Missing entirely:** `xpc_connection_*`, `xpc_main`, `xpc_string_create`, `xpc_int64_create`, `xpc_data_create`, `xpc_pipe_*`, error types.

## Architecture

XPC is three layers:

```
Layer 1: XPC Objects — typed value containers (dictionary, array, string, int64, etc.)
Layer 2: XPC Connections — bidirectional Mach IPC channels between processes
Layer 3: XPC Services — launchd-managed on-demand services via xpc_main()
```

### Layer 1: XPC Object Model

Every XPC object is a refcounted typed container. Internally, use a simple tagged struct:

```c
#define XPC_TYPE_NULL       0x1000
#define XPC_TYPE_BOOL       0x2000
#define XPC_TYPE_INT64      0x3000
#define XPC_TYPE_UINT64     0x4000
#define XPC_TYPE_DOUBLE     0x5000
#define XPC_TYPE_STRING     0x6000
#define XPC_TYPE_DATA       0x7000
#define XPC_TYPE_DICTIONARY 0x8000
#define XPC_TYPE_ARRAY      0x9000
#define XPC_TYPE_FD         0xA000
#define XPC_TYPE_MACH_SEND  0xB000
#define XPC_TYPE_MACH_RECV  0xC000
#define XPC_TYPE_ERROR      0xF000

typedef struct xpc_object_s {
    uint32_t type;
    volatile int32_t refcount;
    union {
        bool bool_val;
        int64_t int64_val;
        uint64_t uint64_val;
        double double_val;
        struct { char *ptr; size_t len; } string;
        struct { uint8_t *ptr; size_t len; } data;
        mach_port_t port;
        int fd;
        struct {
            /* Key-value storage: parallel arrays for simplicity */
            char **keys;
            struct xpc_object_s **values;
            size_t count;
            size_t capacity;
        } dict;
        struct {
            struct xpc_object_s **values;
            size_t count;
            size_t capacity;
        } array;
    };
} *xpc_object_t;
```

**Don't use CFDictionary internally.** XPC objects need to serialize to/from Mach messages. A custom struct is simpler and avoids circular dependency (CF depends on libSystem which re-exports libxpc).

### Layer 2: XPC Connections

An XPC connection wraps a Mach port pair + dispatch queue:

```c
typedef struct xpc_connection_s {
    volatile int32_t refcount;
    mach_port_t send_port;      /* send right — messages go here */
    mach_port_t recv_port;      /* receive right — replies come here */
    dispatch_queue_t queue;     /* event handler dispatch queue */
    xpc_handler_t handler;      /* event handler block */
    char *service_name;         /* Mach service name (for bootstrap) */
    bool active;                /* resume'd? */
    bool is_listener;           /* listening for new connections? */
    pid_t remote_pid;
} *xpc_connection_t;
```

### Layer 3: Wire Format (Mach Message Serialization)

XPC dictionaries are serialized into Mach message payloads using a simple TLV encoding:

```
Message layout:
  mach_msg_header_t   (standard Mach header)
  uint32_t            xpc_magic (0x58504321 = "XPC!")  
  uint32_t            xpc_version (1)
  uint32_t            payload_length
  payload:
    For each key-value pair:
      uint32_t  type        (XPC_TYPE_*)
      uint32_t  key_len     (including NUL)
      char[]    key         (NUL-terminated, padded to 4-byte boundary)
      uint32_t  value_len
      uint8_t[] value       (padded to 4-byte boundary)
    
    Special types:
      MACH_SEND/RECV — sent as OOL port descriptors in the Mach message body
      FD — not supported initially
```

For Mach port rights, use `mach_msg_port_descriptor_t` in a complex Mach message (set `MACH_MSGH_BITS_COMPLEX`).

## Implementation Plan

### Step 1: XPC Object Layer (~200 lines)

Implement in `userland/libsystem/build/obj/libxpc_impl.c` (new file, replaces libxpc_stubs.c):

**Create functions:**
- `xpc_null_create()` → returns singleton null
- `xpc_bool_create(bool)` → returns singleton true/false
- `xpc_int64_create(int64_t)` → malloc + fill
- `xpc_uint64_create(uint64_t)` → malloc + fill
- `xpc_double_create(double)` → malloc + fill
- `xpc_string_create(const char *)` → malloc + strdup
- `xpc_data_create(const void *, size_t)` → malloc + memcpy
- `xpc_dictionary_create(keys, values, count)` → malloc parallel arrays
- `xpc_array_create(objects, count)` → malloc array

**Dictionary operations:**
- `xpc_dictionary_set_value(dict, key, value)` → grow if needed, retain value
- `xpc_dictionary_get_value(dict, key)` → linear search by key
- `xpc_dictionary_set_string/int64/uint64/bool/data/mach_send/mach_recv()` — convenience wrappers
- `xpc_dictionary_get_string/int64/uint64/bool/data()` — convenience unwrappers
- `xpc_dictionary_copy_mach_send(dict, key)` — returns port with extra send right
- `xpc_dictionary_get_audit_token(dict, token)` — from received message header
- `xpc_dictionary_create_reply(original)` — creates reply dict with reply port
- `xpc_dictionary_apply(dict, applier)` — iterate key-value pairs

**Array operations:**
- `xpc_array_append_value(array, value)` → grow + retain
- `xpc_array_get_count(array)`
- `xpc_array_get_value(array, index)`
- `xpc_array_set_string/uint64()` — convenience

**Lifecycle:**
- `xpc_retain(obj)` → atomic increment
- `xpc_release(obj)` → atomic decrement, free on zero
- `xpc_get_type(obj)` → return type pointer
- `xpc_copy_description(obj)` → asprintf a debug string
- `xpc_equal(a, b)` → type check + value compare
- `xpc_hash(obj)` → hash for use as dictionary keys

**Type constants:**
```c
const struct xpc_type_s _xpc_type_dictionary_s = { XPC_TYPE_DICTIONARY };
const struct xpc_type_s _xpc_type_bool_s = { XPC_TYPE_BOOL };
const struct xpc_type_s _xpc_type_uint64_s = { XPC_TYPE_UINT64 };
const void *_xpc_type_dictionary = &_xpc_type_dictionary_s;
const void *_xpc_type_bool = &_xpc_type_bool_s;
const void *_xpc_type_uint64 = &_xpc_type_uint64_s;
const void *_xpc_bool_true = ...; /* singleton true object */
```

**Error constants:**
```c
const struct xpc_object_s _xpc_error_connection_interrupted_s = { .type = XPC_TYPE_ERROR };
const struct xpc_object_s _xpc_error_connection_invalid_s = { .type = XPC_TYPE_ERROR };
const void *_xpc_error_connection_interrupted = &_xpc_error_connection_interrupted_s;
const void *_xpc_error_connection_invalid = &_xpc_error_connection_invalid_s;
```

### Step 2: Serialization (~150 lines)

```c
/* Serialize an XPC dictionary to a flat buffer for Mach message payload */
static size_t xpc_serialize(xpc_object_t dict, void **out_buf);

/* Deserialize a flat buffer back to an XPC dictionary */
static xpc_object_t xpc_deserialize(const void *buf, size_t len);
```

Port rights are not serialized into the buffer — they're sent as Mach message port descriptors (the kernel transfers the rights). Track them separately during serialization.

### Step 3: Connection Layer (~250 lines)

```c
xpc_connection_t
xpc_connection_create_mach_service(const char *name,
    dispatch_queue_t targetq, uint64_t flags)
{
    xpc_connection_t conn = calloc(1, sizeof(struct xpc_connection_s));
    conn->refcount = 1;
    conn->service_name = strdup(name);
    conn->queue = targetq ? targetq : dispatch_get_main_queue();
    dispatch_retain(conn->queue);
    
    if (flags & XPC_CONNECTION_MACH_SERVICE_LISTENER) {
        /* Server: check in with launchd to get receive right */
        mach_port_t port;
        /* Use raw MIG bootstrap_check_in (same pattern as test_bootstrap.c) */
        kern_return_t kr = raw_bootstrap_check_in(bootstrap_port, name, &port);
        if (kr == KERN_SUCCESS) {
            conn->recv_port = port;
            conn->is_listener = true;
        }
    } else {
        /* Client: look up the service to get send right */
        mach_port_t port;
        kern_return_t kr = raw_bootstrap_look_up(bootstrap_port, name, &port);
        if (kr == KERN_SUCCESS) {
            conn->send_port = port;
        }
        /* Allocate reply port for receiving responses */
        mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &conn->recv_port);
    }
    
    return conn;
}

void xpc_connection_set_event_handler(xpc_connection_t conn,
    xpc_handler_t handler)
{
    conn->handler = Block_copy(handler);
}

void xpc_connection_resume(xpc_connection_t conn)
{
    conn->active = true;
    if (conn->is_listener) {
        /* Start listening for incoming connections on recv_port.
         * Use dispatch_source with DISPATCH_SOURCE_TYPE_MACH_RECV
         * to wake when messages arrive. */
        dispatch_source_t src = dispatch_source_create(
            DISPATCH_SOURCE_TYPE_MACH_RECV, conn->recv_port, 0, conn->queue);
        dispatch_source_set_event_handler(src, ^{
            /* Receive message, deserialize, create peer connection, call handler */
            // ... mach_msg receive, xpc_deserialize, dispatch to handler
        });
        dispatch_resume(src);
    }
}

void xpc_connection_send_message(xpc_connection_t conn, xpc_object_t msg)
{
    /* Serialize dict → buffer, wrap in mach_msg, send to conn->send_port */
    void *buf = NULL;
    size_t len = xpc_serialize(msg, &buf);
    
    struct {
        mach_msg_header_t header;
        /* payload follows */
    } *mmsg = malloc(sizeof(mach_msg_header_t) + len);
    
    mmsg->header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
    mmsg->header.msgh_size = sizeof(mach_msg_header_t) + len;
    mmsg->header.msgh_remote_port = conn->send_port;
    mmsg->header.msgh_local_port = MACH_PORT_NULL;
    mmsg->header.msgh_id = 0x58504300; /* "XPC\0" */
    memcpy((uint8_t*)mmsg + sizeof(mach_msg_header_t), buf, len);
    
    mach_msg(&mmsg->header, MACH_SEND_MSG, mmsg->header.msgh_size,
        0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    
    free(mmsg);
    free(buf);
}

xpc_object_t
xpc_connection_send_message_with_reply_sync(xpc_connection_t conn,
    xpc_object_t msg)
{
    /* Send with reply port, then blocking receive on reply port */
    // ... similar to send_message but set msgh_local_port = conn->recv_port
    // ... then mach_msg MACH_RCV_MSG on conn->recv_port
    // ... deserialize reply
}
```

### Step 4: xpc_main() (~50 lines)

```c
void xpc_main(xpc_connection_handler_t handler)
{
    /* Register as a service, listen for connections, dispatch to handler.
     * This function never returns. */
    
    /* The service name comes from the launchd plist's MachServices key.
     * For now, get it from an environment variable or the process label. */
    
    /* Create a listener connection */
    // ... check in with launchd
    // ... set up dispatch_source for incoming messages
    // ... call handler for each new peer connection
    
    /* Run the dispatch main queue forever */
    dispatch_main();
}
```

## Build

Replace the stubs file and relink:

```bash
# Compile the new implementation
CC="$(xcrun -find clang)"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
SYSROOT="userland/libsystem/build/sysroot"

$CC -target x86_64-apple-darwin23.0 -mmacosx-version-min=14.0 \
    -isysroot $SDKROOT \
    -I$SYSROOT/usr/include \
    -c userland/libsystem/build/obj/libxpc_impl.c \
    -o userland/libsystem/build/obj/libxpc_impl.o

# Link as dylib
LD="$(xcrun -find ld)"
$LD -arch x86_64 \
    -dylib -install_name /usr/lib/system/libxpc.dylib \
    -o $SYSROOT/usr/lib/system/libxpc.dylib \
    userland/libsystem/build/obj/libxpc_impl.o \
    -lSystem -L$SYSROOT/usr/lib
```

**Important:** The new libxpc must export AT LEAST the same 32 symbols as the current stub, plus the new ones. Don't break existing binaries that link against libxpc.

After linking:
```bash
bash userland/libsystem/build/relink_libSystem.sh
bash tools/build_shared_cache.sh
bash userland/libsystem/verify_exports.sh
```

## Testing

### Test 1: XPC Object Operations (no IPC)

```c
#include <xpc/xpc.h>

int main(void) {
    /* Dictionary */
    xpc_object_t dict = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(dict, "name", "Panthera");
    xpc_dictionary_set_int64(dict, "version", 1);
    xpc_dictionary_set_uint64(dict, "year", 2026);
    
    const char *name = xpc_dictionary_get_string(dict, "name");
    int64_t ver = xpc_dictionary_get_int64(dict, "version");
    printf("name=%s version=%lld\n", name, ver);
    
    /* Array */
    xpc_object_t arr = xpc_array_create(NULL, 0);
    xpc_array_append_value(arr, xpc_string_create("hello"));
    xpc_array_append_value(arr, xpc_int64_create(42));
    printf("array count=%zu\n", xpc_array_get_count(arr));
    
    /* Description */
    char *desc = xpc_copy_description(dict);
    printf("dict: %s\n", desc);
    free(desc);
    
    xpc_release(arr);
    xpc_release(dict);
    printf("XPC objects: PASS\n");
    return 0;
}
```

### Test 2: XPC Service + Client (requires Mach IPC)

Create a launchd plist for a test service, have the service call `xpc_main()`, and have a client connect and exchange a message. Use the raw bootstrap check-in/look-up pattern from `tests/test_bootstrap.c` as reference.

## Headers

Create `userland/libsystem/build/sysroot/usr/include/xpc/xpc.h` with the public XPC API declarations. Include the type definitions, function prototypes, and block type definitions. Reference Apple's public `<xpc/xpc.h>` for the API surface.

Also create `xpc/base.h`, `xpc/connection.h`, `xpc/endpoint.h` as needed.

## Symbol Compatibility

The new library MUST export these existing symbols (don't break them):
```
_xpc_bool_true, _xpc_type_bool, _xpc_type_dictionary, _xpc_type_uint64
_xpc_dictionary_create, _xpc_dictionary_create_reply
_xpc_dictionary_get_value, _xpc_dictionary_get_string, _xpc_dictionary_get_int64
_xpc_dictionary_get_uint64, _xpc_dictionary_get_audit_token
_xpc_dictionary_set_value, _xpc_dictionary_set_string, _xpc_dictionary_set_int64
_xpc_dictionary_set_uint64, _xpc_dictionary_set_mach_send, _xpc_dictionary_set_mach_recv
_xpc_dictionary_copy_mach_send
_xpc_array_create, _xpc_array_append_value, _xpc_array_set_string, _xpc_array_set_uint64
_xpc_retain, _xpc_release, _xpc_get_type
_xpc_bool_get_value, _xpc_uint64_get_value
_xpc_copy_description, _xpc_copy_entitlements_for_pid, _xpc_strerror
```

Plus new symbols for connections, services, and additional types.

## Rules

- Read `OS_BUILD_ROADMAP.md` foundation rules
- libxpc is UNFROZEN for this task
- Don't use CF types internally in XPC objects (avoid circular dependency — CF is in the same shared cache and re-export chain)
- Use Mach IPC directly (mach_msg, mach_port_*) for connections
- Use dispatch for async event handling
- Use Block_copy/Block_release for handler blocks
- The new implementation must export ALL 32 existing symbols with compatible behavior
- After changes: relink_libSystem → build_shared_cache → verify_exports
- Boot test and read output — don't ask the user
- No deferred work — at minimum, XPC object operations must work. Connection/service support is the stretch goal.

## Success Criteria

**Minimum (must have):**
- XPC dictionary create/set/get works with string, int64, uint64, bool, data
- XPC array create/append/get works
- xpc_retain/xpc_release/xpc_get_type work correctly
- xpc_copy_description returns a readable debug string
- All 32 existing symbols still exported and compatible
- System boots, all services start, no regressions

**Stretch (should have):**
- xpc_connection_create_mach_service connects to launchd services
- xpc_connection_send_message sends serialized XPC dict over Mach port
- xpc_connection_send_message_with_reply_sync does round-trip
- xpc_main() hosts a service that responds to messages
- Test service/client pair exchanges messages through launchd bootstrap

## Deliverables

Report:
1. How many XPC API functions implemented
2. Wire format specification (how dicts are serialized)
3. Test results (object operations + connection test if implemented)
4. Symbol count comparison (old 32 vs new)
5. Boot test results
