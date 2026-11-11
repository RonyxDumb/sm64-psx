"""Pack PS1 4-bpp texture payloads into sector-aligned, shared CD banks.

The 20-byte runtime TexHeader uses its final two u16 fields for the first
sector of an 8-KiB bank and the byte offset of a texture inside that bank.
Every payload stays wholly inside one bank; individual textures do not
consume an entire 2-KiB sector anymore. The output is padded to whole banks.
"""

from pathlib import Path
import struct
import sys

SECTOR_BYTES = 2048
BANK_SECTORS = 4
BANK_BYTES = SECTOR_BYTES * BANK_SECTORS
CLUT_BYTES = 16 * 2
HEADER_BYTES = 16
ALIGNMENT = 4


def pack(archive_path: Path, paths: list[Path]) -> None:
    output = bytearray()
    headers: list[tuple[Path, bytes]] = []

    for path in paths:
        if path.suffix != ".fulldata":
            raise ValueError(f"not a converted texture: {path}")
        contents = path.read_bytes()
        if len(contents) < HEADER_BYTES:
            raise ValueError(f"short texture header: {path}")
        width, height = struct.unpack_from("<HH", contents)
        size = CLUT_BYTES + (width * height // 2)
        payload = contents[HEADER_BYTES:]
        if width == 0 or height == 0 or width % 2 or len(payload) != size:
            raise ValueError(f"invalid 4-bpp texture payload: {path}: {width}x{height} ({len(payload)} bytes, expected {size})")
        if size > BANK_BYTES:
            raise ValueError(f"texture exceeds CD bank: {path}")

        # Align DMA source pointers; never let an upload straddle two banks.
        offset = len(output) % BANK_BYTES
        if offset + size > BANK_BYTES:
            output.extend(bytes(BANK_BYTES - offset))
            offset = 0
        if offset % ALIGNMENT:
            padding = ALIGNMENT - offset % ALIGNMENT
            output.extend(bytes(padding))
            offset += padding
        # 4-byte alignment can push the payload over the bank boundary.
        if offset + size > BANK_BYTES:
            output.extend(bytes(BANK_BYTES - offset))
            offset = 0

        bank_first_sector = (len(output) // BANK_BYTES) * BANK_SECTORS
        if bank_first_sector > 0xFFFF or offset > 0xFFFF:
            raise ValueError("texture archive exceeds 16-bit sector/offset fields")
        headers.append((
            path.with_suffix(".texheader"),
            contents[:HEADER_BYTES] + struct.pack("<HH", bank_first_sector, offset),
        ))
        output.extend(payload)

    if output:
        output.extend(bytes((-len(output)) % BANK_BYTES))
    archive_path.write_bytes(output)
    for header_path, content in headers:
        header_path.write_bytes(content)


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: pack_textures.py <archive> <fulldata_list>")
    filenames = Path(sys.argv[2]).read_text().splitlines()
    pack(Path(sys.argv[1]), [Path(line) for line in filenames if line.strip()])


if __name__ == "__main__":
    main()
