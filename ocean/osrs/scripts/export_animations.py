"""Export OSRS animation data from cache to a binary .anims file.

Reads framebases, frame archives, and sequence (animation) definitions
from a modern OpenRS2 flat file cache. Outputs a compact binary
consumable by osrs_pvp_anim.h.

Modern cache sources:
  - frame bases: index 1 (each group is a framebase)
  - sequences: config index 2, group 12
  - frame archives: index 0

Usage:
    uv run python scripts/export_animations.py \
        --modern-cache ../reference/osrs-cache-modern \
        --output ../data/equipment.anims \
        --include-spotanim-sequences \
        --item-render-map ../../refs/RuneC/data/models/item_render.map \
        --combat-visuals ../../refs/RuneC/data/defs/combat_visuals.tsv
"""

import argparse
import io
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from modern_cache_reader import (
    ModernCacheReader,
    parse_sequence as parse_modern_sequence,
)

MODERN_FRAME_INDEX = 0  # modern cache: frame archives
MODERN_FRAMEBASE_INDEX = 1  # modern cache: frame bases
MODERN_SEQ_CONFIG_GROUP = 12  # modern cache: config index 2, group 12
MODERN_SPOTANIM_CONFIG_GROUP = 13  # modern cache: config index 2, group 13
ITEM_RENDER_MAGIC = 0x4D455249
ITEM_RENDER_V2 = 2
MISSING_U32 = 0xFFFFFFFF


# --- binary reading helpers ---


def read_ubyte(buf: io.BytesIO) -> int:
    """Read unsigned byte from stream."""
    b = buf.read(1)
    if not b:
        return 0
    return b[0]


def read_ushort(buf: io.BytesIO) -> int:
    """Read big-endian unsigned short from stream."""
    b = buf.read(2)
    if len(b) < 2:
        return 0
    return (b[0] << 8) | b[1]


def read_short_smart(buf: io.BytesIO) -> int:
    """Read signed short smart (same as Java Buffer.readShortSmart).

    Single byte (peek < 128): value - 64 (range -64 to 63)
    Two bytes:                value - 0xC000 (range -16384 to 16383)
    """
    pos = buf.tell()
    peek = buf.read(1)
    if not peek:
        return 0
    val = peek[0]
    if val < 128:
        return val - 64
    buf.seek(pos)
    raw = struct.unpack(">H", buf.read(2))[0]
    return raw - 0xC000


# --- data structures ---


@dataclass
class FrameBaseDef:
    """Transform slot layout — defines which vertex groups each slot operates on.

    Each slot has a type (0=origin, 1=translate, 2=rotate, 3=scale, 5=alpha)
    and a list of vertex group label indices (frameMaps).
    """

    base_id: int = 0
    slot_count: int = 0
    types: list[int] = field(default_factory=list)
    frame_maps: list[list[int]] = field(default_factory=list)


@dataclass
class FrameDef:
    """Single animation frame — a list of transforms to apply.

    Each entry references a slot in the FrameBase and provides dx/dy/dz values.
    Origin slots (type 0) auto-inserted before non-origin transforms.
    """

    framebase_id: int = 0
    translator_count: int = 0
    slot_indices: list[int] = field(default_factory=list)
    dx: list[int] = field(default_factory=list)
    dy: list[int] = field(default_factory=list)
    dz: list[int] = field(default_factory=list)


@dataclass
class SequenceDef:
    """Animation sequence — ordered frames with timing and blend metadata.

    primaryFrameIds encode (groupId << 16 | fileId) for cache frame lookup.
    interleaveOrder defines which slots come from secondary (idle/walk) animation.
    """

    seq_id: int = 0
    frame_count: int = 0
    frame_delays: list[int] = field(default_factory=list)
    primary_frame_ids: list[int] = field(default_factory=list)
    frame_step: int = -1
    interleave_order: list[int] = field(default_factory=list)
    priority: int = 5
    loop_count: int = 99
    walk_flag: int = -1  # opcode 10: 0=stall movement, -1=default (derive from interleave)
    run_flag: int = -1   # opcode 9: 0=stall pre-anim steps, -1=default


# --- parsing ---


