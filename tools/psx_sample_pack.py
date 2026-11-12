import sys
import os
import json
import struct

if len(sys.argv) != 6:
	print(f"usage: {sys.argv[0]} <jp/us/eu/sh> <sound_bank_json_dir> <converted_sample_dir> <output_sound_table> <output_sound_data>\n")
	exit()

game_version = sys.argv[1]
bank_dir = sys.argv[2]
conv_sample_dir = sys.argv[3]
out_sound_table_path = sys.argv[4]
out_sound_data_path = sys.argv[5]

SPU_SAMPLE_START = 0x1000
# Leave room for the hardware Room reverb work area and DMA alignment.
SPU_SAMPLE_LIMIT = 0x7D900

def align_up(value: int, alignment: int) -> int:
	return (value + alignment - 1) // alignment * alignment

bank_files = os.listdir(bank_dir)
bank_files.sort()
bank_files = bank_files[0:11]

out_sample_data = bytearray()

class SoundRef:
	__slots__ = "offset_in_data", "freq"

	def __init__(self, path: str):
		global out_sample_data
		full_path = conv_sample_dir + "/" + path + ".samplebin"
		with open(full_path, "rb") as samplebin_handle:
			samplebin_bytes = samplebin_handle.read()
		self.freq = struct.unpack_from("<H", samplebin_bytes)[0]
		if samplebin_bytes[2]:
			self.freq |= 0x8000  # Looping samples set ENDX on every loop.
		self.offset_in_data = len(out_sample_data)
		out_sample_data += samplebin_bytes[4:]
		assert len(out_sample_data) % 8 == 0 # SPU memory is referenced in units of 8 bytes, so it must be aligned to that

loaded_sounds: dict[str, SoundRef] = {}

class BankDef:
	__slots__ = "sound_refs"

	sound_refs: list[SoundRef | None]

	def __init__(self):
		self.sound_refs = []

	def add_null(self):
		self.sound_refs.append(None)

	def add(self, sound_path: str):
		loaded = loaded_sounds.get(sound_path)
		if loaded is None:
			loaded = SoundRef(sound_path)
			loaded_sounds[sound_path] = loaded
		self.sound_refs.append(loaded)

	def build_table(self, out: bytearray):
		for ref in self.sound_refs:
			if ref is None:
				out += b"\0\0\0\0"
			else:
				out += struct.pack("<HH", ref.freq, (SPU_SAMPLE_START + ref.offset_in_data) // 8)

all_banks: list[BankDef] = []

def process_json_ifdef(value):
	if type(value) is dict and (ifdef := value.get("ifdef")) is not None:
		if len(ifdef) == 1 and ifdef[0] == "VERSION_SH":
			return value["then"] if game_version == "sh" else value["else"]
		else:
			raise ValueError(f"unhandled ifdef condition '{ifdef}' in {bank_file_name}")
	return value

for bank_file_name in bank_files:
	with open(bank_dir + "/" + bank_file_name, "r") as bank_handle:
		bank_json = json.load(bank_handle)
		cur_bank = BankDef()
		sound_path_prefix = process_json_ifdef(bank_json["sample_bank"]) + "/"
		for instrument_name in bank_json["instrument_list"]:
			if instrument_name is None:
				cur_bank.add_null()
			else:
				sound_name = process_json_ifdef(bank_json["instruments"][instrument_name]["sound"])
				if type(sound_name) is dict:
					if (sample := sound_name.get("sample")) is not None:
						sound_name = sample # TODO: handle the 'tuning' parameter
					else:
						raise ValueError(f"unhandled value '{sound_name}' in {bank_file_name}")
				cur_bank.add(sound_path_prefix + sound_name)
		all_banks.append(cur_bank)

out_table = bytearray()
if len(all_banks) != 11:
	raise ValueError("expected 11 sound-player banks")
# The US red-coin effects use N64 synthetic instrument 128 (a sawtooth).
# Pack one 28-sample period as a looping SPU block. The previously unused
# alignment halfword after the 11 bank offsets points to it.
saw_addr = (SPU_SAMPLE_START + len(out_sample_data)) // 8
saw = [(i if i < 14 else i - 28) * 8 // 14 for i in range(28)]
out_sample_data.extend((0, 7))
out_sample_data.extend((saw[i] & 15) | ((saw[i + 1] & 15) << 4) for i in range(0, 28, 2))
sample_end = SPU_SAMPLE_START + align_up(len(out_sample_data), 256)
if sample_end > SPU_SAMPLE_LIMIT:
	raise ValueError(f"SFX samples require {sample_end - SPU_SAMPLE_START} SPU bytes, "
	                 f"only {SPU_SAMPLE_LIMIT - SPU_SAMPLE_START} are available")

first_sound_def_off = (len(all_banks) + 1) * 2
for b in all_banks:
	out_table += struct.pack("<H", first_sound_def_off // 4)
	first_sound_def_off += len(b.sound_refs) * 4
out_table += struct.pack("<H", saw_addr)
for b in all_banks:
	while len(out_table) % 4 != 0:
		out_table += b"\0"
	b.build_table(out_table)

with open(out_sound_table_path, "wb") as table_handle:
	table_handle.write(out_table)

with open(out_sound_data_path, "wb") as data_handle:
	data_handle.write(out_sample_data)
