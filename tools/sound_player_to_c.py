#!/usr/bin/env python3
"""
SM64 sound-player -> compact PS1 sequencer data (V4, EXT.DAT-backed).

This is intentionally a static interpreter for sound/sequences/00_sound_player.s.
It follows the channel/layer control flow used by the sound-player sequence and
bakes channel state into note events that the PS1 SPU backend can execute cheaply.
"""
from __future__ import annotations

import bisect
import copy
import re
import struct
import sys
from pathlib import Path
from collections import Counter, defaultdict
from dataclasses import dataclass, field

if len(sys.argv) < 4:
    print(f"usage: {sys.argv[0]} <jp/us/eu/sh> <00_sound_player.s> <output.c>")
    sys.exit(1)

game_version = sys.argv[1]
in_path = sys.argv[2]
out_path = sys.argv[3]

VALID_VERSIONS = {"jp", "us", "eu", "sh"}
if game_version not in VALID_VERSIONS:
    raise ValueError(f"invalid game version: {game_version}")

VERSION_SYMBOLS = {
    "VERSION_JP": game_version == "jp",
    "VERSION_US": game_version == "us",
    "VERSION_EU": game_version == "eu",
    "VERSION_SH": game_version == "sh",
    # Newer upstream sources mention CN; the PS1 tree currently has no CN build.
    "VERSION_CN": False,
}


def split_instruction(line: str) -> list[str]:
    line = line.strip()
    if not line:
        return []
    m = re.match(r"^(\S+)(?:\s+(.*))?$", line)
    if not m:
        return [line]
    if m.group(2) is None:
        return [m.group(1)]
    return [m.group(1), *[x.strip() for x in m.group(2).split(",")]]


def strip_comment(line: str) -> str:
    if "//" in line:
        line = line.split("//", 1)[0]
    return line.strip()


def eval_condition(line: str) -> bool:
    expr = re.sub(r"^#(?:if|elif)\s+", "", line).strip()
    for symbol, enabled in VERSION_SYMBOLS.items():
        expr = re.sub(
            rf"defined\s*\(\s*{re.escape(symbol)}\s*\)",
            str(enabled),
            expr,
        )
        expr = re.sub(rf"\b{re.escape(symbol)}\b", str(enabled), expr)
    expr = expr.replace("||", " or ").replace("&&", " and ")
    expr = re.sub(r"!\s*(?!=)", " not ", expr)
    if not re.fullmatch(r"[\sTrueFalsorandnot()01]+", expr):
        raise ValueError(f"unsupported preprocessor condition: {line}")
    return bool(eval(expr, {"__builtins__": {}}, {}))


with open(in_path, "r", encoding="utf-8") as handle:
    raw_lines = handle.readlines()

# ---------------------------------------------------------------------------
# Minimal preprocessing for the VERSION_* conditionals used by the sequence.
# ---------------------------------------------------------------------------
active_lines: list[str] = []
defines: dict[str, int] = {}
out_c_defines: list[str] = []
# Stack entries: parent_active, branch_taken, this_branch_active
cond_stack: list[list[bool]] = []
current_active = True

for raw in raw_lines:
    if raw.strip() == '#include "seq_macros.inc"':
        continue

    line = strip_comment(raw)
    if not line:
        continue

    if line.startswith("#ifdef ") or line.startswith("#ifndef "):
        symbol = line.split(None, 1)[1].strip()
        if symbol not in VERSION_SYMBOLS:
            raise ValueError(f"unsupported preprocessor symbol: {symbol}")
        cond = VERSION_SYMBOLS[symbol]
        if line.startswith("#ifndef"):
            cond = not cond
        parent = current_active
        branch_active = parent and cond
        cond_stack.append([parent, bool(cond), branch_active])
        current_active = branch_active
        continue

    if line.startswith("#if "):
        cond = eval_condition(line)
        parent = current_active
        branch_active = parent and cond
        cond_stack.append([parent, bool(cond), branch_active])
        current_active = branch_active
        continue

    if line.startswith("#elif "):
        if not cond_stack:
            raise ValueError("#elif without #if")
        parent, branch_taken, _ = cond_stack[-1]
        cond = False if branch_taken else eval_condition(line)
        cond_stack[-1] = [parent, branch_taken or bool(cond), parent and cond]
        current_active = parent and cond
        continue

    if line == "#else":
        if not cond_stack:
            raise ValueError("#else without #if")
        parent, branch_taken, _ = cond_stack[-1]
        cond = not branch_taken
        cond_stack[-1] = [parent, True, parent and cond]
        current_active = parent and cond
        continue

    if line == "#endif":
        if not cond_stack:
            raise ValueError("#endif without #if")
        parent, _, _ = cond_stack.pop()
        current_active = parent
        continue

    if not current_active:
        continue

    if line.startswith(".set "):
        m = re.match(r"\.set\s+([A-Za-z_][A-Za-z0-9_]*)\s*,?\s*(.+)$", line)
        if m:
            name, value = m.group(1), m.group(2).strip()
            try:
                defines[name] = int(value, 0)
            except ValueError:
                pass
            out_c_defines.append(f"#define {name} {value}\n")
        continue

    # Assembler directives do not participate in the script instruction stream.
    if line.startswith(".section") or line.startswith(".align") or line.startswith(".balign"):
        continue

    active_lines.append(line)

