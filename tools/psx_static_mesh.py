"""Compile self-contained level meshes into the PS1 display-list format."""

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import re
import struct
from typing import NamedTuple

ARRAY = re.compile(r"^(?P<static>static )?const (?P<kind>Vtx|Gfx) (?P<name>\w+)\[\] = \{\n(?P<body>.*?)^\};", re.M | re.S)
COMMAND = re.compile(r"(gs\w+)\((.*)\),")
NUMBER = re.compile(r"-?(?:0[xX][\da-fA-F]+|\d+)")
TEXTURE = re.compile(r'\bTexture\s+(\w+)\[\]\s*=\s*\{\s*#include\s+"([^"]+)\.inc\.c"')
LIGHT = re.compile(r"\bLights1\s+(\w+)\s*=\s*gdSPDefLights1\(([^)]*)\)")
IGNORED = {
    "gsDPPipeSync", "gsDPTileSync", "gsDPLoadSync", "gsDPFullSync",
    "gsDPLoadBlock", "gsDPSetTileSize", "gsDPSetCycleType", "gsDPSetDepthSource",
    "gsDPSetFogColor", "gsSPFogPosition", "gsSPFogFactor", "gsDPSetTextureFilter",
    "gsDPSetTexturePersp", "gsDPSetTextureLOD", "gsDPSetPrimColor", "gsSPNoOp",
}
# Try the finest affordable textured mesh first. The final pass only splits
# edges required by the GPU's UV range, so dense meshes can still run natively.
WORLD_EDGE_LIMITS = (320, 448, 640, 896, 1280, 2048, 4096, None)
VERTICES_PER_BATCH = 256


class UnsupportedMesh(ValueError):
    pass


class Texture(NamedTuple):
    width: int
    height: int
    rotated: bool
    bank_sector: int