def _parse_normal_frame(
    group_id: int,
    file_id: int,
    data: bytes,
    framebases: dict[int, FrameBaseDef],
) -> FrameDef | None:
    """Parse a NormalFrame from raw bytes. Mirrors Java NormalFrame constructor."""
    fbuf = io.BytesIO(data)

    framebase_id = read_ushort(fbuf)
    slot_count = read_ubyte(fbuf)

    fb = framebases.get(framebase_id)
    if fb is None:
        return None

    types = fb.types

    # read attribute bytes (one per slot)
    attr_start = fbuf.tell()
    attributes = [read_ubyte(fbuf) for _ in range(slot_count)]

    # data stream starts after attributes
    dbuf = io.BytesIO(data)
    dbuf.seek(slot_count + attr_start)

    slot_indices: list[int] = []
    dx_list: list[int] = []
    dy_list: list[int] = []
    dz_list: list[int] = []

    last_i = -1
    for i in range(slot_count):
        attr = attributes[i]
        if attr <= 0:
            continue

        # get the slot type from the framebase
        slot_type = types[i] if i < len(types) else 0

        # auto-insert preceding origin slot (type 0) if this slot isn't an origin
        if slot_type != 0:
            for j in range(i - 1, last_i, -1):
                if j < len(types) and types[j] == 0:
                    slot_indices.append(j)
                    dx_list.append(0)
                    dy_list.append(0)
                    dz_list.append(0)
                    break

        slot_indices.append(i)

        default_val = 128 if slot_type == 3 else 0
        dx_list.append(read_short_smart(dbuf) if (attr & 1) else default_val)
        dy_list.append(read_short_smart(dbuf) if (attr & 2) else default_val)
        dz_list.append(read_short_smart(dbuf) if (attr & 4) else default_val)

        last_i = i

    frame = FrameDef(
        framebase_id=framebase_id,
        translator_count=len(slot_indices),
        slot_indices=slot_indices,
        dx=dx_list,
        dy=dy_list,
        dz=dz_list,
    )
    return frame


# --- binary output ---


ANIM_MAGIC = 0x414E494D  # "ANIM"