if cond_stack:
    raise ValueError("unterminated preprocessor conditional")

# ---------------------------------------------------------------------------
# Flat instruction stream. Labels are source-level PCs.
# ---------------------------------------------------------------------------
stream: list[str] = []
label_to_pc: dict[str, int] = {}
labels_at_pc: dict[int, list[str]] = defaultdict(list)

for line in active_lines:
    if line.endswith(":"):
        label = line[:-1]
        label_to_pc[label] = len(stream)
        labels_at_pc[len(stream)].append(label)
    else:
        stream.append(line)

sorted_label_pcs = sorted(set(label_to_pc.values()))


def block_for(label: str) -> list[str]:
    if label not in label_to_pc:
        raise KeyError(f"unknown label: {label}")
    start = label_to_pc[label]
    end = len(stream)
    for pc in sorted_label_pcs:
        if pc > start:
            end = pc
            break
    return stream[start:end]


def parse_number(token: str, default: int = 0) -> int:
    token = token.strip()
    if token in defines:
        return defines[token]
    try:
        return int(token, 0)
    except ValueError:
        expr = token
        for name, value in defines.items():
            expr = re.sub(rf"\b{re.escape(name)}\b", str(value), expr)
        if re.fullmatch(r"[0-9a-fA-FxX\s+\-*/%()|&~<>]+", expr):
            try:
                return int(eval(expr, {"__builtins__": {}}, {}))
            except Exception:
                pass
        return default


def clamp(value: int, lo: int, hi: int) -> int:
    return max(lo, min(hi, value))


warnings: list[str] = []
unsupported_layer_ops = Counter()
unsupported_channel_ops = Counter()
recognized_unrendered = Counter()


@dataclass
class ChannelState:
    bank: int = 0
    instrument: int = 0
    value: int = 0
    io: list[int] = field(default_factory=lambda: [-1] * 8)
    pan: int = 64
    panmix: int = 127
    volume: int = 127
    volume_scale: int = 127
    release_rate: int = 0x20
    vibrato_rate: int = 64  # arg form; 64 * 32 == default internal rate 0x800
    vibrato_extent: int = 0
    vibrato_delay_units: int = 0
    reverb: int = 0
    transpose: int = 0
    envelope: str | None = None

    def clone(self) -> "ChannelState":
        return copy.deepcopy(self)


@dataclass
class LayerStart:
    index: int
    label: str
    tick: int


@dataclass
class ChannelCompileResult:
    timeline: list[tuple[int, int, ChannelState]]
    starts: list[LayerStart]
    final_state: ChannelState
    end_tick: int


def target_pc(label: str, owner: str) -> int | None:
    if label not in label_to_pc:
        warnings.append(f"{owner}: target not found: {label}")
        return None
    return label_to_pc[label]


