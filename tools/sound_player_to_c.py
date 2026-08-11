import sys
import os
import enum

if len(sys.argv) < 4:
	print(f"usage: {sys.argv[0]} <jp/us/eu/sh> <00_sound_player.s> <output.c>\n")
	exit()

game_version = sys.argv[1]
in_path = sys.argv[2]
out_path = sys.argv[3]

def split_instruction(line: str):
	line = line.strip()
	if (space := line.find(" ")) != -1:
		return [line[:space], *[x.strip() for x in line[space:].split(",")]]
	else:
		return [line]

with open(in_path, "r") as in_handle:
	asm_lines = in_handle.readlines()

out_c_defines = []

cur_definition_name: str | None = None
cur_definition_lines: list[str] | None = None
definitions: dict[str, list[str]] = {}
definition_first_lines: dict[str, int] = {}
skipping_until_else_or_endif = False

for line_idx, line in enumerate(asm_lines):
	if line == "#include \"seq_macros.inc\"\n":
		continue
	line = line.strip()
	if len(line) == 0 or line.startswith("//"):
		continue
	if (comment_idx := line.find("//")) != -1:
		line = line[0:comment_idx].rstrip()
	if skipping_until_else_or_endif:
		if line == "#else" or line == "#endif":
			skipping_until_else_or_endif = False
		continue
	if line.startswith("#"):
		if line == "#endif":
			pass
		elif line == "#else":
			skipping_until_else_or_endif = True
		elif line == "#ifdef VERSION_JP":
			skipping_until_else_or_endif = game_version != "jp"
		elif line == "#ifndef VERSION_JP":
			skipping_until_else_or_endif = game_version == "jp"
		elif line == "#ifdef VERSION_SH":
			skipping_until_else_or_endif = game_version != "sh"
		elif line == "#if defined(VERSION_EU) || defined(VERSION_SH)":
			skipping_until_else_or_endif = game_version != "eu" and game_version != "sh"
		else:
			raise ValueError(f"unhandled directive in line '{line}'")
		continue
	elif line.startswith(".set "):
		parts = split_instruction(line)
		out_c_defines.append(f"#define {parts[1]} {parts[2]}\n")
	if line.endswith(":"):
		if cur_definition_name is not None:
			definitions[cur_definition_name] = cur_definition_lines
		cur_definition_name = line[0:-1]
		cur_definition_lines = []
		definition_first_lines[cur_definition_name] = line_idx + 1
	elif cur_definition_name is not None:
		cur_definition_lines.append(line)

out_sfx_defs = []

def get_first_note_in_layer(which: str) -> tuple[str, str, str, str]:
	for line_idx in range(definition_first_lines[which], len(asm_lines)):
		parts = split_instruction(asm_lines[line_idx])
		match parts[0]:
			case "layer_end":
				break
			case "layer_note0":
				return parts[1], parts[2], parts[3], parts[4]
			case "layer_note1":
				return parts[1], parts[2], parts[3], "0"
	return "39", "0", "0", "0"

def make_sfx_def(which: str):
	bank: str | None = None
	instrument: str | None = None
	note: str | None = None
	delay: str | None = None
	velocity: str | None = None
	gate: str | None = None
	for line in definitions[which]:
		parts = line.split(" ")
		match parts[0]:
			case "chan_setbank":
				bank = parts[1]
			case "chan_setinstr":
				instrument = parts[1]
			case "chan_setlayer":
				note, delay, velocity, gate = get_first_note_in_layer(parts[2])
				break
	if bank is None:
		bank = "0" # TODO
	if instrument is None:
		instrument = "0" # TODO
	assert bank is not None and instrument is not None
	if note is None:
		note = "39"
		delay = "0"
		velocity = "127"
		gate = "0"
	out_sfx_defs.append(f"\t(SfxDef) {{.bank_id={bank},.sample_id={instrument},.note={note},.gate={gate},.velocity={velocity}}},\n")
	#print(which, bank, instrument)
	return bank, instrument

def make_channel_table_def(which: str):
	out_sfx_defs.append("(SfxDef[]) {\n")
	for line in definitions[which]:
		parts = line.split(" ")
		if parts[0] == "sound_ref":
			make_sfx_def(parts[1])
	out_sfx_defs.append("},\n")

for line in definitions["sequence_start"]:
	parts = line.split(" ")
	match parts[0]:
		case "seq_startchannel":
			make_channel_table_def(parts[2] + "_table")

#print(definitions)

with open(out_path, "w") as out_handle:
	out_handle.write("#include <port/audio_data.h>\n")
	out_handle.writelines(out_c_defines)
	out_handle.write("SfxDef* sfx_defs_per_channel[] = {\n")
	out_handle.writelines(out_sfx_defs)
	out_handle.write("};")
