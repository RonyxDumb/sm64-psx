"""Archive format regression tests. No proprietary game assets are required."""
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "pack_textures.py"
BANK_BYTES = 8192
SECTOR_BYTES = 2048


class TextureBankTest(unittest.TestCase):
    def test_shared_banks_and_exact_payloads(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            items = []
            sizes = [(8, 16), (16, 16), (32, 32), (64, 32), (64, 64), (128, 32)]
            for index in range(120):
                width, height = sizes[index % len(sizes)]
                # Unique payloads exercise palette and bitmap byte integrity.
                payload = bytes((index * 37 + i * 13) & 255 for i in range(32 + width * height // 2))
                header = struct.pack("<HHBB10x", width, height, 0, 0)
                path = folder / f"texture_{index:03d}.fulldata"
                path.write_bytes(header + payload)
                items.append((path, header, payload))

            listing = folder / "files.txt"
            listing.write_text("".join(f"{path}\n" for path, _, _ in items))
            archive = folder / "archive.bin"
            subprocess.run([sys.executable, str(SCRIPT), str(archive), str(listing)], check=True)
            data = archive.read_bytes()
            self.assertEqual(len(data) % BANK_BYTES, 0)
            self.assertLess(len(data), len(items) * SECTOR_BYTES)

            end_positions = []
            banks = set()
            for path, original_header, payload in items:
                hdr = path.with_suffix(".texheader").read_bytes()
                self.assertEqual(len(hdr), 20)
                self.assertEqual(hdr[:16], original_header)
                sector, offset = struct.unpack_from("<HH", hdr, 16)
                self.assertEqual(sector % (BANK_BYTES // SECTOR_BYTES), 0)
                self.assertEqual(offset % 4, 0)
                self.assertLessEqual(offset + len(payload), BANK_BYTES)
                pos = sector * SECTOR_BYTES + offset
                self.assertEqual(data[pos:pos + len(payload)], payload)
                end_positions.append((pos, pos + len(payload)))
                banks.add(sector)
            self.assertGreater(len(banks), 1)
            self.assertLess(len(banks), len(items))
            for previous, current in zip(end_positions, end_positions[1:]):
                self.assertLessEqual(previous[1], current[0])

    def test_rejects_incorrect_pixel_count(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            src = folder / "bad.fulldata"
            src.write_bytes(struct.pack("<HH12x", 16, 16) + bytes(4))
            listing = folder / "input.txt"
            listing.write_text(str(src) + "\n")
            p = subprocess.run(
                [sys.executable, str(SCRIPT), str(folder / "out.bin"), str(listing)],
                capture_output=True, text=True,
            )
            self.assertNotEqual(p.returncode, 0)
            self.assertIn("invalid 4-bpp", p.stderr)

    def test_14_pixel_font_preserves_each_dma_row(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            src = folder / "font.fulldata"
            palette = bytes(range(32))
            rows = [bytes([y + 1]) * 7 for y in range(14)]
            src.write_bytes(struct.pack("<HH12x", 14, 14) + palette + b"".join(rows))
            listing = folder / "input.txt"
            listing.write_text(str(src) + "\n")
            archive = folder / "archive.bin"
            subprocess.run([sys.executable, str(SCRIPT), str(archive), str(listing)], check=True)
            expected = palette + b"".join(row + b"\0" for row in rows)
            self.assertEqual(archive.read_bytes()[:len(expected)], expected)

    def test_ps1_and_packer_bank_sizes_agree(self):
        runtime = (ROOT / "src/port/psx/gfx_texture_psx.c").read_text()
        self.assertRegex(runtime, r"#define TEX_BANK_SECTORS 4\b")
        self.assertEqual(BANK_BYTES, 4 * SECTOR_BYTES)


if __name__ == "__main__":
    unittest.main()