@dataclass(frozen=True)
class Vertex:
    xyz: tuple[int, int, int]
    uv: tuple[int, int]
    rgb: tuple[int, int, int]

    def midpoint(self, other):
        def average(left, right):
            return tuple((a + b) // 2 for a, b in zip(left, right))
        return Vertex(average(self.xyz, other.xyz), average(self.uv, other.uv),
                      average(self.rgb, other.rgb))


@dataclass(frozen=True)
class Triangle:
    vertices: tuple[Vertex, Vertex, Vertex]
    repeat: tuple[int, int]
    flags: str


def read_textures(source, build_dir):
    textures = {}
    for name, path in TEXTURE.findall(source):
        header = build_dir / (path + ".texheader")
        if header.is_file():
            textures[name] = Texture(*struct.unpack_from("<HH?11xH", header.read_bytes()))
    return textures


def make_texture_catalog(build_dir, sources):
    textures = {}
    for path in sources:
        textures.update(read_textures(path.read_text(), build_dir))
    return textures


def select_geometry_variant(source, defines):
    lines = []
    parents = []
    active = True
    for line in source.splitlines(keepends=True):
        directive = re.match(r"^\s*#\s*(if\w*|elif|else|endif)\b\s*(\w*)", line)
        if not directive:
            if active:
                lines.append(line)
            continue
        kind, name = directive.groups()
        if kind in {"ifdef", "ifndef"} and name:
            parents.append(active)
            active = active and ((name in defines) == (kind == "ifdef"))
        elif kind == "else" and parents:
            active = parents[-1] and not active
        elif kind == "endif" and parents:
            active = parents.pop()
        else:
            raise UnsupportedMesh(f"unsupported geometry condition {line.strip()}")
    if parents:
        raise UnsupportedMesh("unclosed geometry condition")
    return "".join(lines)


def read_lights(source):
    return {name: [int(value, 0) for value in NUMBER.findall(values)]
            for name, values in LIGHT.findall(source)}


def read_combiners():
    header = Path(__file__).resolve().parents[1] / "include/PR/gbi.h"
    modes = dict(re.findall(r"^#define\s+(G_CC_\w+)\s+([^\n/]+)", header.read_text(), re.M))
    for name, value in modes.items():
        value = value.strip()
        while value in modes:
            value = modes[value].strip()
        modes[name] = value
    return modes


def commands(body):
    body = re.sub(r"/\*.*?\*/|//[^\n]*", "", body, flags=re.S)
    for line in body.splitlines():
        if not line.strip():
            continue
        match = COMMAND.fullmatch(line.strip())
        if not match:
            raise UnsupportedMesh(f"unrecognized command: {line.strip()}")
        yield match[1], [a.strip() for a in match[2].split(",")]


def vertices(body):
    rows = []
    for line in body.splitlines():
        code = line.split("//")[0].strip()
        if not code:
            continue
        values = [int(v, 0) for v in NUMBER.findall(code)]
        if not code.startswith("{{{") or len(values) != 10:
            raise UnsupportedMesh("vertex layout")
        rows.append(Vertex(tuple(values[:3]), tuple(values[4:6]), tuple(values[6:9])))
    return rows


def op(name, value="0"):
    return f"DL_PACK_OP(DL_CMD_{name}) | ({value})"


def pointer(name):
    return f"(uintptr_t) ({name})"


class Material:
    def __init__(self):
        self.geometry = {"G_LIGHTING"}
        self.scale = (1 << 21, 1 << 21)
        self.clamp = (False, False)
        self.texture = None
        self.use_texture = False
        self.use_env_color = False
        self.decal = "0"
        self.num_lights = 1

    def triangle(self, rows, textures):
        lit = "G_LIGHTING" in self.geometry
        textured = self.use_texture and self.texture is not None
        flags = ["0"]
        if lit:
            flags.append("PRIM_FLAG_LIGHTED")
        if not self.geometry.intersection({"G_CULL_BACK", "G_CULL_FRONT", "G_CULL_BOTH"}):
            flags.append("PRIM_FLAG_TWO_SIDED")
        if self.use_env_color:
            flags.append("PRIM_FLAG_ENV_COLOR")
        if "G_PORT_TITLE_LOGO" in self.geometry:
            flags.append("PRIM_FLAG_TITLE_LOGO")
        if textured:
            flags.append("PRIM_FLAG_TEXTURED")
        if self.decal != "0":
            flags.append(self.decal)

        repeat = (0, 0)
        if textured:
            texture = textures[self.texture]
            periods = (texture.width, texture.height)
            clamp = self.clamp[::-1] if texture.rotated else self.clamp
            repeat = tuple(0 if clip else period for period, clip in zip(periods, clamp))
            if any(period and (period & (period - 1) or period > 128) for period in repeat):
                raise UnsupportedMesh(f"texture size for {self.texture}")

        prepared = []
        for row in rows:
            rgb = tuple((c + 128) % 256 - 128 for c in row.rgb) if lit else row.rgb
            uv = (0, 0)
            if textured:
                coords = row.uv[::-1] if texture.rotated else row.uv
                scaled = tuple((coord * (scale >> 8)) >> 13 for coord, scale in zip(coords, self.scale))
                uv = tuple(max(0, min(size, coord)) if clip else coord
                           for size, coord, clip in zip(periods, scaled, clamp))
            prepared.append(Vertex(row.xyz, uv, rgb))
        return Triangle(tuple(prepared), repeat, " | ".join(flags))


def compile_list(root, lists, batches, lights, textures, combiners):
    state = Material()
    selected = None
    stack = set()
    texture_symbols = {}

    def select_texture(symbol):
        if symbol not in textures:
            raise UnsupportedMesh(f"unknown texture {symbol}")
        state.texture = symbol
        texture_symbols[symbol] = None
        return op("SEG_TEX"), pointer(symbol)

    def walk(name):
        nonlocal selected
        if name in stack or name not in lists:
            raise UnsupportedMesh(f"nonlocal or recursive display list {name}")
        stack.add(name)
        for command, args in lists[name]:
            if command in IGNORED:
                continue
            if command in {"gsSPEndDisplayList", "gsSPBranchList"}:
                if command == "gsSPBranchList":
                    yield from walk(args[0])
                break
            if command == "gsSPDisplayList":
                yield from walk(args[0])
            elif command == "gsSPVertex":
                first, count, offset = args
                if int(offset, 0) != 0 or first not in batches or not 0 < int(count, 0) <= len(batches[first]):
                    raise UnsupportedMesh(f"external or partial vertex load {first}")
                selected = batches[first][:int(count, 0)]
            elif command in {"gsSP1Triangle", "gsSP2Triangles"}:
                if selected is None:
                    raise UnsupportedMesh("triangle without vertices")
                for i in range(0, len(args), 4):
                    yield state.triangle([selected[int(v, 0)] for v in args[i:i + 3]], textures)
            elif command in {"gsSPSetGeometryMode", "gsSPClearGeometryMode", "gsSPGeometryMode"}:
                clear = args[0] if command != "gsSPSetGeometryMode" else "0"
                set_modes = args[-1] if command != "gsSPClearGeometryMode" else "0"
                enabled = {mode.strip() for mode in set_modes.split("|")}
                if "G_TEXTURE_GEN" in enabled:
                    raise UnsupportedMesh("camera-dependent texture generation")
                if clear in {"-1", "0xFFFFFFFF"}:
                    state.geometry.clear()
                else:
                    state.geometry.difference_update(mode.strip() for mode in clear.split("|"))
                state.geometry.update(enabled)
            elif command == "gsSPTexture":
                state.scale = tuple(int(v, 0) for v in args[:2])
            elif command in {"gsDPSetTile", "gsDPLoadTextureBlock"}:
                if command == "gsDPSetTile":
                    if args[4] == "G_TX_LOADTILE":
                        continue
                    state.clamp = ("G_TX_CLAMP" in args[9], "G_TX_CLAMP" in args[6])
                else:
                    state.clamp = ("G_TX_CLAMP" in args[6], "G_TX_CLAMP" in args[7])
                    yield select_texture(args[0])
            elif command == "gsDPSetTextureImage":
                yield select_texture(args[3])
            elif command == "gsDPSetCombineMode":
                if args[0] not in combiners:
                    raise UnsupportedMesh(f"unknown combiner {args[0]}")
                a, b, c, d, *_ = (term.strip() for term in combiners[args[0]].split(","))
                state.use_texture = "TEXEL0" in (a, b, c, d)
                state.use_env_color = "ENVIRONMENT" in (c, d)
                state.decal = "PRIM_FLAG_DECAL" if b == d else "0"
                if b == d and a == "PRIMITIVE":
                    state.use_texture = False
                    state.use_env_color = True
            elif command == "gsDPSetRenderMode":
                state.decal = f"((({args[0]} | {args[1]}) & ZMODE_DEC) == ZMODE_DEC ? PRIM_FLAG_DECAL : 0)"
            elif command == "gsSPNumLights":
                state.num_lights = int(args[0].removeprefix("NUMLIGHTS_"), 0)
            elif command in {"gsSPLight", "gsSPSetLights1"}:
                if command == "gsSPSetLights1":
                    symbol = args[0]
                    state.num_lights = 1
                    indices = [1, 2]
                else:
                    symbol = args[0].removeprefix("&").split(".")[0]
                    indices = [int(args[1], 0)]
                if symbol not in lights:
                    raise UnsupportedMesh(f"unknown light {symbol}")
                for index in indices:
                    which = state.num_lights + 1 - index
                    if which == 0:
                        r, g, b = lights[symbol][:3]
                        yield (op("LIGHT_AMBIENT", f"PSX_STATIC_AMBIENT({r}, {g}, {b})"),)
                    elif which in (1, 2):
                        r, g, b, x, y, z = lights[symbol][3:]
                        normal = (x & 255) | ((y & 255) << 8) | ((z & 255) << 16)
                        yield op(f"STATIC_LIGHT{which - 1}", f"PSX_STATIC_AMBIENT({r}, {g}, {b})"), hex(normal)
                    else:
                        raise UnsupportedMesh("light index")
            elif command == "gsDPSetEnvColor":
                r, g, b, alpha = (int(a, 0) for a in args)
                suffix = "FULL" if alpha >= 192 else "HALF" if alpha >= 32 else "0"
                yield (op(f"ENV_COLOR_ALPHA_{suffix}", hex((r << 16) | (g << 8) | b)),)
            else:
                raise UnsupportedMesh(f"unsupported command {command}")
        else:
            raise UnsupportedMesh(f"display list {name} has no end")
        stack.remove(name)

    events = list(walk(root))
    # The loader has two CD bank buffers. Upload each bank's textures together
    # before executing the original material and triangle order.
    preload = sorted(texture_symbols, key=lambda symbol: textures[symbol].bank_sector)
    return [(op("STATIC_MESH", str(len(preload))), *(pointer(t) for t in preload)), *events]


def face_area_sq(a, b, c):
    abx, aby, abz = (y - x for x, y in zip(a.xyz, b.xyz))
    acx, acy, acz = (y - x for x, y in zip(a.xyz, c.xyz))
    return (aby * acz - abz * acy) ** 2 + (abz * acx - abx * acz) ** 2 + (abx * acy - aby * acx) ** 2


def split_triangle(triangle, split_edges, world_edge_sq):
    points = triangle.vertices
    area_sq = face_area_sq(*points)
    if not area_sq:
        return
    textured = "PRIM_FLAG_TEXTURED" in triangle.flags and len({v.uv for v in points}) > 1
    best = None
    best_midpoint = None
    for index, a in enumerate(points):
        b = points[(index + 1) % 3]
        edge = tuple(sorted((a.xyz, b.xyz)))
        length_sq = sum((x - y) ** 2 for x, y in zip(a.xyz, b.xyz))
        uv_span = max((max(x, y) - min(x, y) // period * period
                       for x, y, period in zip(a.uv, b.uv, triangle.repeat) if period), default=0)
        if edge in split_edges:
            priority = 3
        elif uv_span > 255:
            priority = 2
        elif textured and world_edge_sq is not None and length_sq > world_edge_sq:
            priority = 1
        else:
            continue
        candidate = priority, length_sq, edge, index
        if best is not None and candidate <= best:
            continue
        midpoint = a.midpoint(b)
        c = points[(index + 2) % 3]
        # Rounding a very thin face to integer coordinates can leave one
        # child as large as its parent. Require each split to make progress.
        if (0 < face_area_sq(a, midpoint, c) < area_sq
                and 0 < face_area_sq(midpoint, b, c) < area_sq):
            best = candidate
            best_midpoint = midpoint

    if best is None:
        yield triangle
        return

    _, _, edge, index = best
    a, b, c = points[index:] + points[:index]
    split_edges.add(edge)
    for child in ((a, best_midpoint, c), (best_midpoint, b, c)):
        yield from split_triangle(Triangle(child, triangle.repeat, triangle.flags), split_edges, world_edge_sq)


def tessellate(lists, world_edge, max_triangles=None):
    world_edge_sq = None if world_edge is None else world_edge ** 2
    split_edges = set()
    previous_count = -1
    while len(split_edges) != previous_count:
        previous_count = len(split_edges)
        result = {}
        triangle_count = 0
        for name, events in lists.items():
            pieces = result[name] = []
            for item in events:
                if not isinstance(item, Triangle):
                    pieces.append(item)
                    continue
                for triangle in split_triangle(item, split_edges, world_edge_sq):
                    triangle_count += 1
                    if max_triangles is not None and triangle_count > max_triangles:
                        return None
                    pieces.append(triangle)
    return result


def uv_offsets(vertices, repeat):
    result = []
    for axis, period in enumerate(repeat):
        low = min(vertex.uv[axis] for vertex in vertices)
        high = max(vertex.uv[axis] for vertex in vertices)
        offset = low // period * period if period else 0
        if low < offset or high > offset + 255:
            return None
        result.append(offset)
    return tuple(result)


def emit_list(name, declaration, events):
    source = []
    stream = []
    batch = {}
    batch_commands = []
    batch_index = 0
    repeat = None
    flags = None
    size = 0

    def flush():
        nonlocal batch_index, size
        if not batch:
            return
        symbol = f"{name}_psx_vertices_{batch_index}"
        batch_index += 1
        source.append(f"static const GfxVtx {symbol}[] = {{\n")
        offset_u, offset_v = uv_offsets(batch, repeat)
        size += len(batch) * 12
        for point in batch:
            x, y, z = point.xyz
            u, v = point.uv[0] - offset_u, point.uv[1] - offset_v
            r, g, b = (c & 255 for c in point.rgb)
            source.append(f"    {{.x={x}, .y={y}, .z={z}, .u={u}, .v={v}, .color={{.r={r}, .g={g}, .b={b}}}}},\n")
        source.append("};\n")
        stream.extend((op("SEG_VTX", flags), pointer(symbol)))
        stream.extend(batch_commands)
        batch.clear()
        batch_commands.clear()

    for item in events:
        if not isinstance(item, Triangle):
            # Texture and light commands do not invalidate the vertex table.
            # Keep their original order inside a compatible batch.
            (batch_commands if batch else stream).extend(item)
            continue
        points = item.vertices
        combined = batch.keys() | set(points)
        if (repeat != item.repeat or flags != item.flags or len(combined) > VERTICES_PER_BATCH
                or uv_offsets(combined, repeat) is None):
            flush()
            repeat = item.repeat
            flags = item.flags
        if uv_offsets(points, repeat) is None:
            raise UnsupportedMesh("UV span exceeds PS1 interpolation range")
        i, j, k = (batch.setdefault(point, len(batch)) for point in points)
        batch_commands.append(op("STATIC_TRI", f"({i} << 16) | ({j} << 8) | {k}"))
    flush()
    stream.append(op("END"))
    if len(stream) % 2:
        stream.append("0")
    source.append(declaration + "\n")
    for left, right in zip(stream[::2], stream[1::2]):
        source.append(f"    {{.words = {{{left}, {right}}}}},\n")
    source.append("};\n")
    return "".join(source), size + len(stream) * 4


def compile_mesh(source, textures, combiners, defines=(), shared_lights=None):
    source = select_geometry_variant(source, defines)
    arrays = list(ARRAY.finditer(source))
    lists = {a["name"]: list(commands(a["body"])) for a in arrays if a["kind"] == "Gfx"}
    batches = {a["name"]: vertices(a["body"]) for a in arrays if a["kind"] == "Vtx"}
    if not lists or not batches:
        raise UnsupportedMesh("no static geometry")
    remainder = ARRAY.sub("", source)
    if re.search(r"\b(?:Gfx|Vtx)\s+\w+\s*\[\s*\]\s*=\s*\{", remainder):
        raise UnsupportedMesh("mixed geometry declarations")
    referenced = set(re.findall(r"\b\w+\b", remainder))
    if any(a["kind"] == "Vtx" and (not a["static"] or a["name"] in referenced) for a in arrays):
        raise UnsupportedMesh("externally visible vertex data")
    roots = [a for a in arrays if a["kind"] == "Gfx" and (not a["static"] or a["name"] in referenced)]
    if not roots:
        raise UnsupportedMesh("no display-list roots")
    lights = (shared_lights or {}) | read_lights(source)
    programs = {a["name"]: compile_list(a["name"], lists, batches, lights, textures, combiners)
                for a in roots}
    # Keep each generated mesh within the original overlay allocation.
    old_size = sum(len(rows) * 16 for rows in batches.values())
    old_size += sum(48 if name == "gsDPLoadTextureBlock" else 8
                    for items in lists.values() for name, _ in items)
    native_size = 0
    for edge_limit in WORLD_EDGE_LIMITS:
        # Every triangle needs at least one command word, before vertices.
        result = tessellate(programs, edge_limit, old_size // 4)
        if result is None:
            continue
        output = ['#include <port/gfx/gfx_internal.h>\n#include <port/gfx/gfx_light_color.h>\n', remainder]
        native_size = 0
        for a in roots:
            declaration = source[a.start():a.start("body")].rstrip()
            compiled, size = emit_list(a["name"], declaration, result[a["name"]])
            output.append(compiled)
            native_size += size
        if native_size <= old_size:
            return "".join(output)
    raise UnsupportedMesh(f"native geometry would increase the overlay ({old_size} to {native_size} bytes)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("-D", action="append", default=[])
    parser.add_argument("paths", type=Path, nargs="+")
    args = parser.parse_args()
    if args.catalog is None:
        out, *sources = args.paths
        out.write_text(json.dumps(make_texture_catalog(args.build_dir, sources)))
        return

    model, out = args.paths
    source = model.read_text()
    textures = {name: Texture(*record) for name, record in json.loads(args.catalog.read_text()).items()}
    textures.update(read_textures(source, args.build_dir))
    shared_lights = {}
    for parent in model.parents:
        if parent.parent.name == "areas":
            path = parent / "light.inc.c"
            if path.is_file():
                shared_lights = read_lights(path.read_text())
            break
    try:
        compiled = compile_mesh(source, textures, read_combiners(),
                                {definition.split("=")[0] for definition in args.D}, shared_lights)
    except (ValueError, IndexError) as exc:
        print(f"PS1 static mesh fallback for {model}: {exc}")
        compiled = source
    out.write_text(compiled)


if __name__ == "__main__":
    main()
