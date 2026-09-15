/*
 * test_notify: verify notify_register_check + notify_post + notify_check
 * and notify_get_state / notify_set_state.
 */
#include <notify.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

static int failures = 0;

static void check(const char *name, int cond) {
    if (cond) {
        printf("  PASS: %s\n", name);
    } else {
        printf("  FAIL: %s\n", name);
        failures++;
    }
}

int main(void) {
    int token = -1;
    uint32_t status;
    int changed = 0;

    printf("=== notify test ===\n");

    /* Test 1: register_check */
    status = notify_register_check("com.panthera.test.notify", &token);
    printf("register: status=%u token=%d\n", status, token);
    check("register_check returns OK", status == NOTIFY_STATUS_OK);
    check("token is valid", token >= 0);

    /* Test 2: post */
    status = notify_post("com.panthera.test.notify");
    printf("post: status=%u\n", status);
    check("post returns OK", status == NOTIFY_STATUS_OK);

    /* Test 3: check — should show changed=1 */
    status = notify_check(token, &changed);
    printf("check: status=%u changed=%d\n", status, changed);
    check("check returns OK", status == NOTIFY_STATUS_OK);
    check("check shows changed=1", changed == 1);

    /* Test 4: check again — should show changed=0 (no new post) */
    status = notify_check(token, &changed);
    printf("check2: status=%u changed=%d\n", status, changed);
    check("second check returns OK", status == NOTIFY_STATUS_OK);
    check("second check shows changed=0", changed == 0);

    /* Test 5: set_state / get_state */
    status = notify_set_state(token, 0xDEADBEEF);
    printf("set_state: status=%u\n", status);
    check("set_state returns OK", status == NOTIFY_STATUS_OK);

    uint64_t state = 0;
    status = notify_get_state(token, &state);
    printf("get_state: status=%u state=0x%llx\n", status, (unsigned long long)state);
    check("get_state returns OK", status == NOTIFY_STATUS_OK);
    check("state == 0xDEADBEEF", state == 0xDEADBEEF);

    /* Test 6: is_valid_token */
    check("is_valid_token(token)", notify_is_valid_token(token));
    check("!is_valid_token(-1)", !notify_is_valid_token(-1));

    /* Test 7: cancel */
    status = notify_cancel(token);
    printf("cancel: status=%u\n", status);
    check("cancel returns OK", status == NOTIFY_STATUS_OK);

    printf("\n=== %d tests passed, %d failed ===\n",
           (failures == 0) ? 11 : 11 - failures, failures);
    return failures ? 1 : 0;
}
