/* Panthera shim: notify_probes.h — DTrace probe stubs */
#ifndef _NOTIFY_PROBES_H
#define _NOTIFY_PROBES_H

/* All DTrace probes are no-ops; all ENABLED checks return false */
#define NOTIFY_POST(...)                        ((void)0)
#define NOTIFY_POST_ENABLED()                   (0)
#define NOTIFY_REGISTER(...)                    ((void)0)
#define NOTIFY_REGISTER_ENABLED()               (0)
#define NOTIFY_REGISTER_CHECK(...)              ((void)0)
#define NOTIFY_REGISTER_CHECK_ENABLED()         (0)
#define NOTIFY_REGISTER_SIGNAL(...)             ((void)0)
#define NOTIFY_REGISTER_SIGNAL_ENABLED()        (0)
#define NOTIFY_REGISTER_MACH_PORT(...)          ((void)0)
#define NOTIFY_REGISTER_MACH_PORT_ENABLED()     (0)
#define NOTIFY_REGISTER_FILE_DESCRIPTOR(...)    ((void)0)
#define NOTIFY_REGISTER_FILE_DESCRIPTOR_ENABLED() (0)
#define NOTIFY_REGISTER_DISPATCH(...)           ((void)0)
#define NOTIFY_REGISTER_DISPATCH_ENABLED()      (0)
#define NOTIFY_REGISTER_PLAIN(...)              ((void)0)
#define NOTIFY_REGISTER_PLAIN_ENABLED()         (0)
#define NOTIFY_CHECK(...)                       ((void)0)
#define NOTIFY_CHECK_ENABLED()                  (0)
#define NOTIFY_CANCEL(...)                      ((void)0)
#define NOTIFY_CANCEL_ENABLED()                 (0)
#define NOTIFY_DELIVER_START(...)               ((void)0)
#define NOTIFY_DELIVER_START_ENABLED()          (0)
#define NOTIFY_DELIVER_END(...)                 ((void)0)
#define NOTIFY_DELIVER_END_ENABLED()            (0)
#define NOTIFY_SUSPEND(...)                     ((void)0)
#define NOTIFY_SUSPEND_ENABLED()                (0)
#define NOTIFY_RESUME(...)                      ((void)0)
#define NOTIFY_RESUME_ENABLED()                 (0)
#define NOTIFY_SET_STATE(...)                   ((void)0)
#define NOTIFY_SET_STATE_ENABLED()              (0)
#define NOTIFY_GET_STATE(...)                   ((void)0)
#define NOTIFY_GET_STATE_ENABLED()              (0)

#endif /* _NOTIFY_PROBES_H */
