# Panthera Wayland Roadmap

Read `OS_BUILD_ROADMAP.md` for foundation rules before changing dylibs, the shared cache, or the root image.

## Goal

Bring up a minimal graphical stack on Panthera with a **Wayland-first** architecture:

- kernel display path based on Apple open-source `IOGraphics`
- userspace display/input access through `IOKit.framework` / `IOKitUser`
- a Panthera-native **software compositor** using the boot framebuffer first
- **X11 deferred** to `Xwayland` compatibility later, not as the primary stack

This is intentionally **not** a “port a Linux desktop wholesale” plan. Panthera does not yet have Linux DRM/KMS, libinput, or Mesa userspace conventions, so the first usable desktop must be built on top of Darwin-native primitives that Panthera already has or can realistically finish.

## Why This Direction

Current repo state already supports the first half of this plan:

- QEMU can run Panthera with a graphical window via the existing harness in `boot/qemu/run_phase2_qemu.sh`
- Panthera boot logs already show a live EFI/GOP framebuffer: `PE_create_console ... v_baseAddr=0x80000000 w=1280 h=800 d=32`
- Apple `IOGraphics-598` is already present in `src/`, including `IOBootFramebuffer.cpp`
- input-side kernel work is already underway: `IOHIDSystem`, `ApplePS2Controller`, and `ApplePS2Keyboard` are part of the OpenIOKit build
- CoreFoundation is built, which removes a major blocker for `IOKitUser`
- `IOKitUser-100065.40.4` is already fetched locally

What is *not* present yet:

- no staged `IOGraphicsFamily` kext
- no built `IOKit.framework` / `IOKitUser`
- no userspace graphics compositor
- no Mesa, DRM/KMS, libinput, wlroots, Weston, or Xorg stack

That makes the shortest viable path:

1. finish Darwin graphics plumbing
2. expose framebuffer + input to userspace
3. write a minimal Wayland compositor over that path
4. add compatibility layers later

## Non-Goals For Phase 1

The following are explicitly deferred:

- full GPU acceleration
- OpenGL / Metal / CoreAnimation equivalents
- Quartz / WindowServer replacements
- full desktop environment parity with macOS or Linux
- Weston/wlroots as the **first** compositor target
- native Xorg/X11 as the primary UI stack

## Target Architecture

```text
QEMU display window
    ↓
EFI GOP / boot framebuffer
    ↓
XNU PlatformExpert console info
    ↓
IOGraphicsFamily + IOBootFramebuffer
    ↓
IOKit.framework / IOKitUser + IOHID user APIs
    ↓
Panthera compositor backend
    ↓
Wayland server + shared-memory clients
    ↓
Minimal shell / terminal launcher
    ↓
Optional Xwayland later
```

## Phase 0: Prove The Framebuffer Path

**Goal:** Verify Panthera can promote the existing boot framebuffer into a real IOKit graphics service.

### Deliverable

A booted QEMU instance where Panthera exposes one display service backed by the EFI/GOP framebuffer, without requiring GPU acceleration.

### Tasks

1. Build `IOGraphicsFamily` from `src/IOGraphics-598/`
2. Focus first on:
   - `IOFramebuffer`
   - `IOBootFramebuffer`
   - `IODisplayWrangler`
   - minimum supporting display classes needed for registration
3. Stage the kext into the boot flow the same way current OpenIOKit components are staged
4. Verify that a framebuffer service appears in the IORegistry
5. Confirm it binds only when `AAPL,boot-display` and valid console info are present

### Success Criteria

- Panthera still boots cleanly with graphical QEMU enabled
- `IOGraphicsFamily` loads without regressing shell boot
- one framebuffer service appears in IORegistry
- mode info matches boot console dimensions and depth

### Risks

- `IOGraphicsFamily` may pull in more IOKit power/display infrastructure than expected
- `IOBootFramebuffer` may need a custom personality or provider match glue in Panthera
- boot display registration may depend on missing `IOResources` interactions

## Phase 1: Build Minimal IOKit Userland