def compile_channel(label: str) -> ChannelCompileResult:
    """Interpret one sound's channel script with static IO defaults."""
    if label not in label_to_pc:
        warnings.append(f"missing sound definition {label}")
        st = ChannelState()
        return ChannelCompileResult([(0, 0, st.clone())], [], st, 0)

    state = ChannelState()
    tick = 0
    serial = 0
    timeline: list[tuple[int, int, ChannelState]] = [(0, serial, state.clone())]
    starts: list[LayerStart] = []
    stack: list[dict] = []
    overrides: dict[int, int] = {}
    backward_jumps: Counter[tuple] = Counter()
    pc = label_to_pc[label]
    steps = 0

    def mark() -> None:
        nonlocal serial
        serial += 1
        timeline.append((tick, serial, state.clone()))

    while 0 <= pc < len(stream) and steps < 8192 and tick <= 0xFFFF:
        instr_pc = pc
        parts = split_instruction(stream[pc])
        pc += 1
        steps += 1
        if not parts:
            continue

        if instr_pc in overrides and len(parts) >= 2:
            parts[1] = str(overrides[instr_pc])

        op = parts[0]

        if op == "chan_end":
            if stack:
                frame = stack.pop()
                pc = frame["return_pc"]
                continue
            break

        if op == "chan_call" and len(parts) >= 2:
            dst = target_pc(parts[1], label)
            if dst is None:
                break
            stack.append({"kind": "call", "return_pc": pc})
            pc = dst
            continue

        if op == "chan_jump" and len(parts) >= 2:
            dst = target_pc(parts[1], label)
            if dst is None:
                break
            if dst <= instr_pc:
                # A source-level backward jump is the sound player's common
                # way to express an intentional forever loop. Follow the first
                # pass, then stop on the second execution of the same jump even
                # when script values are being modulated inside the loop.
                key = (instr_pc, dst, len(stack))
                backward_jumps[key] += 1
                if backward_jumps[key] > 1:
                    # Keep one representative pass of an intentional forever loop.
                    break
            pc = dst
            continue

        if op == "chan_loop" and len(parts) >= 2:
            count = parse_number(parts[1]) & 0xFF
            if count == 0:
                count = 256
            stack.append({"kind": "loop", "return_pc": pc, "loop_pc": pc, "remaining": count})
            continue

        if op == "chan_loopend":
            if stack and stack[-1]["kind"] == "loop":
                frame = stack[-1]
                frame["remaining"] -= 1
                if frame["remaining"] > 0:
                    pc = frame["loop_pc"]
                else:
                    stack.pop()
            else:
                warnings.append(f"{label}: chan_loopend without loop")
            continue

        if op == "chan_break":
            if stack:
                stack.pop()
            continue

        if op in ("chan_beqz", "chan_bltz", "chan_bgez") and len(parts) >= 2:
            take = ((op == "chan_beqz" and state.value == 0) or
                    (op == "chan_bltz" and state.value < 0) or
                    (op == "chan_bgez" and state.value >= 0))
            if take:
                dst = target_pc(parts[1], label)
                if dst is None:
                    break
                pc = dst
            continue

        if op == "chan_delay1":
            tick += 1
            continue

        if op == "chan_delay" and len(parts) >= 2:
            tick += max(0, parse_number(parts[1]))
            continue

        if op == "chan_setval" and len(parts) >= 2:
            state.value = parse_number(parts[1])
            continue

        if op == "chan_subtract" and len(parts) >= 2:
            state.value -= parse_number(parts[1])
            continue

        if op == "chan_bitand" and len(parts) >= 2:
            state.value &= parse_number(parts[1])
            continue

        if op == "chan_ioreadval" and len(parts) >= 2:
            slot = clamp(parse_number(parts[1]), 0, 7)
            state.value = state.io[slot]
            if slot <= 3:
                state.io[slot] = -1
            continue

        if op == "chan_iowriteval" and len(parts) >= 2:
            slot = clamp(parse_number(parts[1]), 0, 7)
            state.io[slot] = state.value
            continue

        if op == "chan_writeseq_nextinstr" and len(parts) >= 3:
            add = parse_number(parts[1])
            offset = parse_number(parts[2])
            if offset == 1:
                # In this sequence the macro is deliberately used to patch the
                # immediate byte of the next command (.delay / .set_reverb).
                overrides[pc] = (state.value + add) & 0xFF
            else:
                recognized_unrendered[op] += 1
            continue

        if op == "chan_setlayer" and len(parts) >= 3:
            idx = clamp(parse_number(parts[1]), 0, 3)
            starts.append(LayerStart(idx, parts[2], tick))
            continue

        if op == "chan_freelayer":
            # Static event data cannot literally destroy an already compiled
            # layer. The common force-stop paths are not selected with IO=-1.
            recognized_unrendered[op] += 1
            continue

        state_changed = True
        if op == "chan_setbank" and len(parts) >= 2:
            state.bank = clamp(parse_number(parts[1]), 0, 255)
        elif op == "chan_setinstr" and len(parts) >= 2:
            state.instrument = clamp(parse_number(parts[1]), 0, 255)
        elif op == "chan_setpan" and len(parts) >= 2:
            state.pan = clamp(parse_number(parts[1]), 0, 127)
        elif op == "chan_setpanmix" and len(parts) >= 2:
            state.panmix = clamp(parse_number(parts[1]), 0, 127)
        elif op == "chan_setvol" and len(parts) >= 2:
            state.volume = clamp(parse_number(parts[1]), 0, 127)
        elif op == "chan_setvolscale" and len(parts) >= 2:
            state.volume_scale = clamp(parse_number(parts[1]), 0, 127)
        elif op == "chan_setdecayrelease" and len(parts) >= 2:
            state.release_rate = clamp(parse_number(parts[1]), 0, 255)
        elif op == "chan_setvibratorate" and len(parts) >= 2:
            state.vibrato_rate = clamp(parse_number(parts[1]), 0, 255)
        elif op == "chan_setvibratoextent" and len(parts) >= 2:
            state.vibrato_extent = clamp(parse_number(parts[1]), 0, 255)
        elif op == "chan_setvibratodelay" and len(parts) >= 2:
            state.vibrato_delay_units = clamp(parse_number(parts[1]), 0, 255) * 16
        elif op == "chan_setvibratoratelinear" and len(parts) >= 4:
            state.vibrato_rate = clamp(parse_number(parts[2]), 0, 255)
            state.vibrato_delay_units = clamp(parse_number(parts[3]), 0, 255) * 16
        elif op == "chan_setvibratoextentlinear" and len(parts) >= 4:
            state.vibrato_extent = clamp(parse_number(parts[2]), 0, 255)
            state.vibrato_delay_units = clamp(parse_number(parts[3]), 0, 255) * 16
        elif op == "chan_setreverb" and len(parts) >= 2:
            state.reverb = clamp(parse_number(parts[1]), 0, 127)
        elif op == "chan_transpose" and len(parts) >= 2:
            state.transpose = parse_number(parts[1])
        elif op == "chan_setenvelope" and len(parts) >= 2:
            state.envelope = parts[1]
            recognized_unrendered[op] += 1
        elif op == "chan_freqscale" and len(parts) >= 2:
            # Not used by the stock sound player; retaining it would require
            # another event field. Count explicitly if a custom source uses it.
            recognized_unrendered[op] += 1
            state_changed = False
        elif op.startswith("chan_"):
            # Benign controller/priority/reverb-routing commands are allowed to
            # pass without terminating the static interpretation.
            unsupported_channel_ops[op] += 1
            state_changed = False
        else:
            state_changed = False

        if state_changed:
            mark()

    if steps >= 8192:
        warnings.append(f"{label}: channel execution limit reached")
    if tick > 0xFFFF:
        warnings.append(f"{label}: channel timeline exceeded 65535 ticks; truncated")

    return ChannelCompileResult(timeline, starts, state, min(tick, 0xFFFF))