def write_animations_binary(
    output_path: Path,
    framebases: dict[int, FrameBaseDef],
    all_frames: dict[int, dict[int, FrameDef]],
    sequences: dict[int, SequenceDef],
    needed_seq_ids: set[int],
) -> None:
    """Write animation data to .anims binary format.

    Only exports sequences in needed_seq_ids and their referenced framebases/frames.

    Binary layout:
      header:
        uint32 magic ("ANIM")
        uint16 framebase_count
        uint16 sequence_count

      framebases section (sorted by id):
        for each framebase:
          uint16 base_id
          uint8  slot_count
          uint8[slot_count] types
          for each slot:
            uint8 map_length
            uint8[map_length] frame_map entries

      sequences section:
        for each sequence:
          uint16 seq_id
          uint16 frame_count
          uint8  interleave_count (0 if none)
          uint8[interleave_count] interleave_order
          int8   walk_flag (-1=default, 0=stall movement during anim)
          for each frame in sequence:
            uint16 delay (game ticks)
            uint16 framebase_id
            uint8  translator_count
            for each translator:
              uint8  slot_index
              int16  dx
              int16  dy
              int16  dz
    """
    output_path.parent.mkdir(parents=True, exist_ok=True)

    # collect needed framebases from sequences
    needed_bases: set[int] = set()
    valid_seqs: list[SequenceDef] = []
    for seq_id in sorted(needed_seq_ids):
        seq = sequences.get(seq_id)
        if seq is None:
            continue

        # check all frames exist
        has_frames = True
        for fid in seq.primary_frame_ids:
            if fid == -1:
                continue
            group_id = fid >> 16
            file_id = fid & 0xFFFF
            group = all_frames.get(group_id)
            if group is None or file_id not in group:
                has_frames = False
                break
            needed_bases.add(group[file_id].framebase_id)

        if has_frames:
            valid_seqs.append(seq)

    # remap framebase IDs to compact indices
    sorted_bases = sorted(needed_bases)
    base_id_to_idx = {bid: idx for idx, bid in enumerate(sorted_bases)}

    with open(output_path, "wb") as f:
        # header
        f.write(struct.pack("<I", ANIM_MAGIC))
        f.write(struct.pack("<H", len(sorted_bases)))
        f.write(struct.pack("<H", len(valid_seqs)))

        # framebases section
        for base_id in sorted_bases:
            fb = framebases[base_id]
            f.write(struct.pack("<H", base_id))
            f.write(struct.pack("B", fb.slot_count))
            for t in fb.types:
                f.write(struct.pack("B", t))
            for fmap in fb.frame_maps:
                f.write(struct.pack("B", len(fmap)))
                for entry in fmap:
                    f.write(struct.pack("B", entry))

        # sequences section
        for seq in valid_seqs:
            f.write(struct.pack("<H", seq.seq_id))
            f.write(struct.pack("<H", seq.frame_count))

            il = seq.interleave_order
            f.write(struct.pack("B", len(il)))
            for v in il:
                f.write(struct.pack("B", v))

            # walk_flag: signed int8 (-1=default/no stall, 0=stall movement)
            # matches Animation.java walkFlag from opcode 10
            f.write(struct.pack("b", seq.walk_flag))

            for i in range(seq.frame_count):
                delay = seq.frame_delays[i]
                f.write(struct.pack("<H", max(0, delay)))

                fid = seq.primary_frame_ids[i]
                if fid == -1:
                    # empty frame
                    f.write(struct.pack("<H", 0xFFFF))
                    f.write(struct.pack("B", 0))
                    continue

                group_id = fid >> 16
                file_id = fid & 0xFFFF
                frame = all_frames[group_id][file_id]

                f.write(struct.pack("<H", frame.framebase_id))
                f.write(struct.pack("B", frame.translator_count))

                for j in range(frame.translator_count):
                    f.write(struct.pack("B", frame.slot_indices[j]))
                    f.write(struct.pack("<h", frame.dx[j]))
                    f.write(struct.pack("<h", frame.dy[j]))
                    f.write(struct.pack("<h", frame.dz[j]))

    file_size = output_path.stat().st_size
    print(f"\nwrote {file_size:,} bytes to {output_path}")
    print(f"  {len(sorted_bases)} framebases, {len(valid_seqs)} sequences")


# --- known animation IDs for PvP simulation ---

# player default animations
ANIM_IDLE = 808
ANIM_WALK = 819
ANIM_RUN = 824
ANIM_TURN_180 = 820
ANIM_TURN_CW = 821
ANIM_TURN_CCW = 822

# attack animations by weapon (from OSRS wiki)
ANIM_WHIP_ATTACK = 1658
ANIM_WHIP_SPEC = 1658
ANIM_STAFF_BASH = 393
ANIM_STAFF_SLAM = 414
ANIM_DAGGER_STAB = 376
ANIM_DAGGER_SPEC = 1062
ANIM_CROSSBOW_SHOOT = 4230
ANIM_SCIMITAR_SLASH = 390
ANIM_LONGSWORD_SLASH = 390
ANIM_2H_SLASH = 407
ANIM_MAUL_CRUSH = 1665
ANIM_MAUL_SPEC = 1667
ANIM_CLAWS_SLASH = 393
ANIM_CLAWS_SPEC = 7514
ANIM_GODSWORD_SLASH = 7045
ANIM_GODSWORD_READY = 7053
ANIM_GODSWORD_WALK = 7052
ANIM_GODSWORD_RUN = 7043
ANIM_GODSWORD_TURN = 7044
ANIM_GODSWORD_WALK_LEFT = 7048
ANIM_GODSWORD_WALK_RIGHT = 7047
ANIM_GODSWORD_BLOCK = 7056
ANIM_GODSWORD_SPEC_AGS = 7644
ANIM_BALLISTA_SHOOT = 7218
ANIM_DARK_BOW_SHOOT = 426
ANIM_JAVELIN_THROW = 806
ANIM_WARHAMMER_CRUSH = 401
ANIM_WARHAMMER_SPEC = 1378
ANIM_RAPIER_STAB = 8145
ANIM_MACE_CRUSH = 400
ANIM_INQUISITOR_SPEC = 1060
ANIM_ELDER_MAUL_CRUSH = 7516
ANIM_VOIDWAKER_SPEC = 1378
ANIM_VLS_SLASH = 390
ANIM_VLS_SPEC = 7515

