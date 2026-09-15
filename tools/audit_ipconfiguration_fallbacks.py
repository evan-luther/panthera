#!/usr/bin/env python3
"""Audit temporary Panthera fallbacks in staged configd/IPConfiguration."""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


@dataclass(frozen=True)
class Fallback:
    ident: str
    owner: str
    risk: str
    path: str
    patterns: tuple[str, ...]
    reason: str
    replacement: str
    verification: str


FALLBACKS: tuple[Fallback, ...] = ()


def find_patterns(root: Path, fallback: Fallback) -> tuple[bool, list[str]]:
    path = root / fallback.path
    if not path.exists():
        return False, [f"missing file {fallback.path}"]
    text = path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()
    locations: list[str] = []
    ok = True
    for pattern in fallback.patterns:
        regex = re.compile(pattern)
        match_location = None
        for lineno, line in enumerate(lines, start=1):
            if regex.search(line):
                match_location = f"{fallback.path}:{lineno}"
                break
        if match_location is None:
            ok = False
            locations.append(f"missing pattern `{pattern}`")
        else:
            locations.append(match_location)
    return ok, locations


def markdown_report(results: list[tuple[Fallback, bool, list[str]]]) -> str:
    now = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")
    missing = [fallback for fallback, ok, _ in results if not ok]
    by_risk: dict[str, int] = {}
    for fallback, _, _ in results:
        by_risk[fallback.risk] = by_risk.get(fallback.risk, 0) + 1

    lines = [
        "# IPConfiguration Fallback Audit",
        "",
        f"Generated: {now}",
        "",
        "## Result",
        "",
        f"Status: {'PASS' if not missing else 'FAIL'}",
        f"Tracked fallbacks: {len(results)}",
        f"Missing anchors: {len(missing)}",
        "",
        "## Risk Counts",
        "",
        "| Risk | Count |",
        "|---|---:|",
    ]
    for risk, count in sorted(by_risk.items()):
        lines.append(f"| {risk} | {count} |")

    lines.extend(
        [
            "",
            "## Fallbacks",
            "",
            "| ID | Status | Owner | Risk | Anchors | Replacement | Verification |",
            "|---|---|---|---|---|---|---|",
        ]
    )
    for fallback, ok, locations in results:
        anchors = "<br>".join(f"`{location}`" for location in locations)
        lines.append(
            "| "
            + " | ".join(
                [
                    fallback.ident,
                    "PASS" if ok else "FAIL",
                    fallback.owner,
                    fallback.risk,
                    anchors,
                    fallback.replacement,
                    fallback.verification,
                ]
            )
            + " |"
        )

    lines.extend(["", "## Cleanup Notes", ""])
    for fallback, _, _ in results:
        lines.append(f"- `{fallback.ident}`: {fallback.reason}")

    if missing:
        lines.extend(["", "## Blocking Missing Anchors", ""])
        for fallback in missing:
            lines.append(f"- `{fallback.ident}` in `{fallback.path}`")

    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        type=Path,
        help="Write the markdown audit to this path instead of stdout.",
    )
    args = parser.parse_args()

    results = [(fallback, *find_patterns(ROOT, fallback)) for fallback in FALLBACKS]
    report = markdown_report(results)
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(report, encoding="utf-8")
    else:
        print(report, end="")
    return 1 if any(not ok for _, ok, _ in results) else 0


if __name__ == "__main__":
    raise SystemExit(main())
