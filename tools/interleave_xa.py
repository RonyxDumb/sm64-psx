#!/usr/bin/env python3
"""Pack 2x 37.8 kHz, 4-bit stereo XA tracks into eight CD sector lanes."""

import argparse
from collections import deque
from pathlib import Path
import struct

XA_SECTOR_SIZE = 2336
SECTOR_INTERVAL = 8
MISSING = 0xFFFFFFFF
PADDING = bytes(XA_SECTOR_SIZE)
ALTERNATE_MIXES = (range(7, 10), range(14, 17))


def pack(info_path, pack_path, inputs):
    tracks = []
    bounds = [(MISSING, MISSING)] * len(inputs)
    for number, name in enumerate(inputs):
        if name == "-":
            continue
        path = Path(name)
        count, remainder = divmod(path.stat().st_size, XA_SECTOR_SIZE)
        if not count or remainder:
            raise ValueError(f"{path}: expected a nonempty 2336-byte-sector XA file")
        tracks.append((count, number, path))

    # A stereo sector lasts 8 physical sectors at 2x. Keep each track in its
    # lane for its entire duration, and use the shortest lane for each new
    # track to keep the disc small even with dozens of tracks.
    lanes = [deque() for _ in range(SECTOR_INTERVAL)]
    lengths = [0] * SECTOR_INTERVAL
    by_number = {track[1]: track for track in tracks}

    def schedule(track, lane, row):
        count, number, path = track
        start = row * SECTOR_INTERVAL + lane
        bounds[number] = (start, start + count * SECTOR_INTERVAL)
        lanes[lane].append((row, count, number, path))
        lengths[lane] = row + count

    # Put synchronized mixes beside each other when possible. The drive can
    # switch to the same point in another mix without a long seek.
    for group in ALTERNATE_MIXES:
        mixes = [by_number[number] for number in group if number in by_number]
        if len(mixes) < 2:
            continue
        selected = sorted(range(SECTOR_INTERVAL), key=lengths.__getitem__)[:len(mixes)]
        row = max(lengths[lane] for lane in selected)
        for track, lane in zip(mixes, selected):
            schedule(track, lane, row)
            del by_number[track[1]]

    for track in sorted(by_number.values(), key=lambda track: (-track[0], track[1])):
        lane = lengths.index(min(lengths))
        schedule(track, lane, lengths[lane])

    streams = [None] * SECTOR_INTERVAL
    sectors_left = [0] * SECTOR_INTERVAL
    file_numbers = [0] * SECTOR_INTERVAL
    try:
        with Path(pack_path).open("wb") as output:
            for row in range(max(lengths)):
                for lane, queue in enumerate(lanes):
                    if not sectors_left[lane] and queue and queue[0][0] == row:
                        _, sectors_left[lane], file_numbers[lane], path = queue.popleft()
                        streams[lane] = path.open("rb")
                    if not sectors_left[lane]:
                        output.write(PADDING)
                        continue

                    sector = streams[lane].read(XA_SECTOR_SIZE)
                    if (len(sector) != XA_SECTOR_SIZE or sector[:4] != sector[4:8] or
                            sector[0] != file_numbers[lane] or sector[1] != 0 or
                            sector[2] & 0x64 != 0x64 or sector[3] != 0x01):
                        raise ValueError(f"Invalid 4-bit stereo XA sector in track {file_numbers[lane]}")
                    output.write(sector)
                    sectors_left[lane] -= 1
                    if not sectors_left[lane]:
                        streams[lane].close()
                        streams[lane] = None
            # Let the drive finish decoding the final audio sector before
            # polling the exclusive end position of the last stream.
            output.write(PADDING * (SECTOR_INTERVAL if tracks else 1))
    finally:
        for stream in streams:
            if stream is not None:
                stream.close()
    Path(info_path).write_bytes(b"".join(struct.pack("<II", *track) for track in bounds))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("info")
    parser.add_argument("pack")
    parser.add_argument("inputs", nargs="+")
    args = parser.parse_args()
    pack(args.info, args.pack, args.inputs)