# magic animations
ANIM_CAST_STANDARD = 1162
ANIM_CAST_ICE_BARRAGE = 1979
ANIM_CAST_BLOOD_BARRAGE = 1979
ANIM_CAST_VENGEANCE = 4410

# eating / potions
ANIM_EAT = 829
ANIM_DRINK = 829

# defensive
ANIM_BLOCK_SHIELD = 1156
ANIM_BLOCK_MELEE = 424
ANIM_DEATH = 836
ANIM_PUNCH = 422

# kodai wand
ANIM_KODAI_BASH = 414
ANIM_VOLATILE_SPEC = 8532

# all animation IDs we want to export
NEEDED_ANIMATIONS = {
    ANIM_IDLE, ANIM_WALK, ANIM_RUN,
    ANIM_TURN_180, ANIM_TURN_CW, ANIM_TURN_CCW,
    ANIM_WHIP_ATTACK, ANIM_STAFF_BASH, ANIM_STAFF_SLAM,
    ANIM_DAGGER_STAB, ANIM_DAGGER_SPEC,
    ANIM_CROSSBOW_SHOOT, ANIM_SCIMITAR_SLASH,
    ANIM_2H_SLASH, ANIM_MAUL_CRUSH, ANIM_MAUL_SPEC,
    ANIM_CLAWS_SLASH, ANIM_CLAWS_SPEC,
    ANIM_GODSWORD_SLASH, ANIM_GODSWORD_READY, ANIM_GODSWORD_WALK,
    ANIM_GODSWORD_RUN, ANIM_GODSWORD_TURN, ANIM_GODSWORD_WALK_LEFT,
    ANIM_GODSWORD_WALK_RIGHT, ANIM_GODSWORD_BLOCK,
    ANIM_GODSWORD_SPEC_AGS,
    ANIM_BALLISTA_SHOOT, ANIM_DARK_BOW_SHOOT,
    ANIM_JAVELIN_THROW,
    ANIM_WARHAMMER_CRUSH, ANIM_WARHAMMER_SPEC,
    ANIM_RAPIER_STAB, ANIM_MACE_CRUSH, ANIM_INQUISITOR_SPEC,
    ANIM_ELDER_MAUL_CRUSH, ANIM_VOIDWAKER_SPEC,
    ANIM_VLS_SLASH, ANIM_VLS_SPEC,
    ANIM_CAST_STANDARD, ANIM_CAST_ICE_BARRAGE,
    ANIM_CAST_VENGEANCE,
    ANIM_EAT,
    ANIM_BLOCK_SHIELD, ANIM_BLOCK_MELEE,
    ANIM_DEATH, ANIM_PUNCH,
    ANIM_KODAI_BASH, ANIM_VOLATILE_SPEC,
    # spotanim effect animations (spell effects, projectiles)
    653,   # magic splash (GFX 85)
    1964,  # ice barrage projectile (GFX 368)
    1965,  # ice barrage impact (GFX 369)
    1967,  # blood barrage impact (GFX 377)
    # zulrah NPC animations
    5068, 5069, 5070, 5071, 5072,  # zulrah attack/idle/dive/surface
    5806, 5807,                     # zulrah additional anims
    # snakeling animations
    1721,  # snakeling idle
    140,   # snakeling melee attack
    185,   # snakeling magic attack
    138,   # snakeling death
    2405,  # snakeling walk
    # zulrah spotanim/projectile animations
    5358,  # GFX 1044 ranged projectile / GFX 1047 snakeling spawn orb
    3151,  # GFX 1045 toxic cloud
    6648,  # GFX 1046 magic projectile
    # player weapon attack animations (zulrah encounter)
    1167,  # trident cast wave (HUMAN_CASTWAVE_STAFF)
    1074,  # magic shortbow snapshot special
    5061,  # toxic blowpipe attack
    # player weapon projectile spotanim animations
    5460,  # GFX 665 trident casting effect
    5461,  # GFX 1042 trident impact
    5462,  # GFX 1040 trident projectile
    6622,  # GFX 1122 dragon dart projectile
    876,   # GFX 1043 blowpipe special attack
}


