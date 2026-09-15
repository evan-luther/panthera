#!/usr/bin/env python3
"""
Silence Panthera's cached Objective-C trace strings inside a dyld shared cache.

This is a surgical fallback for the known-good shared cache baseline while the
newer cache rebuild path is still unstable for PID 1.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import sys


TRACE_STRINGS = (
    b"[objc4] _objc_init enter\n",
    b"[objc4] _objc_init after register callbacks\n",
    b"[objc4] preopt_init enter\n",
    b"[objc4] preopt_init shared cache start/len fetched\n",
    b"[objc4] preopt_init empty_cache not in shared range\n",
    b"[objc4] preopt_init disabled\n",
    b"[objc4] preopt_init enabled\n",
)


def patch_cache(path: Path, check_only: bool) -> int:
    data = bytearray(path.read_bytes())
    changed = 0

    for trace in TRACE_STRINGS:
        pos = data.find(trace)
        if pos == -1:
            print(f"missing: {trace.decode('utf-8', 'replace').rstrip()}", file=sys.stderr)
            return 1
        muted = (b" " * (len(trace) - 1)) + b"\n"
        if data[pos : pos + len(trace)] == muted:
            print(f"ok: {trace.decode('utf-8', 'replace').rstrip()} @ 0x{pos:x}")
            continue

        print(
            f"{'would_patch' if check_only else 'patch'}: "
            f"{trace.decode('utf-8', 'replace').rstrip()} @ 0x{pos:x}"
        )
        if not check_only:
            data[pos : pos + len(trace)] = muted
            changed += 1

    if changed:
        path.write_bytes(data)

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cache_path", help="Path to dyld_shared_cache_x86_64")
    parser.add_argument("--check", action="store_true", help="Report matches without modifying the file")
    args = parser.parse_args()

    path = Path(args.cache_path)
    if not path.is_file():
        print(f"error: file not found: {path}", file=sys.stderr)
        return 1

    return patch_cache(path, args.check)


if __name__ == "__main__":
    raise SystemExit(main())
