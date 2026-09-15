/*
 * test_xpc_client.c — XPC client that connects to test_xpc_service,
 * sends a dictionary, and verifies the reply.
 *
 * Run from the shell after the service is running:
 *   /usr/bin/test_xpc_client
 */
#include <xpc/xpc.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    printf("[xpc_client] connecting to com.panthera.test.xpc\n");

    /* Retry look-up — service may not have checked in yet.
     * Use busy-wait since sleep/usleep may not work on Panthera. */
    xpc_object_t conn = NULL;
    for (int attempt = 0; attempt < 20; attempt++) {
        conn = xpc_connection_create_mach_service(
            "com.panthera.test.xpc", NULL, 0);
        if (conn) break;
        /* Busy-wait ~100ms equivalent */
        for (volatile int i = 0; i < 5000000; i++);
    }
    if (!conn) {
        printf("[xpc_client] FAIL: could not connect after retries\n");
        return 1;
    }

    xpc_connection_set_event_handler(conn, ^(xpc_object_t event) {
        (void)event;
    });
    xpc_connection_resume(conn);

    /* Build request */
    xpc_object_t msg = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(msg, "greeting", "hello from client");
    xpc_dictionary_set_int64(msg, "seq", 1);

    printf("[xpc_client] sending message\n");

    /* Synchronous send + reply */
    xpc_object_t reply = xpc_connection_send_message_with_reply_sync(
        conn, msg);

    if (!reply || reply == XPC_ERROR_CONNECTION_INTERRUPTED ||
        reply == XPC_ERROR_CONNECTION_INVALID) {
        printf("[xpc_client] FAIL: no reply or error\n");
        xpc_release(msg);
        xpc_release(conn);
        return 1;
    }

    const char *response = xpc_dictionary_get_string(reply, "response");
    int64_t code = xpc_dictionary_get_int64(reply, "code");

    printf("[xpc_client] reply: %s (code=%lld)\n",
        response ? response : "(null)", (long long)code);

    int pass = (response &&
        strcmp(response, "hello from XPC service") == 0 &&
        code == 200);

    printf("XPC end-to-end: %s\n", pass ? "PASS" : "FAIL");

    xpc_release(reply);
    xpc_release(msg);
    xpc_release(conn);
    return pass ? 0 : 1;
}