def state_at(timeline: list[tuple[int, int, ChannelState]], tick: int) -> ChannelState:
    # Timelines are monotonic in tick; at equal ticks the latest state wins.
    best = timeline[0][2]
    for t, _, state in timeline:
        if t > tick:
            break
        best = state
    return best


def combine_pan(channel_pan: int, panmix: int, layer_pan: int) -> int:
    # N64: channel contribution = panmix/128, layer contribution = 1-weight.
    return clamp((channel_pan * panmix + layer_pan * (128 - panmix) + 64) // 128, 0, 127)


def bake_velocity(velocity: int, channel: ChannelState) -> int:
    value = clamp(velocity, 0, 127)
    value = (value * channel.volume + 63) // 127
    value = (value * channel.volume_scale + 63) // 127
    return clamp(value, 0, 127)


def compile_layer(label: str, base_tick: int, timeline: list[tuple[int, int, ChannelState]]):
    if label not in label_to_pc:
        warnings.append(f"missing layer {label}")
        return [], base_tick

    pc = label_to_pc[label]
    local_tick = 0
    transpose = 0
    local_pan = 64
    local_instrument: tuple[int, int] | None = None
    local_bank_override: int | None = None
    continuous = False
    porta_mode = 0
    porta_target_raw = 0
    porta_time = 0
    stack: list[dict] = []
    backward_jumps: Counter[tuple] = Counter()
    events: list[dict] = []
    steps = 0

    while 0 <= pc < len(stream) and steps < 16384 and len(events) < 4096:
        instr_pc = pc
        parts = split_instruction(stream[pc])
        pc += 1
        steps += 1
        if not parts:
            continue
        op = parts[0]

        if op == "layer_end":
            if stack:
                frame = stack.pop()
                pc = frame["return_pc"]
                continue
            break

        if op == "layer_call" and len(parts) >= 2:
            dst = target_pc(parts[1], label)
            if dst is None:
                break
            stack.append({"kind": "call", "return_pc": pc})
            pc = dst
            continue

        if op == "layer_jump" and len(parts) >= 2:
            dst = target_pc(parts[1], label)
            if dst is None:
                break
            if dst <= instr_pc:
                key = (instr_pc, dst, len(stack))
                backward_jumps[key] += 1
                if backward_jumps[key] > 1:
                    break
            pc = dst
            continue

        if op == "layer_loop" and len(parts) >= 2:
            count = parse_number(parts[1]) & 0xFF
            if count == 0:
                count = 256
            stack.append({"kind": "loop", "return_pc": pc, "loop_pc": pc, "remaining": count})
            continue

        if op == "layer_loopend":
            if stack and stack[-1]["kind"] == "loop":
                frame = stack[-1]
                frame["remaining"] -= 1
                if frame["remaining"] > 0:
                    pc = frame["loop_pc"]
                else:
                    stack.pop()
            else:
                warnings.append(f"{label}: layer_loopend without loop")
            continue

        if op in ("layer_delay", "layer_shortdelay") and len(parts) >= 2:
            local_tick += max(0, parse_number(parts[1]))
            if local_tick > 0xFFFF:
                break
            continue

        if op == "layer_transpose" and len(parts) >= 2:
            transpose = parse_number(parts[1])
            continue

        if op == "layer_setinstr" and len(parts) >= 2:
            ch = state_at(timeline, base_tick + local_tick)
            bank = local_bank_override if local_bank_override is not None else ch.bank
            local_instrument = (clamp(bank, 0, 255), clamp(parse_number(parts[1]), 0, 255))
            continue

        if op == "layer_setbank" and len(parts) >= 2:
            local_bank_override = clamp(parse_number(parts[1]), 0, 255)
            # A previously resolved local instrument remains resolved to its bank.
            continue

        if op == "layer_setpan" and len(parts) >= 2:
            local_pan = clamp(parse_number(parts[1]), 0, 127)
            continue

        if op == "layer_somethingon":
            continuous = True
            continue

        if op == "layer_somethingoff":
            continuous = False
            continue

        if op == "layer_portamento" and len(parts) >= 4:
            porta_mode = clamp(parse_number(parts[1]), 0, 255)
            porta_target_raw = parse_number(parts[2])
            porta_time = clamp(parse_number(parts[3]), 0, 255)
            continue

        if op == "layer_disableportamento":
            porta_mode = 0
            porta_time = 0
            continue

        if op in ("layer_note0", "layer_note1", "layer_note2"):
            abs_tick = base_tick + local_tick
            ch = state_at(timeline, abs_tick)

            if op == "layer_note0":
                if len(parts) < 5:
                    warnings.append(f"{label}: malformed layer_note0")
                    continue
                raw_note = parse_number(parts[1])
                play_ticks = max(0, parse_number(parts[2]))
                velocity = parse_number(parts[3], 127)
                duration_arg = clamp(parse_number(parts[4]), 0, 255)
                gate_ticks = (play_ticks * duration_arg) // 256
                if play_ticks > 0 and duration_arg > 0 and gate_ticks == 0:
                    gate_ticks = 1
            elif op == "layer_note1":
                if len(parts) < 4:
                    warnings.append(f"{label}: malformed layer_note1")
                    continue
                raw_note = parse_number(parts[1])
                play_ticks = max(0, parse_number(parts[2]))
                velocity = parse_number(parts[3], 127)
                # noteDuration == 0 in the large-note interpreter: sustain to
                # the next layer event / delay boundary.
                gate_ticks = play_ticks if play_ticks > 0 else 1
            else:
                # Not used by stock 00_sound_player.s, but retain sensible
                # semantics for custom sources: duration is also the advance.
                if len(parts) < 4:
                    warnings.append(f"{label}: malformed layer_note2")
                    continue
                raw_note = parse_number(parts[1])
                velocity = parse_number(parts[2], 127)
                play_ticks = max(0, parse_number(parts[3]))
                gate_ticks = play_ticks if play_ticks > 0 else 1

            total_transpose = transpose + ch.transpose
            note = clamp(raw_note + total_transpose, 0, 127)

            if local_instrument is None:
                bank = local_bank_override if local_bank_override is not None else ch.bank
                instrument = ch.instrument
            else:
                bank, instrument = local_instrument

            p_mode = porta_mode
            p_base = p_mode & 0x7F
            p_target = clamp(porta_target_raw + total_transpose, 0, 127)
            p_ticks = 0
            if p_base in (1, 2, 3, 4, 5) and porta_time > 0:
                if p_mode & 0x80:
                    p_ticks = max(1, (play_ticks * porta_time + 254) // 255)
                else:
                    p_ticks = max(1, porta_time)

            events.append({
                "start_tick": clamp(abs_tick, 0, 0xFFFF),
                "gate_ticks": clamp(gate_ticks, 0, 0xFFFF),
                "portamento_ticks": clamp(p_ticks, 0, 0xFFFF),
                "bank": clamp(bank, 0, 255),
                "instrument": clamp(instrument, 0, 255),
                "note": note,
                "velocity": bake_velocity(velocity, ch),
                "pan": combine_pan(ch.pan, ch.panmix, local_pan),
                "release_rate": clamp(ch.release_rate, 0, 255),
                "vibrato_rate": clamp(ch.vibrato_rate, 0, 255),
                "vibrato_extent": clamp(ch.vibrato_extent, 0, 255),
                "vibrato_delay_units": clamp(ch.vibrato_delay_units, 0, 0xFFFF),
                "portamento_mode": p_mode,
                "portamento_target": p_target,
                "reverb": clamp(ch.reverb, 0, 127),
                "flags": 1 if continuous else 0,
            })

            # One-shot portamento modes 1/2 are cleared after the note.
            if p_base in (1, 2):
                porta_mode = 0
                porta_time = 0
            elif p_base == 5:
                porta_target_raw = raw_note

            local_tick += play_ticks
            if local_tick > 0xFFFF:
                break
            continue

        # Short-note default setters are harmless for this sequence because all
        # stock sound-player channels use large notes, but recognize them.
        if op in (
            "layer_setshortnotevelocity",
            "layer_setshortnotedefaultplaypercentage",
            "layer_setshortnoteduration",
        ):
            recognized_unrendered[op] += 1
            continue

        if op.startswith("layer_"):
            unsupported_layer_ops[op] += 1

    if steps >= 16384:
        warnings.append(f"{label}: layer execution limit reached")
    if len(events) >= 4096:
        warnings.append(f"{label}: event limit reached")

    end_tick = clamp(base_tick + local_tick, 0, 0xFFFF)
    if events:
        end_tick = max(end_tick, max(e["start_tick"] + e["gate_ticks"] for e in events))
        end_tick = clamp(end_tick, 0, 0xFFFF)
    return events, end_tick


sound_cache: dict[str, list[dict]] = {}
sound_order: list[str] = []


def compile_sound(label: str):
    if label in sound_cache:
        return sound_cache[label]

    chan = compile_channel(label)
    grouped: dict[int, list[LayerStart]] = defaultdict(list)
    for start in chan.starts:
        grouped[start.index].append(start)

    compiled_layers: list[dict] = []
    for layer_index in sorted(grouped):
        merged_events: list[dict] = []
        end_tick = 0
        for start in grouped[layer_index]:
            evs, end = compile_layer(start.label, start.tick, chan.timeline)
            merged_events.extend(evs)
            end_tick = max(end_tick, end)
        merged_events.sort(key=lambda e: e["start_tick"])
        if merged_events:
            compiled_layers.append({"events": merged_events, "end_tick": clamp(end_tick, 0, 0xFFFF)})

    if not compiled_layers:
        st = chan.final_state
        compiled_layers = [{
            "events": [{
                "start_tick": 0,
                "gate_ticks": 8,
                "portamento_ticks": 0,
                "bank": clamp(st.bank, 0, 255),
                "instrument": clamp(st.instrument, 0, 255),
                "note": 39,
                "velocity": bake_velocity(127, st),
                "pan": combine_pan(st.pan, st.panmix, 64),
                "release_rate": clamp(st.release_rate, 0, 255),
                "vibrato_rate": clamp(st.vibrato_rate, 0, 255),
                "vibrato_extent": clamp(st.vibrato_extent, 0, 255),
                "vibrato_delay_units": clamp(st.vibrato_delay_units, 0, 0xFFFF),
                "portamento_mode": 0,
                "portamento_target": 39,
                "reverb": clamp(st.reverb, 0, 127),
                "flags": 0,
            }],
            "end_tick": 8,
        }]

    if len(compiled_layers) > 4:
        warnings.append(f"{label}: {len(compiled_layers)} compiled layers; truncating to 4")
        compiled_layers = compiled_layers[:4]

    sound_cache[label] = compiled_layers
    sound_order.append(label)
    return compiled_layers


# ---------------------------------------------------------------------------
# Discover tables exactly as the sound-player sequence wires them.
# ---------------------------------------------------------------------------
channels: list[list[str]] = []
for line in block_for("sequence_start"):
    parts = split_instruction(line)
    if parts and parts[0] == "seq_startchannel" and len(parts) >= 3:
        table_label = parts[2] + "_table"
        refs: list[str] = []
        for table_line in block_for(table_label):
            table_parts = split_instruction(table_line)
            if table_parts and table_parts[0] == "sound_ref" and len(table_parts) >= 2:
                refs.append(table_parts[1])
        channels.append(refs)

if not channels:
    raise RuntimeError("no sound-player channels found")

for refs in channels:
    for ref in refs:
        compile_sound(ref)


def ident(label: str, index: int) -> str:
    clean = re.sub(r"[^A-Za-z0-9_]", "_", label).strip("_") or "sound"
    return f"sfx_{index}_{clean}"


# ---------------------------------------------------------------------------
# V4 output: keep only a tiny channel index in the executable and put the
# large event/layer/definition tables into EXT.DAT as sfx_data.generated.bin.
# The blob is loaded once from EXT.DAT into the dynamic main pool at runtime.
# ---------------------------------------------------------------------------
SFX_BLOB_MAGIC = 0x34584653  # bytes "SFX4" on little-endian MIPS
SFX_BLOB_HEADER_SIZE = 24
SFX_DEF_SIZE = 4
SFX_LAYER_SIZE = 8
SFX_EVENT_SIZE = 20

# Flatten every unique sound exactly once. Channel definitions can reference
# the same first-layer index, preserving the sharing of the V3 C tables.
event_records: list[dict[str, int]] = []
layer_records: list[tuple[int, int, int]] = []
sound_first_layer: dict[str, int] = {}

for label in sound_order:
    layers = sound_cache[label]
    sound_first_layer[label] = len(layer_records)
    for layer in layers:
        first_event = len(event_records)
        event_records.extend(layer["events"])
        layer_records.append((first_event, len(layer["events"]), layer["end_tick"]))

# Definitions are laid out channel-by-channel so lookup is simply
# first_def[channel] + sound_id.
def_records: list[tuple[int, int]] = []
channel_first_def: list[int] = []
for refs in channels:
    channel_first_def.append(len(def_records))
    for ref in refs:
        def_records.append((sound_first_layer[ref], len(sound_cache[ref])))

# Binary layout: header -> defs -> layers -> events. All sections naturally
# start at 4-byte boundaries and SfxEvent remains the exact 20-byte runtime
# structure, so the PS1 can cast the loaded blob without unpacking notes.
defs_offset = SFX_BLOB_HEADER_SIZE
layers_offset = defs_offset + len(def_records) * SFX_DEF_SIZE
events_offset = layers_offset + len(layer_records) * SFX_LAYER_SIZE
assert events_offset % 4 == 0

blob = bytearray()
blob += struct.pack(
    "<IHHIIII",
    SFX_BLOB_MAGIC,
    len(def_records),
    len(layer_records),
    defs_offset,
    layers_offset,
    events_offset,
    len(event_records),
)

for first_layer, layer_count in def_records:
    if first_layer > 0xFFFF or layer_count > 0xFF:
        raise ValueError("SFX definition index overflow")
    blob += struct.pack("<HBB", first_layer, layer_count, 0)

for first_event, event_count, end_tick in layer_records:
    if event_count > 0xFFFF or end_tick > 0xFFFF:
        raise ValueError("SFX layer field overflow")
    blob += struct.pack("<IHH", first_event, event_count, end_tick)

for e in event_records:
    values = (
        e["start_tick"],
        e["gate_ticks"],
        e["portamento_ticks"],
        e["vibrato_delay_units"],
        e["bank"],
        e["instrument"],
        e["note"],
        e["velocity"],
        e["pan"] | (0x80 if e["flags"] else 0),
        e["release_rate"],
        e["vibrato_rate"],
        e["vibrato_extent"],
        e["portamento_mode"],
        e["portamento_target"],
        e["reverb"],
    )
    if any(v < 0 or v > 0xFFFF for v in values[:4]):
        raise ValueError(f"16-bit SFX event field overflow: {values[:4]}")
    if any(v < 0 or v > 0xFF for v in values[4:]):
        raise ValueError(f"8-bit SFX event field overflow: {values[4:]}")
    # 4*u16 + 11*u8 + 1 explicit padding byte = 20 bytes.
    blob += struct.pack("<HHHHBBBBBBBBBBBx", *values)

expected_size = events_offset + len(event_records) * SFX_EVENT_SIZE
assert len(blob) == expected_size, (len(blob), expected_size)

bin_path = Path(out_path).with_name("sfx_data.generated.bin")
bin_path.parent.mkdir(parents=True, exist_ok=True)
bin_path.write_bytes(blob)

out: list[str] = []
out.append("#include <port/audio_data.h>\n\n")
out.append("/* V4: the large sequencer tables live in EXT.DAT. */\n")
out.append("const u16 sfx_first_def_per_channel[] = {\n")
for first in channel_first_def:
    out.append(f"\t{first},\n")
out.append("};\n\n")

out.append("const u16 sfx_count_per_channel[] = {\n")
for refs in channels:
    out.append(f"\t{len(refs)},\n")
out.append("};\n\n")
out.append(f"const u8 sfx_channel_count = {len(channels)};\n")
out.append(f"const u32 sfx_data_blob_size = {len(blob)}u;\n")
out.append(f"const u16 sfx_total_def_count = {len(def_records)};\n")
out.append(f"const u16 sfx_total_layer_count = {len(layer_records)};\n")
out.append(f"const u32 sfx_total_event_count = {len(event_records)}u;\n")

with open(out_path, "w", encoding="utf-8") as handle:
    handle.writelines(out)

print(
    f"sound_player_to_c V4: EXT.DAT blob {len(blob)} bytes: "
    f"{len(def_records)} defs, {len(layer_records)} layers, "
    f"{len(event_records)} events",
    file=sys.stderr,
)

if unsupported_layer_ops:
    print(
        "sound_player_to_c V3: unimplemented layer opcodes: "
        + ", ".join(f"{k}={v}" for k, v in sorted(unsupported_layer_ops.items())),
        file=sys.stderr,
    )
if unsupported_channel_ops:
    print(
        "sound_player_to_c V3: ignored controller-only channel opcodes: "
        + ", ".join(f"{k}={v}" for k, v in sorted(unsupported_channel_ops.items())),
        file=sys.stderr,
    )
if recognized_unrendered:
    print(
        "sound_player_to_c V3: recognized but not directly rendered: "
        + ", ".join(f"{k}={v}" for k, v in sorted(recognized_unrendered.items())),
        file=sys.stderr,
    )
for warning in warnings:
    print(f"sound_player_to_c V3: warning: {warning}", file=sys.stderr)