**Goal:** Give userspace a stable Darwin-native way to discover display services and map framebuffer memory.

### Deliverable

A built `IOKit.framework`/`IOKitUser` subset sufficient for:

- service matching
- registry inspection
- user-client open/close
- memory mapping
- basic HID access

### Tasks

1. Build the smallest useful `IOKitUser` slice first:
   - `IOKitLib.c`
   - `IOCFSerialize.c`
   - `IOCFUnserialize.c`
   - graphics subproject pieces needed for display services
2. Export and verify these APIs at minimum:
   - `IOServiceMatching`
   - `IOServiceGetMatchingService`
   - `IOServiceGetMatchingServices`
   - `IORegistryEntryCreateCFProperties`
   - `IOServiceOpen`
   - `IOConnectMapMemory`
   - `IOConnectCallMethod`
   - `IOIteratorNext`
   - `IOObjectRelease`
3. Stage headers and framework layout into the sysroot
4. Add the library/framework to the shared cache if needed by booted binaries
5. Write a tiny diagnostic tool:
   - enumerate framebuffer-like services
   - dump properties
   - attempt `IOServiceOpen`
   - attempt `IOConnectMapMemory`

### Success Criteria

- a userspace binary can find the display service
- the display service can be opened from userspace
- framebuffer memory can be mapped into the caller task
- no dyld unresolved binds or flat-namespace regressions

### Risks

- framework packaging may be more work than the library build itself
- some graphics user-client path may need kernel-side fixes rather than userspace work
- staged framework may expose missing `mach`, CF, or MIG edges

## Phase 2: Introduce A Panthera Fallback Display API

**Goal:** Avoid stalling the whole roadmap if full generic IOKit graphics user-clients take too long.

### Deliverable

One Panthera-specific escape hatch for framebuffer access, used only if the generic path is blocked:

- a tiny framebuffer user-client, or
- a narrow character device such as `/dev/pantherafb`

### Rules

- keep it minimal and clearly temporary
- do not design a broad ad-hoc graphics subsystem
- use it only to unblock the first compositor milestone
- keep the compositor backend abstract enough to swap to real `IOKitUser` later

### Success Criteria

- a userspace program can paint solid colors, rectangles, and a software cursor
- the fallback API does not require rethinking later compositor structure

## Phase 3: Bring Up Input End-To-End

**Goal:** Deliver one usable seat: keyboard first, mouse second.

### Deliverable

Userspace receives keyboard events from the existing kernel input stack and can associate them with a single display seat.

### Tasks

1. Verify current kernel input stack in graphical boot:
   - `IOHIDSystem`
   - `ApplePS2Controller`
   - `ApplePS2Keyboard`
2. Build the smallest `IOHID` / HID userspace surface needed
3. Write a simple input probe tool:
   - detect keyboard service(s)
   - read key events
   - print decoded key transitions
4. Add pointer/mouse later once keyboard is stable
5. Define a Panthera seat abstraction in userspace:
   - one output
   - one keyboard
   - optional pointer

### Success Criteria

- key events reach userspace reliably
- focus can be assigned to exactly one client surface
- keyboard input survives repeated shell/compositor restarts

### Risks

- modern HID event APIs may be more complex than the old PS/2 path needs
- minimal keyboard handling may require bypassing newer HID layers at first

## Phase 4: Build A Minimal Wayland Runtime

**Goal:** Run native Wayland clients with software composition only.

### Deliverable

A Panthera-native Wayland compositor with:

- one output
- one seat
- `wl_compositor`
- `wl_shm`
- `xdg-shell` or a simpler shell policy if needed
- software composition into the mapped framebuffer

### Implementation Guidance

Start with:

- `libwayland-server`
- `wayland-protocols` subset
- `pixman`
- shared-memory buffers only

Do **not** start with:

- Weston DRM backend
- wlroots DRM backend
- EGL/GLES requirements
- GPU render nodes

### Tasks

