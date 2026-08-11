import sys
import struct

XA_SECTOR_SIZE = 2336
SECTOR_INTERVAL = 4

if len(sys.argv) < 4:
	print(f"usage: {sys.argv[0]} <output_info.dat> <output_pack.xa> <inputs.xa...>\n")
	exit()

out_info_bytes = bytearray()
padding_sector = bytes(0 for _ in range(XA_SECTOR_SIZE))
out_pack_bytes = bytearray()

class Input:
	__slots__ = "data", "off", "start_lba_off", "end_lba_off"
	data: bytes
	off: int
	start_lba_off: int
	end_lba_off: int

	def __init__(self, data: bytes):
		self.data = data
		self.off = 0
		self.start_lba_off = -1
		self.end_lba_off = -1
		assert len(data) > 0 and len(data) % XA_SECTOR_SIZE == 0

	def next_sector(self) -> bytes:
		next_off = self.off + XA_SECTOR_SIZE
		sector = self.data[self.off:next_off]
		self.off = next_off
		return sector

	def is_finished(self):
		return self.off >= len(self.data)

all_inputs: list[Input] = []

for path in sys.argv[3:]:
	with open(path, "rb") as input:
		all_inputs.append(Input(input.read()))

remaining_inputs = all_inputs.copy()

while True:
	for inp in remaining_inputs:
		if not inp.is_finished():
			break
	else:
		break # if all inputs are finished, stop
	# add SECTOR_INTERVAL sectors, one from each input, but for any that have ended, pad with zeros instead
	cur_lba_off = len(out_pack_bytes) // XA_SECTOR_SIZE
	for i in range(SECTOR_INTERVAL):
		if i >= len(remaining_inputs):
			out_pack_bytes += padding_sector
		else:
			inp = remaining_inputs[i]
			if inp.off == 0:
				inp.start_lba_off = cur_lba_off
			out_pack_bytes += inp.next_sector()
	prev_remaining_inputs = remaining_inputs
	remaining_inputs = []
	for inp in prev_remaining_inputs:
		if inp.is_finished():
			inp.end_lba_off = cur_lba_off
		else:
			remaining_inputs.append(inp)

with open(sys.argv[1], "wb") as out_info:
	for inp in all_inputs:
		assert inp.start_lba_off != -1 and inp.end_lba_off != -1
		out_info.write(struct.pack("<II", inp.start_lba_off, inp.end_lba_off))

with open(sys.argv[2], "wb") as out_pack:
	out_pack.write(out_pack_bytes)