def _skip_bytes(buf: io.BytesIO, count: int, context: str) -> None:
    skipped = buf.read(count)
    if len(skipped) != count:
        sys.exit(f"truncated {context}")


def _skip_cstring(buf: io.BytesIO, context: str) -> None:
    while True:
        b = buf.read(1)
        if not b:
            sys.exit(f"truncated {context}")
        if b[0] == 0:
            return


def parse_spotanim_sequence_ids(reader: ModernCacheReader) -> set[int]:
    """Return every animation sequence referenced by spotanim definitions."""

    files = reader.read_group(2, MODERN_SPOTANIM_CONFIG_GROUP)
    out: set[int] = set()

    for gfx_id, data in files.items():
        buf = io.BytesIO(data)
        while True:
            opcode_raw = buf.read(1)
            if not opcode_raw:
                break
            opcode = opcode_raw[0]

            if opcode == 0:
                break
            if opcode == 1:
                _skip_bytes(buf, 2, f"spotanim {gfx_id} model id")
            elif opcode == 2:
                anim_id = read_ushort(buf)
                if anim_id != 0xFFFF:
                    out.add(anim_id)
            elif opcode == 3:
                _skip_bytes(buf, 4, f"spotanim {gfx_id} model id")
            elif opcode in (4, 5, 6):
                _skip_bytes(buf, 2, f"spotanim {gfx_id} u16 opcode {opcode}")
            elif opcode in (7, 8):
                _skip_bytes(buf, 1, f"spotanim {gfx_id} u8 opcode {opcode}")
            elif opcode == 9:
                _skip_cstring(buf, f"spotanim {gfx_id} string opcode")
            elif opcode in (40, 41):
                count = read_ubyte(buf)
                _skip_bytes(buf, count * 4, f"spotanim {gfx_id} pairs opcode {opcode}")
            else:
                sys.exit(f"unknown spotanim opcode {opcode} for gfx {gfx_id}")

    return out


def read_item_render_sequence_ids(path: Path) -> set[int]:
    """Return ready, walk, and run sequence ids from a RuneC item render map."""

    if not path.is_file():
        return set()

    data = path.read_bytes()
    if len(data) < 16:
        sys.exit(f"item render map too small: {path}")

    magic, version, count, body_count = struct.unpack_from("<IIII", data, 0)
    if magic != ITEM_RENDER_MAGIC:
        sys.exit(f"bad item render map magic in {path}: 0x{magic:08x}")
    if version < ITEM_RENDER_V2:
        return set()

    pos = 16 + body_count * 4
    row_size = struct.calcsize("<IIIIIIIIIIIII")
    out: set[int] = set()

    for _ in range(count):
        if pos + row_size > len(data):
            sys.exit(f"truncated item render row in {path}")
        fields = struct.unpack_from("<IIIIIIIIIIIII", data, pos)
        pos += row_size
        for anim_id in fields[10:13]:
            if anim_id != MISSING_U32 and 0 < anim_id <= 0xFFFF:
                out.add(int(anim_id))

    return out


def read_combat_visual_sequence_ids(path: Path) -> set[int]:
    """Return attack and projectile sequence ids from RuneC combat visuals."""

    if not path.is_file():
        return set()

    out: set[int] = set()
    for raw in path.read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or line.startswith("kind|"):
            continue
        parts = line.split("|")
        for idx in (3, 8, 30):
            if idx >= len(parts):
                continue
            value = parts[idx].strip()
            if not value or value == "-":
                continue
            try:
                anim_id = int(value)
            except ValueError:
                sys.exit(f"bad combat visual sequence id {value!r} in {path}")
            if 0 < anim_id <= 0xFFFF:
                out.add(anim_id)

    return out


