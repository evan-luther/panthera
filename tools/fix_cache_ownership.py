#!/usr/bin/env python3
"""
Fix HFS+ catalog ownership for Panthera's shared cache inside a raw disk image.

This patches the catalog record directly, so it works without mounting the image
and without host root privileges. XNU requires the shared cache file to be owned
by uid 0 / gid 0 before PID 1 starts.
"""

from __future__ import annotations

import argparse
import mmap
import os
import struct
import sys

DEFAULT_TARGETS = ("dyld_shared_cache_x86_64",)
HFS_PLUS_FOLDER_RECORD = 0x0001
HFS_PLUS_FILE_RECORD = 0x0002
SF_RESTRICTED_BIT = 0x08
HFS_MODE_PERMISSIONS_MASK = 0o7777


def iter_matching_records(mm: mmap.mmap, target_name: str):
    target_utf16 = target_name.encode("utf-16-be")
    pos = 0

    while True:
        pos = mm.find(target_utf16, pos)
        if pos == -1:
            return

        name_length_offset = pos - 2
        if name_length_offset < 0:
            pos += 1
            continue

        stored_name_len = struct.unpack_from(">H", mm, name_length_offset)[0]
        if stored_name_len != len(target_name):
            pos += 1
            continue

        parent_id_offset = name_length_offset - 4
        key_length_offset = parent_id_offset - 2
        if key_length_offset < 0:
            pos += 1
            continue

        key_length = struct.unpack_from(">H", mm, key_length_offset)[0]
        expected_key_length = 4 + 2 + (2 * len(target_name))
        if key_length != expected_key_length:
            pos += 1
            continue

        record_offset = key_length_offset + 2 + key_length
        if record_offset & 1:
            record_offset += 1

        if record_offset + 48 > len(mm):
            pos += 1
            continue

        record_type = struct.unpack_from(">h", mm, record_offset)[0]
        if record_type not in (HFS_PLUS_FOLDER_RECORD, HFS_PLUS_FILE_RECORD):
            pos += 1
            continue

        yield {
            "target_name": target_name,
            "name_offset": pos,
            "record_offset": record_offset,
            "record_type": record_type,
            "cnid": struct.unpack_from(">I", mm, record_offset + 8)[0],
            "perm_offset": record_offset + 32,
        }
        pos += 1


def parse_octal_mode(value: str) -> int:
    try:
        mode = int(value, 8)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid octal mode: {value}") from exc
    if mode < 0 or mode > 0o7777:
        raise argparse.ArgumentTypeError(f"mode out of range: {value}")
    return mode


def patch_image(
    image_path: str,
    target_names: tuple[str, ...],
    check_only: bool,
    quiet: bool,
    uid_target: int,
    gid_target: int,
    mode_target: int | None,
    set_restricted: bool,
) -> int:
    matched = 0
    needs_patch_count = 0
    changed = 0

    with open(image_path, "r+b") as image_file:
        with mmap.mmap(image_file.fileno(), 0) as mm:
            for target_name in target_names:
                for record in iter_matching_records(mm, target_name):
                    matched += 1
                    perm_offset = record["perm_offset"]
                    uid, gid = struct.unpack_from(">II", mm, perm_offset)
                    admin_flags = mm[perm_offset + 8]
                    owner_flags = mm[perm_offset + 9]
                    mode = struct.unpack_from(">H", mm, perm_offset + 10)[0]

                    needs_patch = (
                        uid != uid_target
                        or gid != gid_target
                        or (set_restricted and not (admin_flags & SF_RESTRICTED_BIT))
                        or (
                            mode_target is not None
                            and (mode & HFS_MODE_PERMISSIONS_MASK) != mode_target
                        )
                    )
                    if needs_patch:
                        needs_patch_count += 1

                    if not quiet:
                        if not needs_patch:
                            status = "ok"
                        elif check_only:
                            status = "would_patch"
                        else:
                            status = "patch"
                        print(
                            f"{status}: {target_name} cnid={record['cnid']} "
                            f"type=0x{record['record_type']:04x} "
                            f"uid={uid} gid={gid} adminFlags=0x{admin_flags:02x} "
                            f"mode=0o{mode:o}"
                        )

                    if check_only or not needs_patch:
                        continue

                    if set_restricted:
                        admin_flags |= SF_RESTRICTED_BIT
                    if mode_target is not None:
                        mode = (mode & ~HFS_MODE_PERMISSIONS_MASK) | mode_target
                    patch = (
                        struct.pack(">II", uid_target, gid_target)
                        + bytes([admin_flags, owner_flags])
                        + struct.pack(">H", mode)
                    )
                    mm[perm_offset : perm_offset + len(patch)] = patch
                    changed += 1

            if changed:
                mm.flush()

    if matched == 0:
        print(
            f"error: no matching HFS+ catalog entries found in {image_path}",
            file=sys.stderr,
        )
        return 1

    if not quiet:
        patched_count = needs_patch_count if check_only else changed
        verb = "would patch" if check_only else "patched"
        print(f"{verb}: {patched_count} record(s), matched {matched} record(s)")

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Fix dyld shared cache ownership in a Panthera raw HFS+ image."
    )
    parser.add_argument("image_path", help="Path to the raw HFS+ disk image")
    parser.add_argument(
        "--check",
        action="store_true",
        help="Report whether a patch is needed without modifying the image",
    )
    parser.add_argument(
        "--quiet",
        action="store_true",
        help="Suppress per-record output",
    )
    parser.add_argument(
        "--target-name",
        action="append",
        dest="target_names",
        help="Exact HFS+ filename to patch. May be passed multiple times.",
    )
    parser.add_argument(
        "--uid",
        type=int,
        default=0,
        help="UID to write into matching catalog records (default: 0)",
    )
    parser.add_argument(
        "--gid",
        type=int,
        default=0,
        help="GID to write into matching catalog records (default: 0)",
    )
    parser.add_argument(
        "--mode",
        type=parse_octal_mode,
        help="Octal mode to write into matching catalog records, e.g. 755",
    )
    parser.add_argument(
        "--no-restricted",
        action="store_true",
        help="Do not set the HFS+ SF_RESTRICTED admin flag",
    )
    args = parser.parse_args()

    image_path = os.path.abspath(args.image_path)
    if not os.path.isfile(image_path):
        print(f"error: image not found: {image_path}", file=sys.stderr)
        return 1

    target_names = tuple(args.target_names) if args.target_names else DEFAULT_TARGETS
    return patch_image(
        image_path,
        target_names,
        args.check,
        args.quiet,
        args.uid,
        args.gid,
        args.mode,
        not args.no_restricted,
    )


if __name__ == "__main__":
    raise SystemExit(main())
