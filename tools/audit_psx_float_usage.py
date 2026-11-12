#!/usr/bin/env python3
"""Inventory source-level floating-point types, operations and conversions.

An audit is not a proof that a path executes on PS1: conditional compilation
and link reachability must be reviewed for every reported call site. Run on
every CI push to make the remaining migration work observable.
"""
from __future__ import annotations

from collections import Counter
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent.parent
SOURCE_ROOTS = ("src", "include", "lib", "actors", "levels", "assets", "bin", "data", "sound", "text", "tools", "ps1-bare-metal")
EXTENSIONS = {".c", ".h", ".cpp", ".cc"}
TOKENS = re.compile(r"\b(?:float|double|f32|f64|sqrtf|sinf|cosf|ftoq|qtof)\b")
COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.DOTALL)


def category(path: Path) -> str:
    rel = path.relative_to(ROOT).as_posix()
    if rel.startswith(("src/port/pc/", "src/port/saturn/", "src/goddard_og/")):
        return "non-PS1 renderer/reference"
    if rel.startswith(("tools/", "ps1-bare-metal/", "lib/")):
        return "build tools / shared libraries"
    if rel.startswith(("actors/", "levels/", "assets/", "bin/", "data/", "sound/", "text/")):
        return "game assets / generated sources"
    return "PS1-relevant game and port sources"


def main() -> None:
    totals = Counter()
    files_with_matches = Counter()
    hotspots: list[tuple[int, str, str]] = []
    scanned = 0
    for parent in SOURCE_ROOTS:
        root = ROOT / parent
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if path.suffix not in EXTENSIONS or not path.is_file():
                continue
            scanned += 1
            content = COMMENT.sub("", path.read_text(encoding="utf-8", errors="replace"))
            matches = TOKENS.findall(content)
            if not matches:
                continue
            group = category(path)
            totals[group] += len(matches)
            files_with_matches[group] += 1
            hotspots.append((len(matches), path.relative_to(ROOT).as_posix(), group))

    print(f"Audited {scanned} C/C++ source/header files")
    for group in sorted(totals):
        print(f"{group}: {totals[group]} token references in {files_with_matches[group]} files")
    print("Top sources to inspect (not necessarily active at runtime):")
    for count, name, group in sorted(hotspots, reverse=True)[:20]:
        print(f"  {count:4}  {name}  [{group}]")
    print("NOTE: this inventory includes identifiers in disabled #if branches.")
    print("Remaining references require caller/data-layout audits, not blanket int casts.")


if __name__ == "__main__":
    main()
