# Boot Bring-Up

Phase 1 is complete: Panthera now builds a Sonoma-era x86_64 XNU kernel from source.

This directory holds the Phase 2 boot bring-up workspace:

- `efi/stage_phase2_efi.sh` stages the built kernel into an EFI-visible FAT tree.
- `efi/build_bootx64.sh` builds the in-repo `BOOTX64.EFI` scaffold when an EFI linker is available.
- `efi/inspect_kernel_layout.sh` validates the loader's Mach-O parser against the built kernel on the host.
- `qemu/run_phase2_qemu.sh` launches QEMU with the local edk2 firmware and the staged tree.

Current state: the repository now produces and stages a native `EFI/BOOT/BOOTX64.EFI` loader, edk2 executes it successfully under QEMU, and the loader performs a real handoff into XNU. The kernel now gets through early allocator/bootstrap work, reaches real BSD bring-up, prints through `kext submap`, and currently resets after that point instead of reaching the earlier route-init panic.

The immediate goal for this phase is no longer "prove the boot chain" in the abstract. That part now works. The current goal is to keep pushing XNU through late Phase 2 until the remaining post-`kext submap` reset is localized and resolved cleanly enough to begin Phase 3 rootfs and userland work.

`boot/qemu/run_phase2_qemu.sh` also now supports `--no-reboot` so reset loops can be turned into one-shot debug runs.

Current kernel-entry fact: the built XNU image's Mach-O `LC_UNIXTHREAD` entry PC is `0xffffff8000100000`, which lands in the `__HIB` segment at `__start` / `_pstart`. The loader should derive that from the image, not hard-code `_vstart`.