def parse_extra_sequence_ids(raw: str) -> set[int]:
    """Parse comma-separated extra sequence ids."""

    if not raw:
        return set()
    out: set[int] = set()
    for piece in raw.split(","):
        value = piece.strip()
        if not value:
            continue
        seq_id = int(value, 0)
        if seq_id <= 0 or seq_id > 0xFFFF:
            sys.exit(f"extra sequence id out of range: {seq_id}")
        out.add(seq_id)
    return out


def parse_modern_framebase(base_id: int, data: bytes) -> FrameBaseDef:
    """Parse a single framebase from modern cache (index 1).

    Modern framebases are stored as individual entries. The format inside
    is the same as 317: u8 slot_count, u8[slot_count] types,
    u8[slot_count] map_lengths, then map entries.
    """
    fb = FrameBaseDef(base_id=base_id)
    fbuf = io.BytesIO(data)

    fb.slot_count = read_ubyte(fbuf)
    fb.types = [read_ubyte(fbuf) for _ in range(fb.slot_count)]

    map_lengths = [read_ubyte(fbuf) for _ in range(fb.slot_count)]
    fb.frame_maps = []
    for length in map_lengths:
        fb.frame_maps.append([read_ubyte(fbuf) for _ in range(length)])

    return fb


def load_modern_framebases(
    reader: ModernCacheReader, needed_base_ids: set[int],
) -> dict[int, FrameBaseDef]:
    """Load framebases from modern cache index 1.

    Each framebase is a separate group in index 1. Groups may contain
    multiple files — we use file 0 as the framebase data.
    """
    framebases: dict[int, FrameBaseDef] = {}

    for base_id in sorted(needed_base_ids):
        raw = reader.read_container(MODERN_FRAMEBASE_INDEX, base_id)
        if raw is None:
            print(f"  warning: framebase {base_id} not found in index {MODERN_FRAMEBASE_INDEX}")
            continue

        fb = parse_modern_framebase(base_id, raw)
        framebases[base_id] = fb

    return framebases


def load_modern_frame_archive(
    reader: ModernCacheReader,
    group_id: int,
    framebases: dict[int, FrameBaseDef],
) -> dict[int, FrameDef]:
    """Load a frame archive from modern cache index 0.

    In modern cache, frame archives are in index 0. Each group contains
    multiple files (one per frame). We use read_group to get all files,
    then parse each as a NormalFrame.
    """
    try:
        files = reader.read_group(MODERN_FRAME_INDEX, group_id)
    except (KeyError, FileNotFoundError):
        return {}

    frames: dict[int, FrameDef] = {}
    for file_id, file_data in files.items():
        if len(file_data) < 3:
            continue
        frame = _parse_normal_frame(group_id, file_id, file_data, framebases)
        if frame is not None:
            frames[file_id] = frame

    return frames


