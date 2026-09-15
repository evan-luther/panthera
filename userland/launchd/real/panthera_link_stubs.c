/*
 * panthera_link_stubs.c — Link-time stubs for symbols that are
 * #if-guarded out of core.c but still referenced from non-guarded code.
 *
 * These are no-op implementations that satisfy the linker.
 */

#include <mach/mach.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <xpc/xpc.h>

/* Forward declarations of types from core.c */
typedef struct job_s *job_t;
typedef struct jobmgr_s *jobmgr_t;

/* ================================================================
 * XPC Event System stubs — guarded by #if HAVE_XPC_EVENTS
 * ================================================================ */

struct eventsystem;
struct externalevent;

void eventsystem_ping(void) { }

void eventsystem_setup(void *obj, const char *key, void *context) {
    (void)obj; (void)key; (void)context;
}

void externalevent_delete(struct externalevent *ee) {
    (void)ee;
}

struct externalevent *externalevent_find(const char *sysname, uint64_t id) {
    (void)sysname; (void)id;
    return NULL;
}

/* XPC event/process demux — called from runtime.c event loop */
bool xpc_event_demux(mach_port_t p, xpc_object_t request, xpc_object_t *reply) {
    (void)p; (void)request; (void)reply;
    return false;
}

bool xpc_process_demux(mach_port_t p, xpc_object_t request, xpc_object_t *reply) {
    (void)p; (void)request; (void)reply;
    return false;
}

/* ================================================================
 * waiting4attach stubs — guarded by #if HAVE_XPC_EVENTS
 * ================================================================ */

struct waiting4attach;

struct waiting4attach *waiting4attach_new(jobmgr_t jm, const char *name,
    mach_port_t port, pid_t dest, void *type)
{
    (void)jm; (void)name; (void)port; (void)dest; (void)type;
    return NULL;
}

void waiting4attach_delete(jobmgr_t jm, struct waiting4attach *w4a) {
    (void)jm; (void)w4a;
}

struct waiting4attach *waiting4attach_find(jobmgr_t jm, job_t j) {
    (void)jm; (void)j;
    return NULL;
}

/* ================================================================
 * Exception server stubs — the old exc MIG server (non-mach_ variant)
 * runtime.c provides catch_mach_exception_raise* but exc MIG
 * also needs the non-mach variant.
 * ================================================================ */

kern_return_t catch_exception_raise(
    mach_port_t exception_port,
    mach_port_t thread, mach_port_t task,
    exception_type_t exception,
    exception_data_t code,
    mach_msg_type_number_t codeCnt)
{
    (void)exception_port; (void)thread; (void)task;
    (void)exception; (void)code; (void)codeCnt;
    mach_port_deallocate(mach_task_self(), thread);
    mach_port_deallocate(mach_task_self(), task);
    return KERN_SUCCESS;
}

kern_return_t catch_exception_raise_state(
    mach_port_t exception_port,
    exception_type_t exception,
    const exception_data_t code,
    mach_msg_type_number_t codeCnt,
    int *flavor,
    const thread_state_t old_state,
    mach_msg_type_number_t old_stateCnt,
    thread_state_t new_state,
    mach_msg_type_number_t *new_stateCnt)
{
    (void)exception_port; (void)exception; (void)code; (void)codeCnt;
    (void)flavor;
    memcpy(new_state, old_state, old_stateCnt * sizeof(old_state[0]));
    *new_stateCnt = old_stateCnt;
    return KERN_SUCCESS;
}

kern_return_t catch_exception_raise_state_identity(
    mach_port_t exception_port,
    mach_port_t thread, mach_port_t task,
    exception_type_t exception,
    exception_data_t code,
    mach_msg_type_number_t codeCnt,
    int *flavor,
    thread_state_t old_state,
    mach_msg_type_number_t old_stateCnt,
    thread_state_t new_state,
    mach_msg_type_number_t *new_stateCnt)
{
    (void)exception_port; (void)exception; (void)code; (void)codeCnt;
    (void)flavor;
    memcpy(new_state, old_state, old_stateCnt * sizeof(old_state[0]));
    *new_stateCnt = old_stateCnt;
    mach_port_deallocate(mach_task_self(), thread);
    mach_port_deallocate(mach_task_self(), task);
    return KERN_SUCCESS;
}

/* ================================================================
 * Responsibility stubs
 * ================================================================ */

void responsibility_init2(void) { }

/* ================================================================
 * XPC call wakeup — used by XPC domain code
 * ================================================================ */

void xpc_call_wakeup(mach_port_t port, int status) {
    (void)port; (void)status;
}

/* ================================================================
 * Quarantine stubs — qtn_proc_to_data from libvproc
 * ================================================================ */

void *qtn_proc_to_data(void) { return NULL; }

/* ================================================================
 * UUID stubs — if not provided by sysroot
 * ================================================================ */

void uuid_clear(unsigned char uuid[16]) {
    memset(uuid, 0, 16);
}

int uuid_is_null(const unsigned char uuid[16]) {
    unsigned char zero[16] = {0};
    return memcmp(uuid, zero, 16) == 0;
}
