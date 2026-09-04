#!/usr/bin/env python3
from __future__ import annotations

import argparse
import binascii
import json
from pathlib import Path
import re
import struct
import zlib

TILE_W = 32
TILE_H = 32
RAW_TILE_SIZE = TILE_W * TILE_H * 2
TEX_HEADER_SIZE = 20

def parse_numeric_bytes(body: str) -> bytes:
    tokens = re.findall(r"0[xX][0-9A-Fa-f]+|\b\d+\b", body)
    values = [int(token, 0) for token in tokens]
    if any(value < 0 or value > 255 for value in values):
        raise ValueError("skyconv array contains value outside u8 range")
    return bytes(values)

def rgba16_to_rgba8(raw: bytes) -> bytes:
    if len(raw) != RAW_TILE_SIZE:
        raise ValueError(f"expected {RAW_TILE_SIZE} bytes, got {len(raw)}")
    out = bytearray(TILE_W * TILE_H * 4)
    for i in range(TILE_W * TILE_H):
        b0 = raw[i * 2]
        b1 = raw[i * 2 + 1]
        r5 = (b0 & 0xF8) >> 3
        g5 = ((b0 & 0x07) << 2) | ((b1 & 0xC0) >> 6)
        b5 = (b1 & 0x3E) >> 1
        out[i * 4 + 0] = (r5 * 255) // 31
        out[i * 4 + 1] = (g5 * 255) // 31
        out[i * 4 + 2] = (b5 * 255) // 31
        out[i * 4 + 3] = 255 if (b1 & 1) else 0
    return bytes(out)

def png_chunk(kind: bytes, payload: bytes) -> bytes:
    crc = binascii.crc32(kind)
    crc = binascii.crc32(payload, crc) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", crc)

def write_rgba_png(path: Path, width: int, height: int, rgba: bytes) -> None:
    stride = width * 4
    scanlines = bytearray()
    for y in range(height):
        scanlines.append(0)
        scanlines.extend(rgba[y * stride:(y + 1) * stride])

    data = bytearray(b"\x89PNG\r\n\x1a\n")
    data.extend(png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)))
    data.extend(png_chunk(b"IDAT", zlib.compress(bytes(scanlines), 9)))
    data.extend(png_chunk(b"IEND", b""))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)

def parse_legacy_skybox(path: Path, name: str) -> dict:
    text = path.read_text(encoding="utf-8")
    array_re = re.compile(
        rf"ALIGNED8\s+static\s+const\s+Texture\s+{re.escape(name)}_skybox_texture_([0-9A-Fa-f]+)\[\]\s*=\s*\{{(.*?)\}};",
        re.DOTALL,
    )
    arrays = {}
    for match in array_re.finditer(text):
        tile_id = f"{int(match.group(1), 16):05X}"
        raw = parse_numeric_bytes(match.group(2))
        if len(raw) != RAW_TILE_SIZE:
            raise ValueError(f"{path}: tile {tile_id} is {len(raw)} bytes")
        arrays[tile_id] = raw

    if not arrays:
        raise ValueError(f"{path}: no tile arrays found")

    ptr_re = re.compile(
        rf"const\s+Texture\s*\*const\s+{re.escape(name)}_skybox_ptrlist\[\]\s*=\s*\{{(.*?)\}};",
        re.DOTALL,
    )
    match = ptr_re.search(text)
    if match is None:
        raise ValueError(f"{path}: ptrlist not found")

    refs = [
        f"{int(value, 16):05X}"
        for value in re.findall(
            rf"{re.escape(name)}_skybox_texture_([0-9A-Fa-f]+)",
            match.group(1),
        )
    ]
    if len(refs) != 80:
        raise ValueError(f"{path}: expected 80 ptrlist entries, found {len(refs)}")
    missing = sorted(set(refs) - set(arrays))
    if missing:
        raise ValueError(f"{path}: ptrlist refers to missing tiles {missing}")
    return {"arrays": arrays, "ptrlist": refs}

