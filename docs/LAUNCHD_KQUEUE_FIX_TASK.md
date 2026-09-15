# Fix launchd Service Dispatch — kqueue Demand Thread + Job Start

## Context

Read `OS_BUILD_ROADMAP.md` for foundation rules.

Apple's real launchd (`runtime.c` + `core.c`) boots on Panthera. It enters the event loop, loads 7 plists via `job_import()`, but services never start. The `launchd_all_stubs.c` shim is preserved as a fallback (`PANTHERA_LAUNCHD_REAL=0`).

## Architecture: How Apple's launchd Dispatches Jobs

Apple's launchd has TWO threads and THREE event paths:

### Thread 1: Main thread — `launchd_runtime()` → `launchd_runtime2()`

This is a tight loop calling `xpc_pipe_try_receive()` on `ipc_port_set`. It handles:
- XPC messages (from xpc_connection clients)
- MIG messages (bootstrap check_in/look_up via `launchd_mig_demux`)

The `xpc_pipe_try_receive()` implementation (in `panthera_xpc_pipe.c`) does a `mach_msg(MACH_RCV_MSG)` receive on the port set, then dispatches through the MIG demux. This loop blocks on mach_msg waiting for messages.

### Thread 2: Demand thread — `kqueue_demand_loop()` (runtime.c:518)

Created by `runtime_init()` via `pthread_create()`. This thread:
1. Calls `select(mainkq + 1, &rfds, NULL, NULL, NULL)` — blocks until kqueue has events
2. When select returns, calls `handle_kqueue(launchd_internal_port, mainkq)`
3. `handle_kqueue` → `x_handle_kqueue()` → `kevent()` to drain events → dispatches via `kq_callback` function pointers