def main() -> None:
    """Export animation data from modern OpenRS2 cache."""
    parser = argparse.ArgumentParser(description="export OSRS animations from cache")
    parser.add_argument("--modern-cache", type=Path, required=True, help="path to modern OpenRS2 cache directory")
    parser.add_argument("--output", required=True, help="output .anims file path")
    parser.add_argument(
        "--include-spotanim-sequences",
        action="store_true",
        help="include every animation sequence referenced by spotanim definitions",
    )
    parser.add_argument(
        "--item-render-map",
        type=Path,
        help="include ready, walk, and run BAS sequences from a RuneC item_render.map",
    )
    parser.add_argument(
        "--combat-visuals",
        action="append",
        default=[],
        type=Path,
        help="include attack and projectile sequences from combat_visuals.tsv",
    )
    parser.add_argument(
        "--extra-sequences",
        default="",
        help="comma-separated extra animation sequence ids",
    )
    args = parser.parse_args()

    output_path = Path(args.output)
    cache_path = args.modern_cache

    print(f"reading modern cache from {cache_path}")
    reader = ModernCacheReader(cache_path)
    needed = set(NEEDED_ANIMATIONS)

    if args.include_spotanim_sequences:
        spotanim_ids = parse_spotanim_sequence_ids(reader)
        needed |= spotanim_ids
        print(f"including {len(spotanim_ids)} spotanim sequence IDs")

    if args.item_render_map:
        item_render_ids = read_item_render_sequence_ids(args.item_render_map)
        needed |= item_render_ids
        print(f"including {len(item_render_ids)} item render BAS sequence IDs")

    for combat_visuals in args.combat_visuals:
        combat_visual_ids = read_combat_visual_sequence_ids(combat_visuals)
        needed |= combat_visual_ids
        print(
            f"including {len(combat_visual_ids)} combat visual sequence IDs "
            f"from {combat_visuals}"
        )

    extra_ids = parse_extra_sequence_ids(args.extra_sequences)
    if extra_ids:
        needed |= extra_ids
        print(f"including {len(extra_ids)} explicit extra sequence IDs")

    # 1. load sequences
    print("loading sequences...")
    seq_files = reader.read_group(2, MODERN_SEQ_CONFIG_GROUP)
    sequences: dict[int, SequenceDef] = {}
    for seq_id, entry_data in seq_files.items():
        modern_seq = parse_modern_sequence(seq_id, entry_data)
        # convert modern SequenceDef to our local SequenceDef
        seq = SequenceDef(
            seq_id=modern_seq.seq_id,
            frame_count=modern_seq.frame_count,
            frame_delays=modern_seq.frame_delays,
            primary_frame_ids=modern_seq.primary_frame_ids,
            frame_step=modern_seq.frame_step,
            interleave_order=modern_seq.interleave_order,
            priority=modern_seq.forced_priority,
            loop_count=modern_seq.max_loops,
            walk_flag=modern_seq.priority,  # modern opcode 10 = priority (walk_flag equivalent)
            run_flag=modern_seq.precedence_animating,  # modern opcode 9
        )
        sequences[seq_id] = seq
    print(f"  loaded {len(sequences)} sequences")

    # filter to needed animations
    available = needed & set(sequences.keys())
    missing = needed - set(sequences.keys())
    if missing:
        print(f"  warning: {len(missing)} animations not found in cache: {sorted(missing)}")
    print(f"  {len(available)} needed animations available")

    # 2. collect needed frame group IDs from sequences
    needed_groups: set[int] = set()
    for seq_id in available:
        seq = sequences[seq_id]
        for fid in seq.primary_frame_ids:
            if fid != -1:
                needed_groups.add(fid >> 16)

    print(f"loading {len(needed_groups)} frame archives from cache...")

    # 3. load frame archives to discover needed framebases,
    #    then load framebases, then re-parse frames with framebases available

    # first pass: discover framebase IDs from frame data headers
    needed_base_ids: set[int] = set()
    raw_frame_data: dict[int, dict[int, bytes]] = {}
    for group_id in sorted(needed_groups):
        try:
            files = reader.read_group(MODERN_FRAME_INDEX, group_id)
        except (KeyError, FileNotFoundError):
            print(f"  warning: frame archive group {group_id} not found in index {MODERN_FRAME_INDEX}")
            continue
        raw_frame_data[group_id] = files
        # each frame file starts with u16 framebase_id
        for file_data in files.values():
            if len(file_data) >= 2:
                fb_id = (file_data[0] << 8) | file_data[1]
                needed_base_ids.add(fb_id)

    print(f"  discovered {len(needed_base_ids)} needed framebases")
    print("loading framebases from modern cache index 1...")
    framebases = load_modern_framebases(reader, needed_base_ids)
    print(f"  loaded {len(framebases)} framebases")

    # second pass: parse frames with framebases available
    all_frames: dict[int, dict[int, FrameDef]] = {}
    loaded = 0
    errors = 0
    for group_id, files in raw_frame_data.items():
        frames: dict[int, FrameDef] = {}
        for file_id, file_data in files.items():
            if len(file_data) < 3:
                continue
            frame = _parse_normal_frame(group_id, file_id, file_data, framebases)
            if frame is not None:
                frames[file_id] = frame
        if frames:
            all_frames[group_id] = frames
            loaded += 1

    print(f"  loaded {loaded} frame archives ({sum(len(v) for v in all_frames.values())} total frames), {errors} errors")

    # 4. write output
    write_animations_binary(output_path, framebases, all_frames, sequences, available)


if __name__ == "__main__":
    main()