def extract_assets(legacy_dir: Path, tile_dir: Path, manifest_path: Path, names: list[str]) -> None:
    tile_dir.mkdir(parents=True, exist_ok=True)
    manifest = {"format": 1, "skyboxes": {}}

    for name in names:
        parsed = parse_legacy_skybox(legacy_dir / f"{name}_skybox.c", name)
        tile_ids = sorted(set(parsed["ptrlist"]), key=lambda x: int(x, 16))

        for tile_id in tile_ids:
            png = tile_dir / f"{name}.{tile_id}.rgba16.png"
            write_rgba_png(png, TILE_W, TILE_H, rgba16_to_rgba8(parsed["arrays"][tile_id]))

        manifest["skyboxes"][name] = {
            "tiles": tile_ids,
            "ptrlist": parsed["ptrlist"],
        }
        print(f"{name}: {len(tile_ids)} unique tiles / 80 table entries")

    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

SKYBOX_RUNTIME_ORDER = [
    "water",
    "bitfs",
    "wdw",
    "cloud_floor",
    "ccm",
    "ssl",
    "bbh",
    "bidw",
    "clouds",
    "bits",
]

RECORD_HEADER_BYTES = 84  # count + 80 mapping bytes + 3 bytes padding
MAX_UNIQUE = 64
RUNTIME_RECORD_SIZE = RECORD_HEADER_BYTES + MAX_UNIQUE * TEX_HEADER_SIZE
EXTERNAL_RECORD_STRIDE = 2048
assert RUNTIME_RECORD_SIZE <= EXTERNAL_RECORD_STRIDE

def emit_bin(manifest_path: Path, tile_dir: Path, output: Path) -> None:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    skyboxes = manifest["skyboxes"]
    blob = bytearray()

    for name in SKYBOX_RUNTIME_ORDER:
        if name not in skyboxes:
            raise ValueError(f"manifest does not contain skybox {name}")

        info = skyboxes[name]
        tile_ids = info["tiles"]
        if not 1 <= len(tile_ids) <= MAX_UNIQUE:
            raise ValueError(
                f"{name}: expected 1..{MAX_UNIQUE} unique tiles, got {len(tile_ids)}"
            )

        index_for_tile = {tile_id: i for i, tile_id in enumerate(tile_ids)}

        # One external record per CD sector. The runtime only DMA-reads
        # RUNTIME_RECORD_SIZE bytes from the beginning of each sector.
        record = bytearray(EXTERNAL_RECORD_STRIDE)
        record[0] = len(tile_ids)

        for i, tile_id in enumerate(info["ptrlist"]):
            record[1 + i] = index_for_tile[tile_id]

        header_offset = RECORD_HEADER_BYTES
        for i, tile_id in enumerate(tile_ids):
            header_path = tile_dir / f"{name}.{tile_id}.rgba16.texheader"
            raw = header_path.read_bytes()
            if len(raw) != TEX_HEADER_SIZE:
                raise ValueError(
                    f"{header_path}: expected {TEX_HEADER_SIZE} bytes, got {len(raw)}"
                )
            dst = header_offset + i * TEX_HEADER_SIZE
            record[dst:dst + TEX_HEADER_SIZE] = raw

        blob.extend(record)
        print(
            f"{name}: {len(tile_ids)} unique headers, "
            f"{RUNTIME_RECORD_SIZE} bytes runtime / {EXTERNAL_RECORD_STRIDE} bytes external stride"
        )

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(blob)
    print(f"wrote {output} ({len(blob)} bytes, external EXT.DAT metadata)")

def main() -> int:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("extract")
    p.add_argument("legacy_dir", type=Path)
    p.add_argument("tile_dir", type=Path)
    p.add_argument("manifest", type=Path)
    p.add_argument("names", nargs="+")
    p.set_defaults(run=lambda a: extract_assets(a.legacy_dir, a.tile_dir, a.manifest, a.names))

    p = sub.add_parser("emit-bin")
    p.add_argument("manifest", type=Path)
    p.add_argument("tile_dir", type=Path)
    p.add_argument("output", type=Path)
    p.set_defaults(run=lambda a: emit_bin(a.manifest, a.tile_dir, a.output))

    args = parser.parse_args()
    args.run(args)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
