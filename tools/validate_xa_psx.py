#!/usr/bin/env python3
"""Check the 2336-byte Mode 2 payloads and the optional BGMINFO offsets."""

import argparse
from pathlib import Path
import struct

from interleave_xa import MISSING, SECTOR_INTERVAL, XA_SECTOR_SIZE, PADDING


def validate(pack, info=None, expect_tracks=None):
    bounds = list(struct.iter_unpack("<II", info.read_bytes())) if info else None
    counts = {}
    padding = 0
    with pack.open("rb") as source:
        for lba, sector in enumerate(iter(lambda: source.read(XA_SECTOR_SIZE), b"")):
            if sector == PADDING:
                padding += 1
                continue
            if len(sector) != XA_SECTOR_SIZE or sector[:4] != sector[4:8]:
                raise ValueError(f"sector {lba}: invalid XA header")
            track, channel, submode, coding = sector[:4]
            if channel != 0 or submode & 0x64 != 0x64 or coding != 0x01:
                raise ValueError(f"sector {lba}: expected 37.8 kHz 4-bit stereo XA")
            count = counts.get(track, 0)
            if bounds is not None:
                if track >= len(bounds):
                    raise ValueError(f"sector {lba}: track {track} is out of range")
                start, end = bounds[track]
                if lba != start + count * SECTOR_INTERVAL or lba >= end:
                    raise ValueError(f"sector {lba}: invalid spacing for track {track}")
            counts[track] = count + 1
    if bounds is not None:
        for track, (start, end) in enumerate(bounds):
            count = counts.get(track, 0)
            if start == MISSING and end == MISSING and count == 0:
                continue
            if not count or end - start != count * SECTOR_INTERVAL:
                raise ValueError(f"track {track}: metadata length does not match pack")
    if not counts and not padding:
        raise ValueError("empty XA pack")
    if expect_tracks is not None and set(counts) != set(range(expect_tracks)):
        raise ValueError(f"expected {expect_tracks} tracks numbered from 0, found {sorted(counts)}")
    print(f"XA validated: {len(counts)} tracks, {sum(counts.values())} audio sectors, {padding} padding sectors")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pack", type=Path)
    parser.add_argument("info", nargs="?", type=Path)
    parser.add_argument("--expect-tracks", type=int)
    args = parser.parse_args()
    try:
        validate(args.pack, args.info, args.expect_tracks)
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f"XA validation failed: {error}\n")
