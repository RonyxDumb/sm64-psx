#!/usr/bin/env python3
"""
Reorder makextfiles' manifest so files belonging to the same SM64 level are
physically adjacent inside EXT.DAT.

This does not change symbols or virtual addresses. makextfiles still computes
the final offsets and --defsym values after this step. It only improves locality
on the CD image, reducing seek distance when a level loads leveldata + scriptgeo.
"""

from __future__ import annotations
import re
import sys
from collections import OrderedDict
from pathlib import Path

LEVEL_RE = re.compile(r"(?:^|/)levels/([^/.:!]+)(?:/|\\.)")

def level_key(line: str):
    # Only inspect the file path before the first ':'.
    file_part = line.split(":", 1)[0].replace("\\\\", "/")
    m = LEVEL_RE.search(file_part)
    return m.group(1) if m else None

def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} INPUT_MANIFEST OUTPUT_MANIFEST", file=sys.stderr)
        return 2

    src = Path(sys.argv[1])
    dst = Path(sys.argv[2])
    lines = [ln for ln in src.read_text().splitlines() if ln.strip()]

    grouped: dict[str, list[str]] = OrderedDict()
    order: list[tuple[str, str | int]] = []
    seen_levels: set[str] = set()

    for idx, line in enumerate(lines):
        level = level_key(line)
        if level is None:
            order.append(("raw", idx))
            continue

        grouped.setdefault(level, []).append(line)
        if level not in seen_levels:
            seen_levels.add(level)
            order.append(("level", level))

    output: list[str] = []
    for kind, value in order:
        if kind == "raw":
            output.append(lines[int(value)])
        else:
            output.extend(grouped[str(value)])

    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text("\n".join(output) + "\n")
    print(f"PSX EXT.DAT locality: {len(lines)} entries, {len(grouped)} level groups")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
