#!/usr/bin/env python3

import sys
from pathlib import Path


def move_observer_type_before_workloop(path: Path) -> None:
    text = path.read_text()
    definition = """typedef struct dispatch_pthread_root_queue_observer_hooks_s {
\tvoid (*queue_will_execute)(dispatch_queue_t queue);
\tvoid (*queue_did_execute)(dispatch_queue_t queue);
} dispatch_pthread_root_queue_observer_hooks_s;
typedef dispatch_pthread_root_queue_observer_hooks_s
\t\t*dispatch_pthread_root_queue_observer_hooks_t;
"""
    anchor = "#define DISPATCH_WORKLOOP_ATTR_HAS_SCHED"

    definition_index = text.find(definition)
    anchor_index = text.find(anchor)
    if definition_index < 0 or anchor_index < 0:
        raise RuntimeError(f"unexpected queue_internal.h layout: {path}")
    if definition_index < anchor_index:
        return

    text = text[:definition_index] + text[definition_index + len(definition):]
    anchor_index = text.index(anchor)
    text = text[:anchor_index] + definition + "\n" + text[anchor_index:]
    path.write_text(text)


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_sources.py QUEUE_INTERNAL_H")
    move_observer_type_before_workloop(Path(sys.argv[1]))


if __name__ == "__main__":
    main()