This thread handles ALL kqueue events:
- `EVFILT_PROC` (NOTE_EXIT) — child process exits → `job_callback()` → `job_reap()`
- `EVFILT_MACHPORT` — Mach port set events (redundant with main thread's mach_msg)
- `EVFILT_TIMER` — scheduled job timers
- `EVFILT_READ/WRITE` — socket activation

### The Fork Path: `job_start()` → `runtime_fork()`

When `job_dispatch()` decides a job should start:
1. `job_start(j)` at core.c:1789
2. Creates a `fork_fd` pipe for parent-child synchronization
3. Calls `runtime_fork(bsport)` at runtime.c:637
4. `runtime_fork()` calls `fork()`, sets up bootstrap port in child, returns
5. Parent registers `EVFILT_PROC` with `NOTE_EXIT|NOTE_FORK|NOTE_EXEC` on the child's PID (core.c:1799-1808)
6. Child calls `job_start_child(j)` → `execve()`
7. When child exits, kqueue fires `EVFILT_PROC` → demand thread drains it → `job_callback()` → `job_reap()`

## What's Broken

### Problem 1: Demand thread may not start

`runtime_init()` at line 202 calls:
```c
os_assert_zero(pthread_create(&kqueue_demand_thread, NULL, kqueue_demand_loop, NULL));
os_assert_zero(pthread_detach(kqueue_demand_thread));
```

Panthera's `pthread_create` works (threads execute, test_pthread showed mutex/condvar working) BUT `pthread_join` returns ESRCH and child threads may have TLS issues. If the demand thread crashes immediately due to TLS, the kqueue never gets serviced.

**Debug:** Add a trace at the start of `kqueue_demand_loop()`:
```c
static void *kqueue_demand_loop(void *arg) {
    panthera_launchd_trace("launchd: demand thread started\n");
    // ...
```

### Problem 2: `select()` on kqueue fd

The demand loop uses `select(mainkq + 1, &rfds, ...)` to wait for kqueue readability. The `select()` FD_SET fix was applied (the `__darwin_check_fd_set_overflow` fix), so this should work now. But verify:

**Debug:** Add trace inside the loop:
```c
for (;;) {
    FD_ZERO(&rfds);
    FD_SET(mainkq, &rfds);
    panthera_launchd_trace("launchd: demand thread select() waiting\n");
    int r = select(mainkq + 1, &rfds, NULL, NULL, NULL);
    panthera_launchd_trace("launchd: demand thread select() returned\n");
    // ...
```

### Problem 3: `job_dispatch()` may not trigger `job_start()`

After `job_import()` loads a job, `job_dispatch()` evaluates whether to start it. For `RunAtLoad` jobs, it should call `job_start()`. But `job_dispatch()` has complex condition evaluation:

```c
// core.c job_dispatch() simplified:
if (job_active(j)) return; // already running
if (!job_keepalive(j)) return; // not eligible
// ... various conditions ...
job_start(j);
```

The `job_keepalive()` check at core.c evaluates KeepAlive conditions. For `RunAtLoad` jobs, `j->ondemand = false` (meaning "always run"). Check that `job_import_keys()` correctly sets this when parsing `RunAtLoad = true`.

**Debug:** Add trace in `job_dispatch()`:
```c
void job_dispatch(job_t j, bool kickstart) {
    panthera_launchd_trace_str("launchd: job_dispatch ", j->label);
    // after the start decision:
    panthera_launchd_trace_str("launchd: job_start ", j->label);
    job_start(j);
```

### Problem 4: `runtime_fork()` might fail

`runtime_fork()` at line 637 calls `fork()`. If fork succeeds but the child crashes before exec (due to shared cache ownership, TLS, or missing bootstrap port), the parent sees the child exit immediately and may not log it clearly.

**Debug:** Add trace around fork:
```c
kern_return_t runtime_fork(mach_port_t bsport) {
    panthera_launchd_trace("launchd: runtime_fork() called\n");
    // ...
    r = fork();
    if (r == 0) {
        // child
        panthera_launchd_trace("launchd: runtime_fork child\n");
    } else if (r > 0) {
        panthera_launchd_trace("launchd: runtime_fork parent, child pid=\n");
    } else {
        panthera_launchd_trace("launchd: runtime_fork FAILED\n");
    }
```

### Problem 5: `launchd_runtime2()` blocks forever on mach_msg

The main loop in `launchd_runtime2()` calls `xpc_pipe_try_receive()` which does `mach_msg(MACH_RCV_MSG)`. If this blocks indefinitely (no timeout), the main thread never returns to process other work. On real macOS, the demand thread handles kqueue events in parallel. But if the demand thread isn't running, everything stalls.

Check if `xpc_pipe_try_receive()` in `panthera_xpc_pipe.c` uses a timeout:
```c
// Should use MACH_RCV_TIMEOUT, not blocking indefinitely
kr = mach_msg(&request.header, MACH_RCV_MSG | MACH_RCV_TIMEOUT,
    0, sizeof(request), port, 100 /* 100ms timeout */, MACH_PORT_NULL);
```

If it blocks forever, the main thread never runs `jobmgr_dispatch_all()`.

## Fix Strategy

### Step 1: Add diagnostic traces

Add `panthera_launchd_trace()` calls at:
1. Start of `kqueue_demand_loop()` — does the thread start?
2. Inside the `select()` loop — does select return?
3. In `job_dispatch()` — is it called for each job? Does it decide to start?
4. In `job_start()` — is it reached?
5. In `runtime_fork()` — does fork succeed?
6. In `job_start_child()` — does exec happen?

### Step 2: Fix `xpc_pipe_try_receive()` timeout

Check `userland/launchd/real/panthera_xpc_pipe.c`. The `mach_msg` receive MUST have a timeout so the main thread can alternate between message handling and other work:

```c
kr = mach_msg(&request.header,
    MACH_RCV_MSG | MACH_RCV_TIMEOUT | MACH_RCV_LARGE,
    0, sizeof(request), port,
    100, /* 100ms timeout — allows main thread to do other work */
    MACH_PORT_NULL);

if (kr == MACH_RCV_TIMED_OUT) {
    return 0; /* no message, but not an error */
}
```

### Step 3: Verify demand thread starts

If the demand thread doesn't start (pthread_create fails or thread crashes):
- Check that `pthread_create` returns 0
- Check that `kqueue_demand_loop` prints its trace
- If the thread crashes, it's likely a TLS issue — the same `__bsdthread_register` / TSD problem from earlier

### Step 4: Verify `job_dispatch` triggers `job_start`

After `panthera_load_jobs()` imports all plists, `jobmgr_dispatch_all(root_jobmgr, true)` should iterate all jobs and call `job_dispatch(j, true)` for each. `job_dispatch` should then call `job_start()` for RunAtLoad jobs.

If `job_dispatch` doesn't start jobs, check:
- Is `j->ondemand` set correctly? (`RunAtLoad=true` should set `j->ondemand = false`)
- Is `job_active(j)` returning true prematurely?
- Is `job_keepalive(j)` returning false?

### Step 5: Verify `runtime_fork` works

If `job_start()` is called but children don't appear:
- Is `fork()` returning > 0 in the parent?
- Is the child surviving past `fork()` (check for immediate crash)?
- Is `execve()` in `job_start_child()` succeeding?

### Step 6: Fix shared cache for child processes

`panthera_boot_init()` calls `panthera_fix_shared_cache_ownership()` which fixes cache file permissions. This must happen BEFORE any `job_start()`. Verify it runs early enough in the boot sequence.

### Step 7: Remove traces and verify

Once services start, remove all `panthera_launchd_trace()` calls and do a clean boot test.

## Build and Test

```bash
# Build real launchd
PANTHERA_LAUNCHD_REAL=1 bash userland/launchd/build_launchd.sh

# Stage to root image
raw_device="$(hdiutil attach -nomount -imagekey diskimage-class=CRawDiskImage images/qemu/panthera-root.img | awk 'NR==1 {print $1}')"
sleep 1
diskutil mount "${raw_device}s1"
cp userland/launchd/launchd /Volumes/PantheraRoot/sbin/launchd
diskutil unmount /Volumes/PantheraRoot
hdiutil detach "$raw_device"

# Boot test
PANTHERA_NOGRAPHIC=1 boot/qemu/run_phase2_qemu.sh --no-reboot --ssh-port 0 \
    --root-disk images/qemu/panthera-root.img
```

Check for:
- `launchd: demand thread started` — thread is alive
- `launchd: demand thread select() waiting` — thread enters select
- `launchd: job_dispatch <label>` — jobs being evaluated
- `launchd: job_start <label>` — jobs starting
- `launchd: runtime_fork parent, child pid=N` — fork succeeds
- Login prompt appears

## Fallback

If services still don't start after debugging:
```bash
# Restore shim launchd
PANTHERA_LAUNCHD_REAL=0 bash userland/launchd/build_launchd.sh
# Re-stage and boot
```

## Rules

- Read `OS_BUILD_ROADMAP.md` foundation rules
- Keep `launchd_all_stubs.c` as fallback — don't delete it
- Modifications to Apple source use `#if PANTHERA` guards
- Add `panthera_launchd_trace()` for debugging, remove when done
- Boot test and read output — don't ask the user
- No deferred work — services must start before declaring done

## Success Criteria

1. The kqueue demand thread starts and runs `select()` on `mainkq`
2. `job_dispatch()` evaluates all 7 loaded jobs
3. `job_start()` is called for RunAtLoad jobs
4. `runtime_fork()` creates child processes
5. Children execute (login prompt appears, netbringup runs, sshd starts)
6. `EVFILT_PROC` NOTE_EXIT events are delivered when children exit
7. KeepAlive jobs restart when killed
8. SSH works from host: `ssh -p 2222 root@localhost`
9. All diagnostic traces removed from final build