1. Port or build `libwayland-server`
2. Port or build `pixman`
3. Write a Panthera backend module for:
   - output creation
   - framebuffer blit/flush
   - keyboard event injection
   - frame timing
4. Implement minimal shell policy:
   - one focused toplevel
   - basic stacking or tiling
   - background fill
5. Build one tiny test client
   - colored window
   - keyboard input
   - redraw loop

### Success Criteria

- compositor launches from the Panthera shell
- a Wayland test client appears on screen
- keyboard focus works
- redraw is stable without corrupting the console or panicking the kernel

### Risks

- framebuffer writes may need double-buffering or dirty-rect handling
- software composition performance may be poor at first
- client/toolkit expectations may exceed the initial protocol subset

## Phase 5: First Usable Graphical Session

**Goal:** Reach a basic graphical environment that is useful for development.

### Deliverable

A minimal Panthera graphical session with:

- compositor launched by shell or launchd
- one terminal or terminal-like app
- one launcher or hardcoded menu
- clean exit path back to text shell

### Tasks

1. Decide launch model:
   - shell-started for early bring-up
   - launchd job once stable
2. Add a small terminal path:
   - Wayland-native terminal if feasible
   - otherwise a very small local test terminal app
3. Add crash containment:
   - compositor exit should not wedge the machine
   - return to console shell cleanly
4. Add screenshots / logging / debug toggles for iteration

### Success Criteria

- boot to shell, launch compositor manually, run a graphical client
- compositor can be stopped and restarted without rebooting
- a minimal session is usable for real debugging

## Phase 6: Hardening And Packaging

**Goal:** Turn the first successful prototype into a maintainable Panthera subsystem.

### Deliverable

Repeatable build scripts, root-image staging, and boot verification for the graphical stack.

### Tasks

1. Add build scripts under `userland/` for all new userspace components
2. Stage outputs in `rootfs/create_hfs_root_image.sh`
3. Extend package symbol-audit workflow to graphics components
4. Add boot tests for:
   - text mode still works
   - graphical path launches
   - compositor restart path
5. Document how to boot Panthera in graphical vs serial modes

### Success Criteria

- clean rebuild from repo scripts
- staged image contains the complete minimal Wayland stack
- both serial and graphical bring-up remain available

## Deferred Phase: X11 Compatibility

**Goal:** Support legacy X11 applications without making X11 the base architecture.

### Preferred Path

Bring up `Xwayland` after the native Wayland compositor is stable.

### Why Deferred

- native X11 on Darwin is historically possible but large and brittle
- it does not reduce the need for framebuffer/input/userland IOKit work
- it would compete directly with the Wayland-first goal

### Only Revisit Native X11 If

- `Xwayland` proves infeasible on Panthera after Wayland works, or
- there is a specific hard requirement for old X11 server behavior

## Concrete Milestones

### M1

Panthera boots in QEMU graphical mode and loads `IOGraphicsFamily` without regressing shell boot.

### M2

A userspace tool enumerates the boot framebuffer service and maps display memory.

### M3

A userspace test program draws directly to the framebuffer and responds to keyboard input.

### M4

A Panthera-native Wayland compositor displays one software-rendered client.

### M5

A minimal graphical session can be launched and exited repeatedly from the shell.

### M6

`Xwayland` support is evaluated on top of the working Wayland compositor.

## Recommended Immediate Next Steps

1. Build and stage a minimal `IOGraphicsFamily` centered on `IOBootFramebuffer`
2. Build the minimal `IOKitUser` slice needed for display enumeration and `IOConnectMapMemory`
3. Write two probes before any compositor work:
   - `ioreg`-style display enumerator
   - framebuffer mapping/draw test
4. Only after those pass, begin the Wayland server/compositor work

## Decision Boundary

If Panthera cannot expose the boot framebuffer cleanly through either `IOGraphicsFamily + IOKitUser` or a tiny Panthera fallback API, then Wayland is premature and the project should pause before any compositor/library porting.

If framebuffer mapping and keyboard delivery both work, Wayland becomes the right primary path and native X11 should remain deferred.
