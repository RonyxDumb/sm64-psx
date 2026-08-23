#!/usr/bin/env python3
"""Validate the memory layout and deliverables of a retail PSX build."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

RETAIL_APP_END = 0x801F8000
RETAIL_RAM_TOP = 0x80200000
ICACHE_LIMIT = 0x1000


def parse_symbol(map_text: str, symbol: str) -> int | None:
    match = re.search(
        rf"^\s*(0x[0-9a-fA-F]+)\s+{re.escape(symbol)}\s*=",
        map_text,
        re.MULTILINE,
    )
    return int(match.group(1), 16) if match else None


def parse_dl_exec_size(map_text: str) -> int | None:
    match = re.search(
        r"^\s*\.dl_exec\s+0x[0-9a-fA-F]+\s+(0x[0-9a-fA-F]+)",
        map_text,
        re.MULTILINE,
    )
    return int(match.group(1), 16) if match else None


def require_file(path: Path, errors: list[str]) -> None:
    if not path.is_file() or path.stat().st_size == 0:
        errors.append(f"missing or empty artifact: {path}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args()

    build_dir = args.build_dir
    elf = build_dir / "sm64.elf"
    exe = build_dir / "sm64.exe"
    iso = build_dir / "sm64.iso"
    cue = build_dir / "sm64.cue"
    map_file = build_dir / "sm64.map"
    errors: list[str] = []

    for artifact in (elf, exe, iso, cue, map_file):
        require_file(artifact, errors)

    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1

    if elf.read_bytes()[:4] != b"\x7fELF":
        errors.append(f"invalid ELF signature: {elf}")
    if exe.read_bytes()[:8] != b"PS-X EXE":
        errors.append(f"invalid PS-X EXE signature: {exe}")

    map_text = map_file.read_text(encoding="utf-8", errors="replace")
    bss_end = parse_symbol(map_text, "_bssEnd")
    stack_bottom = parse_symbol(map_text, "_stackBottom")
    stack_top = parse_symbol(map_text, "_stackTop")
    dl_exec_size = parse_dl_exec_size(map_text)

    for name, value in (
        ("_bssEnd", bss_end),
        ("_stackBottom", stack_bottom),
        ("_stackTop", stack_top),
        (".dl_exec", dl_exec_size),
    ):
        if value is None:
            errors.append(f"could not read {name} from {map_file}")

    if not errors:
        assert bss_end is not None
        assert stack_bottom is not None
        assert stack_top is not None
        assert dl_exec_size is not None

        if bss_end >= RETAIL_APP_END:
            errors.append(f"_bssEnd 0x{bss_end:08x} reaches the 2 MiB stack reservation")
        if stack_bottom != RETAIL_APP_END:
            errors.append(
                f"_stackBottom is 0x{stack_bottom:08x}, expected 0x{RETAIL_APP_END:08x}"
            )
        if not stack_bottom < stack_top < RETAIL_RAM_TOP:
            errors.append(
                f"invalid stack interval: 0x{stack_bottom:08x}..0x{stack_top:08x}"
            )
        if bss_end > stack_bottom:
            errors.append("application sections overlap the reserved stack")
        if dl_exec_size >= ICACHE_LIMIT:
            errors.append(
                f".dl_exec is 0x{dl_exec_size:x}, not below the 0x{ICACHE_LIMIT:x} I-cache budget"
            )

    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1

    assert bss_end is not None
    assert stack_bottom is not None
    assert stack_top is not None
    assert dl_exec_size is not None
    print("Retail PSX verification passed")
    print(f"  mapped slack: 0x{stack_bottom - bss_end:x} ({stack_bottom - bss_end} bytes)")
    print(f"  stack interval: 0x{stack_bottom:08x}..0x{stack_top:08x} ({stack_top - stack_bottom} bytes)")
    print(f"  .dl_exec: 0x{dl_exec_size:x} ({ICACHE_LIMIT - dl_exec_size} bytes I-cache margin)")
    for artifact in (elf, exe, iso, cue):
        print(f"  artifact: {artifact}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
