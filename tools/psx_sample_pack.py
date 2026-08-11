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
		self.freq = struct.unpack("<H", samplebin_bytes[0:2])[0]
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
				out += struct.pack("<HH", ref.freq, (4096 + ref.offset_in_data) // 8)

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
first_sound_def_off = align_up(len(all_banks) * 2, 4)
for b in all_banks:
	out_table += struct.pack("<H", first_sound_def_off // 4)
	first_sound_def_off += len(b.sound_refs) * 4
for b in all_banks:
	while len(out_table) % 4 != 0:
		out_table += b"\0"
	b.build_table(out_table)

with open(out_sound_table_path, "wb") as table_handle:
	table_handle.write(out_table)

with open(out_sound_data_path, "wb") as data_handle:
	data_handle.write(out_sample_data)
