#!/usr/bin/env python3
"""Check the original intro note, note gates and XA cadence without a ROM."""

from pathlib import Path
import re
import struct
import subprocess
import sys
from tempfile import TemporaryDirectory

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from import_kondo_soundtrack import TRACKS
from interleave_xa import MISSING, SECTOR_INTERVAL, XA_SECTOR_SIZE, pack


def check_sfx(directory):
    source = directory / "sfx.c"
    subprocess.run(
        [sys.executable, str(ROOT / "tools/sound_player_to_c.py"), "us",
         str(ROOT / "sound/sequences/00_sound_player.s"), str(source)],
        check=True,
    )
    table = source.read_text().split("sfx_first_def_per_channel[] = {", 1)[1].split("}", 1)[0]
    first_defs = [int(n) for n in re.findall(r"\d+", table)]
    data = (directory / "sfx_data.generated.bin").read_bytes()
    _, _, _, defs, layers, events, _ = struct.unpack_from("<IHHIIII", data)

    def sound_events(bank, sound_id):
        first_layer, layer_count = struct.unpack_from("<HB", data, defs + 4 * (first_defs[bank] + sound_id))
        result = []
        for index in range(first_layer, first_layer + layer_count):
            first_event, event_count, _ = struct.unpack_from("<IHH", data, layers + 8 * index)
            result.extend(struct.unpack_from("<HHHHBBBB", data, events + 20 * event)
                          for event in range(first_event, first_event + event_count))
        return result

    intro = sound_events(7, 0x14)
    assert any((start, gate, bank, instr, note) == (87, 200, 10, 8, 39)
               for start, gate, _, _, bank, instr, note, _ in intro)

    red_coin = sound_events(7, 0x28)
    assert any(start == 6 and gate == 12 and instr == 128
               for start, gate, _, _, _, instr, _, _ in red_coin)


def check_xa(directory):
    inputs = []
    payloads = {}
    missing = {1, 13, 31}
    assert len(TRACKS) == 38
    for number in range(len(TRACKS)):
        if number in missing:
            inputs.append("-")
            continue
        header = bytes((number, 0, 0x64, 0x01)) * 2
        payloads[number] = [header + bytes([i]) * (XA_SECTOR_SIZE - 8)
                            for i in range(1, number % 7 + 2)]
        track = directory / f"{number}.xa"
        track.write_bytes(b"".join(payloads[number]))
        inputs.append(track)

    info = directory / "info.dat"
    packed = directory / "pack.xa"
    pack(info, packed, inputs)
    bounds = list(struct.iter_unpack("<II", info.read_bytes()))
    assert len(bounds) == len(TRACKS)
    seen = set()
    data = packed.read_bytes()
    for number, (start, end) in enumerate(bounds):
        if number in missing:
            assert (start, end) == (MISSING, MISSING)
            continue
        assert (end - start) == SECTOR_INTERVAL * len(payloads[number])
        for i, sector in enumerate(range(start, end, SECTOR_INTERVAL)):
            assert sector not in seen
            seen.add(sector)
            pos = sector * XA_SECTOR_SIZE
            assert data[pos:pos + XA_SECTOR_SIZE] == payloads[number][i]
    assert len(data) >= max(end for start, end in bounds if start != MISSING) * XA_SECTOR_SIZE
    assert not any(data[-XA_SECTOR_SIZE:])

    pack(info, packed, ["-"] * len(TRACKS))
    assert set(struct.iter_unpack("<II", info.read_bytes())) == {(MISSING, MISSING)}
    assert packed.read_bytes() == bytes(XA_SECTOR_SIZE)


with TemporaryDirectory() as temporary:
    path = Path(temporary)
    check_sfx(path)
    check_xa(path)
print("PS1 sound-effect and XA packing checks passed")
