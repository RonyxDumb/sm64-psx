#!/usr/bin/env python3
"""
Validate the interleaved XA pack produced by tools/interleave_xa.py.

IMPORTANT:
This project does NOT store 2352-byte raw CD sectors in pack.xa.
interleave_xa.py explicitly uses 2336-byte XA sectors (Mode 2 sector payload,
without the 12-byte sync + 4-byte sector header).

Layout of each 2336-byte XA sector:
    0..3   XA subheader copy 1: file, channel, submode, coding
    4..7   XA subheader copy 2: duplicate
    8..    XA payload

Padding sectors inserted by interleave_xa.py are all-zero 2336-byte sectors and
are accepted by this validator.
"""

from __future__ import annotations

import sys
from pathlib import Path

XA_SECTOR_SIZE = 2336

def fail(msg: str) -> int:
    print(f"XA validation FAILED: {msg}", file=sys.stderr)
    return 1

def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} PACK.XA", file=sys.stderr)
        return 2

    path = Path(sys.argv[1])
    data = path.read_bytes()

    if not data:
        return fail("empty file")

    if len(data) % XA_SECTOR_SIZE:
        return fail(
            f"size {len(data)} is not a multiple of project XA sector size "
            f"{XA_SECTOR_SIZE}"
        )

    sectors = len(data) // XA_SECTOR_SIZE
    audio_sectors = 0
    padding_sectors = 0

    for n in range(sectors):
        sec = data[n * XA_SECTOR_SIZE:(n + 1) * XA_SECTOR_SIZE]

        # interleave_xa.py pads missing streams with an all-zero 2336-byte sector.
        if not any(sec):
            padding_sectors += 1
            continue

        sub_a = sec[0:4]
        sub_b = sec[4:8]

        if sub_a != sub_b:
            return fail(
                f"sector {n}: duplicated XA subheaders differ "
                f"({sub_a.hex()} != {sub_b.hex()})"
            )

        file_no, channel_no, submode, coding = sub_a

        # XA submode bit 2 = audio.
        if not (submode & 0x04):
            return fail(
                f"sector {n}: expected XA audio sector, "
                f"submode=0x{submode:02X}"
            )

        audio_sectors += 1

        # XA coding-info bit 4 selects 8-bit ADPCM.
        # Hardware target for this port is 4-bit, so bit 4 must be clear.
        if coding & 0x10:
            return fail(
                f"sector {n}: 8-bit XA ADPCM detected "
                f"(file={file_no}, channel={channel_no}, coding=0x{coding:02X}); "
                "expected 4-bit"
            )

    if audio_sectors == 0:
        return fail("no XA audio sectors found")

    print(
        "XA validation OK: "
        f"{sectors} sectors x {XA_SECTOR_SIZE} bytes, "
        f"{audio_sectors} audio, {padding_sectors} padding, "
        "4-bit ADPCM"
    )
    return 0

if __name__ == "__main__":
    raise SystemExit(main())