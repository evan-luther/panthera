#!/usr/bin/env python3
"""Audit Panthera's current pkgsrc/bootstrap prerequisites.

This is intentionally a host-side source/staging audit. It answers whether the
default root image inputs contain the tools needed to try pkgsrc bootstrap from
inside Panthera; it does not claim the tools work in guest unless paired with a
boot or SSH smoke.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]


@dataclass(frozen=True)
class Check:
    name: str
    paths: tuple[str, ...]
    required: bool
    note: str


CHECKS = (
    Check("shell:/bin/sh", ("userland/shell/zsh",), True, "base manifest stages zsh as /bin/sh"),
    Check("awk", ("userland/awk/bin/awk",), True, "pkgsrc bootstrap scripts use awk"),
    Check("sed", ("userland/coreutils/bin/sed",), True, "pkgsrc/bootstrap scripts use sed"),
    Check("curl", ("userland/curl/bin/curl",), True, "source fetch path"),
    Check("tar", ("userland/libarchive/bin/bsdtar",), True, "pkgsrc.tar.gz extraction"),
    Check("patch", ("userland/patch_cmds/bin/patch",), True, "patch application"),
    Check("bzip2", ("userland/bzip2/bin/bzip2",), False, "common package distfiles"),
    Check("gzip", ("userland/file_cmds/bin/gzip", "userland/gzip/bin/gzip"), False, "standalone gzip command"),
    Check("install", ("userland/file_cmds/bin/install", "userland/system_cmds/bin/install"), True, "installing bootstrap outputs"),
    Check("mtree", ("userland/file_cmds/bin/mtree", "userland/system_cmds/bin/mtree"), False, "BSD packaging metadata"),
    Check("make", ("userland/make/bin/make", "userland/bmake/bin/bmake", "userland/system_cmds/bin/make"), True, "needed after bootstrap starts"),
    Check("cc", ("userland/clang/bin/cc", "userland/clang/bin/clang", "userland/gcc/bin/gcc", "userland/llvm/bin/clang", "userland/cctools/bin/cc"), True, "in-guest C compiler for bootstrap"),
    Check("ar", ("userland/cctools/bin/ar", "userland/binutils/bin/ar"), True, "archive tool for compiler builds"),
    Check("ranlib", ("userland/cctools/bin/ranlib", "userland/binutils/bin/ranlib"), True, "archive index tool"),
    Check("ld", ("userland/ld64/bin/ld", "userland/cctools/bin/ld", "userland/llvm/bin/ld64"), True, "linker"),
)


def exists(rel_paths: tuple[str, ...]) -> tuple[bool, str]:
    for rel in rel_paths:
        if (ROOT / rel).exists():
            return True, rel
    return False, ", ".join(rel_paths)


def main() -> int:
    failures: list[Check] = []
    warnings: list[Check] = []
    rows: list[tuple[Check, str, str]] = []

    for check in CHECKS:
        ok, detail = exists(check.paths)
        if ok:
            result = "PASS"
        elif check.required:
            result = "FAIL"
            failures.append(check)
        else:
            result = "WARN"
            warnings.append(check)
        rows.append((check, result, detail))

    print("# Panthera pkgsrc prerequisite audit")
    print()
    print(f"Result: {'FAIL' if failures else 'PASS'}")
    print()
    print("| Check | Result | Detail | Note |")
    print("|---|---|---|---|")
    for check, result, detail in rows:
        print(f"| {check.name} | {result} | `{detail}` | {check.note} |")

    print()
    print("## Blocking Gaps")
    if failures:
        for check in failures:
            print(f"- {check.name}: {check.note}")
    else:
        print("- None.")

    print()
    print("## Warnings")
    if warnings:
        for check in warnings:
            print(f"- {check.name}: {check.note}")
    else:
        print("- None.")

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
