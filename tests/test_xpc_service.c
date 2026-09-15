/*
 * test_xpc_service.c — XPC service that receives messages and replies.
 * Uses xpc_main() to register as a listener.
 */
#include <xpc/xpc.h>
#include <stdio.h>
#include <string.h>

static void handle_peer(xpc_object_t peer) {
    xpc_connection_set_event_handler(peer, ^(xpc_object_t msg) {
        const char *greeting = xpc_dictionary_get_string(msg, "greeting");
        if (greeting)
            printf("[xpc_service] received: %s\n", greeting);

        xpc_object_t reply = xpc_dictionary_create_reply(msg);
        xpc_dictionary_set_string(reply, "response",
            "hello from XPC service");
        xpc_dictionary_set_int64(reply, "code", 200);
        xpc_connection_send_message(peer, reply);
        xpc_release(reply);
    });
    xpc_connection_resume(peer);
}

int main(void) {
    printf("[xpc_service] starting\n");
    fflush(stdout);
    xpc_main(handle_peer);
    return 0;
}
