"""Generate tfa_rooms_generated.inc: movement flags, boss spawn and player starts from the fixture room files,
projectile flags from each room's exported scene collision (data/tfa_<room>.cmap) at its scene origin.
DAGONHAI has no recording: its movement flags come from the scene collision too, cut to the floor reachable from the starts."""

import collections
import pathlib
import re
import struct

HERE = pathlib.Path(__file__).resolve().parent
FIXTURES = HERE.parent / "fixtures"
OUT = HERE.parent / "tfa_rooms_generated.inc"
DATA = HERE.parents[2] / "data"
SIZE = 64
BLOCKED = 0x200000
LOS_BITS = 0x20000 | 0x1000 | 0x10000 | 0x400 | 0x4000
# World tile of room tile (0, 0) in the exported scene. The Ocellus instance sits 8 tiles north of template region 64,88 (9/9 scenes).
SCENE_ORIGINS = {
    "GUARDIAN": (4096, 5568),
    "BORK": (4160, 5568),
    "EZEKIEL": (4032, 5504),
    "DREADNOTH": (4160, 5632),
    "MOIA": (4032, 5632),
    "XARTH": (4160, 5504),
    "DAGONHAI": (4032, 5568),
    "OCELLUS": (4096, 5624),
}
ROOMS = ["GUARDIAN", "BORK", "EZEKIEL", "DREADNOTH", "MOIA", "XARTH", "DAGONHAI", "OCELLUS"]
BOSS_IDS = {
    "GUARDIAN": 16657,
    "BORK": 16640,
    "EZEKIEL": 16652,
    "DREADNOTH": 16665,
    "MOIA": 16667,
    "XARTH": 16684,
    "OCELLUS": 16673,
}


def section(text, name):
    m = re.search(r"^## " + re.escape(name) + r".*?\n(.*?)(?=^## |\Z)", text, re.S | re.M)
    assert m, name
    return m.group(1)


def parse_room(name):
    """Movement flags from the recorded majority collision. The recorded scene loads see the inert Guardian 16657 blocking its 5x5, which P0
    stood under once it flew (20 of its 25 tiles, centre included, 233 POS rows): the footprint is walkable."""
    text = (FIXTURES / f"tfa_{name.lower()}_room.txt").read_text()
    flags = [[0] * SIZE for _ in range(SIZE)]
    for line in section(text, "MAP").splitlines():
        m = re.match(r"^\s*(\d+) (\S{64})$", line)
        if not m:
            continue
        y = int(m.group(1))
        for x, ch in enumerate(m.group(2)):
            flags[y][x] = BLOCKED if ch in "#?" else 0
    for line in section(text, "WALLS").splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[0].isdigit():
            x, y, mask = int(parts[0]), int(parts[1]), int(parts[2], 16)
            if not flags[y][x]:
                flags[y][x] = mask
    npc_id = BOSS_IDS[name]
    spawns = re.findall(rf"^{npc_id} (\d+) (\d+) : (\d+)/", section(text, "BOSS SPAWN"), re.M)
    bx, by, _ = max(((int(x), int(y), int(n)) for x, y, n in spawns), key=lambda t: t[2])
    if name == "GUARDIAN":
        for y in range(by, by + 5):
            for x in range(bx, bx + 5):
                flags[y][x] = 0
    starts = collections.Counter()
    for m in re.finditer(r"P0@start=\((\d+), (\d+)\)", text):
        starts[(int(m.group(1)), int(m.group(2)))] += 1
    sx, sy = starts.most_common(1)[0][0]
    assert flags[sy][sx] == 0, (name, sx, sy)
    picks = [(sx, sy)]
    for dx, dy in [(1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1), (2, 0), (-2, 0)]:
        if len(picks) == 4:
            break
        if flags[sy + dy][sx + dx] == 0:
            picks.append((sx + dx, sy + dy))
    assert len(picks) == 4, name
    return flags, (bx, by), picks


def scene_flags(name):
    data = (DATA / f"tfa_{name.lower()}.cmap").read_bytes()
    magic, version, count = struct.unpack_from("<3I", data, 0)
    region_bytes = 4 * 64 * 64 * 4
    regions = {}
    for i in range(count):
        off = 12 + i * (4 + region_bytes)
        (key,) = struct.unpack_from("<i", data, off)
        regions[key] = off + 4
    ox, oy = SCENE_ORIGINS[name]
    los = [[0] * SIZE for _ in range(SIZE)]
    for y in range(SIZE):
        for x in range(SIZE):
            wx, wy = ox + x, oy + y
            base = regions[(wx >> 6) << 8 | (wy >> 6)]
            (flags,) = struct.unpack_from("<i", data, base + ((wx & 63) * 64 + (wy & 63)) * 4)
            los[y][x] = flags
    return los


def scene_los(name):
    return [[f & LOS_BITS for f in row] for row in scene_flags(name)]


def scene_room(name, starts):
    """Walls keep their direction bits, objects and blocked floor become BLOCKED, and tiles not reachable from the starts are blocked."""
    raw = scene_flags(name)
    flags = [[BLOCKED if f & (0x100 | BLOCKED | 0x20000) else f & 0xFF for f in row] for row in raw]
    seen = {starts[0]}
    queue = [starts[0]]
    while queue:
        x, y = queue.pop()
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if 0 <= nx < SIZE and 0 <= ny < SIZE and (nx, ny) not in seen and not flags[ny][nx]:
                seen.add((nx, ny))
                queue.append((nx, ny))
    for y in range(SIZE):
        for x in range(SIZE):
            if (x, y) not in seen and not flags[y][x] & BLOCKED:
                flags[y][x] = BLOCKED
    return flags


def dagonhai_room():
    """Provisional spawn (the cauldron, 3x3 in the middle of the floor) and starts south of it until a recording."""
    starts = [(31, 26), (32, 26), (30, 26), (33, 26)]
    return scene_room("DAGONHAI", starts), (31, 30), starts


def main():
    out = ["/* Generated by tools/gen_tfa_rooms.py from fixtures/tfa_*_room.txt and data/tfa_*.cmap. Do not edit.",
           "   DAGONHAI has no recording yet: its floor is the scene's cache collision reachable from provisional starts. */"]
    layouts = []
    for name in ROOMS:
        ox, oy = SCENE_ORIGINS[name]
        out.append(f"#define TFA_SCENE_ORIGIN_X_{name} {ox}")
        out.append(f"#define TFA_SCENE_ORIGIN_Y_{name} {oy}")
        flags, boss, starts = dagonhai_room() if name == "DAGONHAI" else parse_room(name)
        los = scene_los(name)
        for table, rows in ((f"TFA_FLAGS_{name}", flags), (f"TFA_LOS_{name}", los)):
            out.append(f"static const uint32_t {table}[{SIZE * SIZE}] = {{")
            for y in range(SIZE):
                out.append("    " + ", ".join(f"0x{f:x}" for f in rows[y]) + ",")
            out.append("};")
        sx = ", ".join(str(p[0]) for p in starts)
        sy = ", ".join(str(p[1]) for p in starts)
        layouts.append(
            f"    [TFA_ROOM_{name}] = {{ .flags = TFA_FLAGS_{name}, .los = TFA_LOS_{name}, .boss_x = {boss[0]}, .boss_y = {boss[1]},\n"
            f"        .start_x = {{ {sx} }}, .start_y = {{ {sy} }} }},")
    out.append("static const TfaRoomLayout TFA_ROOM_LAYOUTS[TFA_ROOM_COUNT] = {")
    out += layouts
    out.append("};")
    OUT.write_text("\n".join(out) + "\n")


main()
