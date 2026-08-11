#!/usr/bin/env python3
import re
import os
import traceback
import sys

compress = True
POS_STEP = 2
ROT_STEP = 2

num_headers = 0
items = []
len_mapping = {}
order_mapping = {}
line_number_mapping = {}

def raise_error(filename, lineindex, msg):
    raise SyntaxError("Error in " + filename + ":" + str(line_number_mapping[lineindex] + 1) + ": " + msg)

ANIM_FLAG_HOR_TRANS = (1 << 3)
ANIM_FLAG_VERT_TRANS = (1 << 4)
ANIM_FLAG_NO_TRANS = (1 << 6)
ANIM_FLAG_COMPRESSED = (1 << 7)

flags_mapping = {}
values_to_indices_name_mapping = {}

def parse_struct(filename, lines, lineindex, name):
    global items, order_mapping
    lineindex += 1
    if lineindex + 9 >= len(lines):
        raise_error(filename, lineindex, "struct Animation must be 11 lines")
    v1 = int(lines[lineindex + 0].rstrip(","), 0)
    if compress:
        v1 |= ANIM_FLAG_COMPRESSED
    v2 = int(lines[lineindex + 1].rstrip(","), 0)
    v3 = int(lines[lineindex + 2].rstrip(","), 0)
    v4 = int(lines[lineindex + 3].rstrip(","), 0)
    v5 = int(lines[lineindex + 4].rstrip(","), 0)
    values = lines[lineindex + 6].rstrip(",")
    indices = lines[lineindex + 7].rstrip(",")
    items.append(("header", name, (v1, v2, v3, v4, v5, values, indices)))
    if lines[lineindex + 9] != "};":
        raise_error(filename, lineindex + 9, "Expected \"};\" but got " + lines[lineindex + 9])
    order_mapping[name] = len(items)
    flags_mapping[name] = v1
    flags_mapping[values] = v1
    flags_mapping[indices] = v1
    assert values not in values_to_indices_name_mapping or values_to_indices_name_mapping[values] == indices
    values_to_indices_name_mapping[values] = indices
    lineindex += 10
    return lineindex

arrays_by_name = {}

def parse_array(filename, lines, lineindex, name, is_indices):
    global items, len_mapping, order_mapping
    lineindex += 1
    values = []
    while lineindex < len(lines) and lines[lineindex] != "};":
        line = lines[lineindex].rstrip(",")
        if line:
            values.extend(line.split(","))
        lineindex += 1
    if lineindex >= len(lines):
        raise_error(filename, lineindex, "Expected \"};\" but reached end of file")

    items.append(("array", name, is_indices))
    len_mapping[name] = len(values)
    order_mapping[name] = len(items)
    arrays_by_name[name] = values
    lineindex += 1
    return lineindex

def parse_file(filename, lines):
    global num_headers
    lineindex = 0
    while lineindex < len(lines):
        line = lines[lineindex]
        for prefix in ["static ", "const "]:
            if line.startswith(prefix):
                line = line[len(prefix):]
        lines[lineindex] = line

        is_struct = line.startswith("struct Animation ") and line.endswith("[] = {")
        is_indices = line.startswith("u16 ") and line.endswith("[] = {")
        is_values = line.startswith("s16 ") and line.endswith("[] = {")
        if not is_struct and not is_indices and not is_values:
            raise_error(filename, lineindex, "\"" + line + "\" does not follow the pattern \"static const struct Animation anim_x[] = {\", \"static const u16 anim_x_indices[] = {\" or \"static const s16 anim_x_values[] = {\"")

        if is_struct:
            name = lines[lineindex][len("struct Animation "):-6]
            lineindex = parse_struct(filename, lines, lineindex, name)
            num_headers += 1
        else:
            name = lines[lineindex][len("s16 "):-6]
            lineindex = parse_array(filename, lines, lineindex, name, is_indices)

