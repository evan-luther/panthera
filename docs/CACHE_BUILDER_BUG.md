# Shared Cache Builder: DATA Rebase Corruption Bug

**Status:** Known, partially mitigated, needs proper fix
**Filed:** 2026-04-02
**Affects:** `tools/build_shared_cache_real.c` — `do_rebase()` and `apply_rebases()`

## Symptom

After the cache builder processes rebases in the DATA region, small integer
fields in static C structs are corrupted.  For example, Libinfo's
`si_module_static_file()` returns a `si_mod_t` with `vers=0, refcount=0`
instead of the correct `vers=1, refcount=1`.  This prevents the file module
from functioning, breaking `getpwuid()`, `getpwnam()`, and all passwd/group
lookups.

## Root Cause

`do_rebase()` in the cache builder processes every location specified by the
old-style rebase opcodes (LC_DYLD_INFO).  When a rebased pointer value does
not fall within any known segment range, the fallback at line ~906 applies
the TEXT slide unconditionally:

```c
/* Target not in any segment — use TEXT slide */
if (d->textSegIdx >= 0) {
    *(uint64_t *)loc = val + d->segSlides[d->textSegIdx];
}
```

The problem: when the compiler places a pointer field and adjacent integer
fields in the same 8-byte-aligned region, the rebase opcode covers the
pointer location.  But the `do_rebase()` function reads and writes full
8-byte values.  If an adjacent integer field (e.g., `vers=1`) occupies
bytes that overlap with a rebased pointer when read as a 64-bit value,
the small integer gets treated as a pointer target and corrupted.

Additionally, the rebase opcode stride (8 bytes) may walk through struct
members that aren't pointers at all, if the compiler's DATA layout places
non-pointer fields at 8-byte boundaries that happen to be in the rebase
chain.

## Partial Mitigation

A `>= 0x1000` threshold was added to skip small values:

```c
if (d->textSegIdx >= 0 && val >= 0x1000) {
```

This prevents corruption of zero and very small values but does NOT fix:
- Combined 32-bit fields that form values >= 0x1000 when read as 64-bit
  (e.g., `vers=1, refcount=1` → 0x0000000100000001)
- Fields with legitimate non-zero values that aren't pointers

## Proper Fix Needed

The cache builder should only rebase locations that the rebase opcodes
explicitly specify as pointer locations.  The current implementation
correctly decodes the opcode stream (segment index + offset), but the
`do_rebase()` fallback should NOT blindly apply TEXT slide to values
that don't match any known segment.  Instead:

1. If a value doesn't match any segment, leave it unchanged (it may be
   an absolute address or a non-pointer value at a coincident offset)
2. Or: validate that the value falls in a plausible pointer range before
   applying any slide
3. Long-term: switch to proper chained fixup processing for all dylibs,
   which encodes pointer vs. non-pointer information directly in the
   fixup chain bits

## Current Workaround

Panthera provides its own `getpwuid`/`getpwnam`/`getgrgid`/`getgrnam`
implementations in `libpanthera_extra.dylib` that read `/etc/passwd`
and `/etc/group` directly, bypassing the corrupted cached Libinfo
entirely.  These are loaded before `libsystem_info.dylib` in the
symbol resolution order.

## Reproduction

1. Build the shared cache: `bash tools/build_shared_cache.sh`
2. Boot Panthera in QEMU
3. Run a probe that calls `si_module_static_file()` and dumps the struct
4. Observe `vers=0, refcount=0` instead of `vers=1, refcount=1`
5. `getpwuid(0)` returns NULL even though `/etc/passwd` has root entry
