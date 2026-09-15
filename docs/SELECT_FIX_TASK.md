# Fix select() on TCP sockets — waitq linkage is broken in XNU

## Context

Read `OS_BUILD_ROADMAP.md` for foundation rules. `poll()` works on sockets but `select()` doesn't. OpenSSL uses `select()`, so HTTPS fails without curl's poll-based workaround.

## Confirmed Facts

- `select(read, 10s)` returns 0 (timeout) even with 773 bytes in the socket buffer
- `select(write, 10s)` returns 0 (timeout) for non-blocking connect completion
- `poll(POLLIN, 10s)` returns 1 with `revents=0x11` on the **same socket** — poll works
- Debug showed: `sorwakeup SKIP sb_flags=0x404` (SB_DROP|SB_WAIT, no SB_SEL or SB_KNOTE)
- Blocking `read()` works — data IS retrievable from the socket buffer

## How select() Should Work (from the source)

The code in `bsd/kern/sys_socket.c:soo_select()` lines 314-323 IS correct:

```c
case FREAD:
    so->so_rcv.sb_flags |= SB_SEL;          // Set flag
    if (soreadable(so)) {                     // Data ready?
        retnum = 1;
        so->so_rcv.sb_flags &= ~SB_SEL;     // Clear flag
        goto done;                            // Return 1 immediately
    }
    selrecord(procp, &so->so_rcv.sb_sel, wql); // Register for wakeup
    break;
```

When data IS in the buffer (`cc=773`), `soreadable()` should return TRUE, `retnum=1`, and select should return 1 immediately. **But it returns 0.**

## Where to Look for the Bug

### Hypothesis A: `fo_select()` dispatch is broken

`selscan()` at line 1655 calls `fo_select(fp, flag[msk], s_data, &context)`. If `fo_select` doesn't dispatch to `soo_select()`, select never checks the socket. Add kprintf in `soo_select()`:

```c
soo_select(struct fileproc *fp, int which, void *wql, vfs_context_t ctx) {
    struct socket *so = (struct socket *)fp_get_data(fp);
    kprintf("PANTHERA: soo_select which=%d so=%p\n", which, so);
    // ... rest of function
```

If this never prints during a select call on a socket, the dispatch is broken.

### Hypothesis B: `soreadable()` returns false despite data

`soreadable(so)` is a macro: `(so->so_rcv.sb_cc >= so->so_rcv.sb_lowat || ...)`. If `sb_lowat` is set absurdly high, data is present but not "readable." Add kprintf:

```c
if (soreadable(so)) {
    kprintf("PANTHERA: soo_select READABLE cc=%d lowat=%d\n",
            so->so_rcv.sb_cc, so->so_rcv.sb_lowat);
    retnum = 1;
    // ...
} else {
    kprintf("PANTHERA: soo_select NOT readable cc=%d lowat=%d\n",
            so->so_rcv.sb_cc, so->so_rcv.sb_lowat);
    selrecord(procp, &so->so_rcv.sb_sel, wql);
}
```

### Hypothesis C: selscan loop skips the fd

The bit-scanning loop in `selscan()` (line 1631-1670) might not iterate over the socket's fd. Add kprintf at the top of the loop:

```c
if (fo_select(fp, flag[msk], s_data, &context)) {
    kprintf("PANTHERA: selscan fd=%d msk=%d HIT\n", fd, msk);
    optr[fd / NFDBITS] |= (1U << (fd % NFDBITS));
    n++;
} else {
    kprintf("PANTHERA: selscan fd=%d msk=%d miss\n", fd, msk);
}
```

### Hypothesis D: selrecord/waitq linkage fails

If data isn't available when select first runs, `selrecord()` at line 2108 links the socket's waitq to the thread's select set. If `waitq_init()` or `select_set_link()` fails silently, the wakeup path is dead. Add kprintf in `selrecord()`:

```c
selrecord(__unused struct proc *selector, struct selinfo *sip, void *s_data) {
    struct select_set *selset = current_uthread()->uu_selset;
    if (!s_data) return;
    
    if (selset == SELSPEC_RECORD_MARKER) {
        ((selspec_record_hook_t)s_data)(sip);
    } else {
        waitq_link_t *linkp = s_data;
        int was_valid = waitq_is_valid(&sip->si_waitq);
        if (!was_valid) {
            waitq_init(&sip->si_waitq, WQT_SELECT, SYNC_POLICY_FIFO);
        }
        select_set_link(&sip->si_waitq, selset, linkp);
        kprintf("PANTHERA: selrecord was_valid=%d selset=%p\n", was_valid, selset);
    }
}
```

### Hypothesis E: `select_internal` never reaches `selscan`

The `select_internal()` path goes through `selcount()` (line 1334) then `selprocess()` then `selscan()`. If `selcount()` returns `count=0`, selscan is skipped. Add kprintf:

```c
// In select_internal, after selcount:
kprintf("PANTHERA: select_internal nd=%d count=%d\n", uap->nd, seldata->count);
```

## Debug Strategy

1. Add the 5 kprintfs above (hypotheses A-E)
2. Rebuild kernel: `bash build/build_xnu.sh`
3. Stage: `cp BUILD/obj/RELEASE_X86_64/kernel boot/efi/staging/mach_kernel && cp boot/efi/staging/mach_kernel boot/efi/staging/System/Library/Kernels/kernel && cp boot/efi/staging/mach_kernel boot/efi/staging/EFI/PANTHERA/kernel`
4. Rebuild root image: `bash rootfs/create_hfs_root_image.sh --force`
5. Boot and test with a minimal program that does:
   ```c
   int fd = socket(AF_INET, SOCK_STREAM, 0);
   connect(fd, google_80, ...);
   write(fd, "GET / HTTP/1.1\r\nHost: google.com\r\n\r\n", 36);
   sleep(2); // wait for response
   fd_set rset; FD_ZERO(&rset); FD_SET(fd, &rset);
   struct timeval tv = {5, 0};
   int rc = select(fd+1, &rset, NULL, NULL, &tv);
   printf("select rc=%d\n", rc);
   ```
6. Read the kprintf output to identify which hypothesis is correct

## Fix Based on Findings

- **If A**: Fix `fo_select` dispatch — check the socket's fileops table
- **If B**: Fix `sb_lowat` initialization — set to 1 in `soreserve()` or `socreate()`
- **If C**: Fix the fd bit scanning — check `copyin` of the fd_set from userspace
- **If D**: Fix `waitq_init` or `select_set_link` — these are in `osfmk/kern/waitq.c`
- **If E**: Fix `selcount` — it's miscounting socket fds

After fixing, remove ALL `PANTHERA:` kprintfs and rebuild clean.

## Rules

- Kernel source modifications are acceptable for XNU
- Minimize changes — debug first, fix precisely, clean up
- After fixing: rebuild kernel, stage to all 3 locations, rebuild root image
- Boot test: `PANTHERA_NOGRAPHIC=1 boot/qemu/run_phase2_qemu.sh --no-reboot --ssh-port 0 --root-disk images/qemu/panthera-root.img`
- Read output as text — don't ask the user
- No deferred work — select() must work when done

## Success Criteria

- `select(fd+1, &rset, NULL, NULL, &tv)` returns 1 when socket has data
- `curl -vkL https://google.com` works WITHOUT the BIO poll patch
- Remove curl's poll-on-EAGAIN sed patch from `build_curl.sh` after confirming
- All kprintfs removed from final kernel
- EVFILT_TIMER tested (may also be fixed if the issue is in shared waitq infrastructure)
