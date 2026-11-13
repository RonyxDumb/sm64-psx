#!/usr/bin/env python3
"""Rewrite the 16-sector ISO system area of a raw MODE2/2352 image as empty
Mode 2 Form 1 sectors with valid EDC/ECC.

Without a license file, mkpsxiso fills sectors 0-15 with Mode 2 *Form 2*
sectors. Emulators that inspect the system area the way the BIOS does (e.g.
Mednafen/Beetle PSX read sectors 4-11 as Form 1 data) then reject the disc as
"not a PS1 disc" and the drive refuses every data read. Empty Form 1 sectors
contain no license data; disc detection then falls back to the ISO9660
"PLAYSTATION" volume descriptor.

usage: psx_system_area.py image.iso
"""
import sys

SECTOR = 2352
SYSTEM_AREA_SECTORS = 16

ECC_F = [0] * 256
ECC_B = [0] * 256
EDC = [0] * 256
for i in range(256):
	j = ((i << 1) ^ (0x11D if i & 0x80 else 0)) & 0xFF
	ECC_F[i] = j
	ECC_B[i ^ j] = i
	e = i
	for _ in range(8):
		e = (e >> 1) ^ (0xD8018001 if e & 1 else 0)
	EDC[i] = e


def edc(data):
	e = 0
	for b in data:
		e = (e >> 8) ^ EDC[(e ^ b) & 0xFF]
	return e


def ecc_block(sector, major_count, minor_count, major_mult, minor_inc, dest):
	# Source starts at the sector header (offset 12), as in the CD-ROM spec.
	size = major_count * minor_count
	for major in range(major_count):
		index = (major >> 1) * major_mult + (major & 1)
		a = b = 0
		for _ in range(minor_count):
			t = sector[12 + index]
			index += minor_inc
			if index >= size:
				index -= size
			a ^= t
			b ^= t
			a = ECC_F[a]
		a = ECC_B[ECC_F[a] ^ b]
		sector[dest + major] = a
		sector[dest + major + major_count] = a ^ b


def form1_sector(header):
	s = bytearray(SECTOR)
	s[0:12] = b"\x00" + b"\xFF" * 10 + b"\x00"
	s[12:16] = header  # BCD MSF + mode 2
	s[16:24] = bytes([0, 0, 0x08, 0] * 2)  # subheader: data, Form 1
	s[2072:2076] = edc(s[16:2072]).to_bytes(4, "little")
	saved = bytes(s[12:16])
	s[12:16] = b"\0\0\0\0"  # Mode 2 ECC treats the header as zero
	ecc_block(s, 86, 24, 2, 86, 2076)  # P parity
	ecc_block(s, 52, 43, 86, 88, 2248)  # Q parity
	s[12:16] = saved
	return bytes(s)


def main(path):
	with open(path, "r+b") as f:
		for lba in range(SYSTEM_AREA_SECTORS):
			f.seek(lba * SECTOR)
			raw = f.read(SECTOR)
			if len(raw) != SECTOR or raw[15] != 2:
				sys.exit(f"{path}: sector {lba} is not a raw Mode 2 sector")
			if any(raw[24:2072]) and raw[18] & 0x20 == 0:
				continue  # real Form 1 license data: keep it untouched
			f.seek(lba * SECTOR)
			f.write(form1_sector(raw[12:16]))


if __name__ == "__main__":
	if len(sys.argv) != 2:
		sys.exit(__doc__)
	main(sys.argv[1])
