#!/usr/bin/env python3
"""Audit Panthera SDK shape, optionally comparing a reference Darwin SDK."""

from __future__ import annotations

from pathlib import Path
import argparse
import os


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SDK = ROOT / "userland/panthera_sdk/Panthera.sdk"

REQUIRED_TOP = (
    "SDKSettings.json",
    "SDKSettings.plist",
    "SDKInfo.plist",
    "Entitlements.plist",
    "_PROVENANCE",
    "usr/include",
    "usr/lib",
    "usr/local/include",
    "usr/local/lib",
    "System/Library/Frameworks/System.framework",
    "System/Library/Frameworks/Kernel.framework",
)

CORE_HEADERS = (
    "Availability.h",
    "AssertMacros.h",
    "Block.h",
    "TargetConditionals.h",
    "dlfcn.h",
    "errno.h",
    "fcntl.h",
    "notify.h",
    "pthread.h",
    "pwd.h",
    "resolv.h",
    "signal.h",
    "stdarg.h",
    "stdatomic.h",
    "stdio.h",
    "stdlib.h",
    "string.h",
    "unistd.h",
    "utmpx.h",
)


def rels(root: Path, kind: str) -> set[str]:
    if not root.exists():
        return set()
    out: set[str] = set()
    for path in root.rglob("*"):
        try:
            st_mode = os.lstat(path).st_mode
        except FileNotFoundError:
            continue
        if kind == "file" and path.is_file() and not path.is_symlink():
            out.add(path.relative_to(root).as_posix())
        elif kind == "symlink" and path.is_symlink():
            out.add(path.relative_to(root).as_posix())
        elif kind == "dir" and path.is_dir() and not path.is_symlink():
            out.add(path.relative_to(root).as_posix())
    return out


def exists(root: Path, rel: str) -> bool:
    return (root / rel).exists() or (root / rel).is_symlink()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", type=Path, default=DEFAULT_SDK)
    parser.add_argument("--reference", type=Path)
    args = parser.parse_args()

    sdk = args.sdk
    files = rels(sdk, "file")
    links = rels(sdk, "symlink")
    dirs = rels(sdk, "dir")

    failures: list[str] = []
    print("# Panthera SDK audit")
    print()
    print(f"SDK: `{sdk}`")
    print()
    print(f"- Files: {len(files)}")
    print(f"- Symlinks: {len(links)}")
    print(f"- Directories: {len(dirs)}")
    print()

    print("## Required Shape")
    for rel in REQUIRED_TOP:
        ok = exists(sdk, rel)
        print(f"- {'PASS' if ok else 'FAIL'} `{rel}`")
        if not ok:
            failures.append(rel)

    print()
    print("## Core Headers")
    for header in CORE_HEADERS:
        rel = f"usr/include/{header}"
        ok = exists(sdk, rel)
        print(f"- {'PASS' if ok else 'FAIL'} `{rel}`")
        if not ok:
            failures.append(rel)

    if args.reference:
        ref = args.reference
        ref_files = rels(ref, "file")
        ref_links = rels(ref, "symlink")
        ref_dirs = rels(ref, "dir")
        print()
        print("## Reference Comparison")
        print(f"Reference: `{ref}`")
        print()
        print("| Metric | Panthera | Reference |")
        print("|---|---:|---:|")
        print(f"| Files | {len(files)} | {len(ref_files)} |")
        print(f"| Symlinks | {len(links)} | {len(ref_links)} |")
        print(f"| Directories | {len(dirs)} | {len(ref_dirs)} |")

        p_headers = {p.removeprefix("usr/include/") for p in files | links if p.startswith("usr/include/") and "/" not in p.removeprefix("usr/include/")}
        r_headers = {p.removeprefix("usr/include/") for p in ref_files | ref_links if p.startswith("usr/include/") and "/" not in p.removeprefix("usr/include/")}
        missing = sorted(r_headers - p_headers)
        extra = sorted(p_headers - r_headers)
        print()
        print("Reference top-level headers missing in Panthera:")
        if missing:
            for name in missing[:80]:
                print(f"- `{name}`")
            if len(missing) > 80:
                print(f"- ... {len(missing) - 80} more")
        else:
            print("- None.")
        print()
        print("Panthera top-level headers not in reference:")
        if extra:
            for name in extra[:80]:
                print(f"- `{name}`")
            if len(extra) > 80:
                print(f"- ... {len(extra) - 80} more")
        else:
            print("- None.")

    print()
    if failures:
        print("Result: FAIL")
        return 1
    print("Result: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
