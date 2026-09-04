#!/usr/bin/env python3
from pathlib import Path
import shutil
import sys

path = Path(sys.argv[1] if len(sys.argv) > 1 else 'Makefile.psx.mk')
text = path.read_text(encoding='utf-8')
lines = text.splitlines(True)

backup = path.with_name(path.name + '.pre-audio-v4-extdat-backup')
if not backup.exists():
    shutil.copy2(path, backup)

# 1) Variable.
if not any(l.startswith('SFX_DATA_BIN :=') for l in lines):
    insert_at = None
    for i, line in enumerate(lines):
        if line.lstrip().startswith('GENERATED_C_FILES'):
            insert_at = i + 1
            # skip continuation lines
            while insert_at < len(lines) and lines[insert_at - 1].rstrip().endswith('\\'):
                insert_at += 1
            break
    if insert_at is None:
        raise RuntimeError('GENERATED_C_FILES not found')
    lines.insert(insert_at, 'SFX_DATA_BIN := $(BUILD_DIR)/sfx_data.generated.bin\n')

# 2) Add binary target after generated-C target if absent.
joined = ''.join(lines)
if '$(SFX_DATA_BIN): $(BUILD_DIR)/sfx_defs.generated.c' not in joined:
    target_idx = None
    for i, line in enumerate(lines):
        if line.startswith('$(BUILD_DIR)/sfx_defs.generated.c:'):
            target_idx = i
            break
    if target_idx is None:
        raise RuntimeError('sfx_defs.generated.c rule not found')

    end = target_idx + 1
    while end < len(lines) and (lines[end].startswith('>') or lines[end].startswith('\t')):
        end += 1

    rule = [
        '\n',
        '$(SFX_DATA_BIN): $(BUILD_DIR)/sfx_defs.generated.c sound/sequences/00_sound_player.s $(TOOLS_DIR)/sound_player_to_c.py\n',
        '>\t$(V)if [ ! -f $@ ]; then $(PYTHON) $(TOOLS_DIR)/sound_player_to_c.py $(VERSION) sound/sequences/00_sound_player.s $(BUILD_DIR)/sfx_defs.generated.c; fi\n',
    ]
    lines[end:end] = rule

# 3) Locate EXT.DAT no-audio rule.
target_idx = None
for i, line in enumerate(lines):
    if line.startswith('$(BUILD_DIR)/ext_files_sections_noaudio.txt:'):
        target_idx = i
        break
if target_idx is None:
    raise RuntimeError('ext_files_sections_noaudio rule not found')

# Add dependency.
if '$(SFX_DATA_BIN)' not in lines[target_idx]:
    lines[target_idx] = lines[target_idx].rstrip('\n') + ' $(SFX_DATA_BIN)\n'

# Find recipe end and remove any prior SFX segment echo to stay idempotent.
end = target_idx + 1
while end < len(lines) and (lines[end].startswith('>') or lines[end].startswith('\t')):
    end += 1
recipe = [l for l in lines[target_idx + 1:end] if '_sfx_data_segment' not in l]

# Insert before mv.
mv_idx = None
for i, line in enumerate(recipe):
    if '@mv $@.tmp $@' in line:
        mv_idx = i
        break
if mv_idx is None:
    raise RuntimeError('ext_files_sections_noaudio mv line not found')

recipe.insert(
    mv_idx,
    '>\t@echo " $(SFX_DATA_BIN):0:$$(printf "%x" $$(wc -c <"$(SFX_DATA_BIN)"))!_sfx_data_segment:_sfx_data_segment_end " >> $@.tmp\n'
)
lines[target_idx + 1:end] = recipe

final = ''.join(lines)
for token in (
    'SFX_DATA_BIN := $(BUILD_DIR)/sfx_data.generated.bin',
    '$(SFX_DATA_BIN): $(BUILD_DIR)/sfx_defs.generated.c',
    '_sfx_data_segment:_sfx_data_segment_end',
):
    if token not in final:
        raise RuntimeError(f'post-patch check failed: {token}')

path.write_text(final, encoding='utf-8')
print(f'Patched {path} for Audio V4 EXT.DAT data.')
print(f'Backup: {backup}')