# this is rarely needed, but why not
def reduce_count(values_arr: list[str], start: int, count: int, step: int) -> int:
    last_idx = start + count - 1
    last_value = values_arr[last_idx]
    old_count = count
    for i in range(start + count // step * step, start, -step):
        if i < last_idx and values_arr[i] == last_value:
            count -= 1
    return count

already_compressed = {}
def compress_pair(values_name: str, indices_name: str, flags: int):
    if values_name in already_compressed or indices_name in already_compressed:
        return
    assert values_name not in already_compressed and indices_name not in already_compressed
    already_compressed[values_name] = True
    already_compressed[indices_name] = True
    values_arr = arrays_by_name[values_name]
    indices_arr = arrays_by_name[indices_name]
    if (flags & ANIM_FLAG_HOR_TRANS) != 0:
        has_xzpos = False
        has_ypos = True
    elif (flags & ANIM_FLAG_VERT_TRANS) != 0:
        has_xzpos = True
        has_ypos = False
    elif (flags & ANIM_FLAG_NO_TRANS) != 0:
        has_xzpos = False
        has_ypos = False
    else:
        has_xzpos = True
        has_ypos = True
    new_values = []
    new_indices = []
    attr_idx = 0
    for i in range(0, len(indices_arr), 2):
        count_str = indices_arr[i]
        start_str = indices_arr[i + 1]
        count = int(count_str, 0)
        start = int(start_str, 0)
        assert count != 0
        if count == 1:
            new_indices.append("0")
            new_indices.append(values_arr[start])
        elif (has_xzpos or has_ypos) and attr_idx < 3:
            # do not compress translation attributes, just take the opportunity to remove the indices for unused ones because why not
            count = reduce_count(values_arr, start, count, POS_STEP)
            if (has_xzpos and has_ypos) or has_ypos == (attr_idx == 1):
                new_indices.append(str((count + POS_STEP - 1) // POS_STEP))
                if len(new_values) % 2 != 0: # ensure the value array is aligned to 2
                    new_values.append("0")
                new_indices.append(str(len(new_values) // 2))
                for j in range(start, start + count, POS_STEP):
                    v = int(values_arr[j], 0)
                    new_values.append(str(v & 0xFF))
                    new_values.append(str(v >> 8 & 0xFF))
                if count % POS_STEP != 0:
                    v = int(values_arr[start + count - 1], 0)
                    new_values.append(str(v & 0xFF))
                    new_values.append(str(v >> 8 & 0xFF))
        else:
            count = reduce_count(values_arr, start, count, ROT_STEP)
            new_indices.append(str((count + ROT_STEP - 1) // ROT_STEP))
            new_indices.append(str(len(new_values)))
            last_val = -1
            last_val_count = 0
            for j in range(start, start + count, ROT_STEP):
                new_values.append(str(int(values_arr[j], 0) >> 8))
            if count % ROT_STEP != 0:
                new_values.append(str(int(values_arr[start + count - 1], 0) >> 8))
        attr_idx += 1
    arrays_by_name[values_name] = new_values
    arrays_by_name[indices_name] = new_indices

try:
    files = os.listdir("assets/anims")
    files.sort()

    for filename in files:
        if filename.endswith(".inc.c"):
            lines = []
            with open("assets/anims/" + filename) as f:
                for i, line in enumerate(f):
                    line = re.sub(r"/\*.*?\*/", "", line)
                    if "/*" in line:
                        line_number_mapping[-1] = i
                        raise_error(filename, -1, "Multiline comments are not supported")
                    line = line.split("//", 1)[0].strip()
                    if line:
                        line_number_mapping[len(lines)] = i
                        lines.append(line)
            if lines:
                parse_file(filename, lines)

    structdef = ["u32 numEntries;", "const struct Animation *addrPlaceholder;", "struct OffsetSizePair entries[" + str(num_headers) + "];"]
    structobj = [str(num_headers) + ",", "NULL,","{"]

    for item in items:
        type, name, obj = item
        if type == "header":
            v1, v2, v3, v4, v5, values, indices = obj
            if order_mapping[indices] < order_mapping[name]:
                raise SyntaxError("Error: Animation struct must be written before indices array for " + name)
            if order_mapping[values] < order_mapping[indices]:
                raise SyntaxError("Error: values array must be written after indices array for " + name)
            values_num_values = len_mapping[values]
            offset_to_struct = "offsetof(struct MarioAnimsObj, " + name + ")"
            offset_to_end = "offsetof(struct MarioAnimsObj, " + values + ") + sizeof(gMarioAnims." + values + ")"
            structobj.append("{" + offset_to_struct + ", " + offset_to_end + " - " + offset_to_struct + "},")
            if compress:
                compress_pair(values, indices, v1)
    structobj.append("},")

    for item in items:
        type, name, obj = item
        if type == "header":
            v1, v2, v3, v4, v5, values, indices = obj
            indices_len = len_mapping[indices] // 6 - 1
            values_num_values = len_mapping[values]
            offset_to_struct = "offsetof(struct MarioAnimsObj, " + name + ")"
            offset_to_end = "offsetof(struct MarioAnimsObj, " + values + ") + sizeof(gMarioAnims." + values + ")"
            structdef.append("struct Animation " + name + ";")
            structobj.append("{" + ", ".join([
                str(v1),
                str(v2),
                str(v3),
                str(v4),
                str(v5),
                str(indices_len),
                "(const s16 *)(offsetof(struct MarioAnimsObj, " + values + ") - " + offset_to_struct + ")",
                "(const u16 *)(offsetof(struct MarioAnimsObj, " + indices + ") - " + offset_to_struct + ")",
                offset_to_end + " - " + offset_to_struct
            ]) + "},")
        else:
            is_indices = obj
            arr = arrays_by_name[name]
            type = "u16" if is_indices else ("s8" if compress else "s16")
            structdef.append(f"{type} {name}[{len(arr)}];")
            structobj.append("{" + ",".join(arr) + "},")

    print("#include \"game/memory.h\"")
    print("#include <stddef.h>")
    print("")

    print("struct MarioAnimsObj {")
    for s in structdef:
        print(s)
    print("} gMarioAnims = {")
    for s in structobj:
        print(s)
    print("};")

except Exception as e:
    note = "NOTE! The mario animation C files are not processed by a normal C compiler, but by the script in tools/mario_anims_converter.py. The format is much more strict than normal C, so please follow the syntax of existing files.\n"
    if e is SyntaxError:
        e.msg = note + e.msg
    else:
        print(note, file=sys.stderr)
    traceback.print_exc()
    sys.exit(1)
