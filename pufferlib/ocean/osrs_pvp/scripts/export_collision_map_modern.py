"""Export collision data from modern OpenRS2 OSRS cache to .cmap binary format.

Reads modern cache (flat file format from OpenRS2), parses terrain and object
data for specified regions, and outputs collision flags compatible with
osrs_pvp_collision.h's collision_map_load().

Modern cache differences from 317:
  - Map data in index 5, groups identified by djb2 name hash
  - Object data (l_X_Y) is XTEA encrypted, keys from OpenRS2 archive
  - Object IDs in map data use read_big_smart (2 or 4 bytes) instead of read_smart
  - Object definitions in index 2 group 6, modern opcode set

Usage:
    uv run python scripts/export_collision_map_modern.py \
        --cache ../reference/osrs-cache-modern \
        --keys ../reference/osrs-cache-modern/keys.json \
        --output data/zulrah.cmap \
        --regions 35,48

    # export multiple regions
    uv run python scripts/export_collision_map_modern.py \
        --cache ../reference/osrs-cache-modern \
        --keys ../reference/osrs-cache-modern/keys.json \
        --output data/world.cmap \
        --regions 35,48 34,48 36,48

    # export wilderness regions
    uv run python scripts/export_collision_map_modern.py \
        --cache ../reference/osrs-cache-modern \
        --keys ../reference/osrs-cache-modern/keys.json \
        --output data/wilderness.cmap \
        --wilderness
"""

import argparse
import bz2
import io
import json
import struct
import sys
import zlib
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from export_collision_map import (
    BLOCKED,
    IMPENETRABLE_BLOCKED,
    WALL_EAST,
    WALL_NORTH,
    WALL_SOUTH,
    WALL_WEST,
    CollisionFlags,
    mark_occupant,
    mark_wall,
    new_collision_flags,
    parse_terrain,
    write_cmap,
)
from modern_cache_reader import (
    ModernCacheReader,
    read_big_smart,
    read_smart,
    read_string,
    read_u16,
    read_u8,
)

# object type IDs (same as 317)
OBJ_STRAIGHT_WALL = 0
OBJ_DIAGONAL_CORNER = 1
OBJ_ENTIRE_WALL = 2
OBJ_WALL_CORNER = 3
OBJ_DIAGONAL_WALL = 9
OBJ_GENERAL_PROP = 10
OBJ_WALKABLE_PROP = 11
OBJ_GROUND_PROP = 22

# directions
DIR_WEST = 0
DIR_NORTH = 1
DIR_EAST = 2
DIR_SOUTH = 3

# modern cache config group for object definitions
MODERN_OBJ_CONFIG_GROUP = 6


# --- djb2 name hash (OSRS modern cache uses this for map group names) ---


def djb2(name: str) -> int:
    """Compute djb2 hash for modern cache group name lookup.

    Returns signed 32-bit int matching Java's djb2 semantics.
    """
    h = 0
    for c in name.lower():
        h = (h * 31 + ord(c)) & 0xFFFFFFFF
    if h >= 0x80000000:
        h -= 0x100000000
    return h


# --- XTEA decryption ---


def xtea_decrypt(data: bytes, key: list[int]) -> bytes:
    """Decrypt XTEA-encrypted data using 4 int32 key.

    OSRS uses 32 rounds of XTEA in big-endian mode.
    Only processes complete 8-byte blocks; trailing bytes pass through.
    """
    delta = 0x9E3779B9
    result = bytearray()

    for i in range(len(data) // 8):
        v0, v1 = struct.unpack(">II", data[i * 8 : (i + 1) * 8])
        total = (delta * 32) & 0xFFFFFFFF

        # convert key to unsigned for arithmetic
        ukey = [k & 0xFFFFFFFF for k in key]

        for _ in range(32):
            v1 = (
                v1 - (((v0 << 4 ^ v0 >> 5) + v0) ^ (total + ukey[(total >> 11) & 3]))
            ) & 0xFFFFFFFF
            total = (total - delta) & 0xFFFFFFFF
            v0 = (
                v0 - (((v1 << 4 ^ v1 >> 5) + v1) ^ (total + ukey[total & 3]))
            ) & 0xFFFFFFFF

        result.extend(struct.pack(">II", v0, v1))

    # pass through any trailing bytes (< 8)
    result.extend(data[(len(data) // 8) * 8 :])
    return bytes(result)


# --- modern object definition decoder ---


@dataclass
class ModernObjDef:
    """Object definition from modern OSRS cache, with collision-relevant fields."""

    obj_id: int = 0
    width: int = 1
    length: int = 1
    solid: bool = True
    impenetrable: bool = True
    has_actions: bool = False
    actions: list[str | None] = field(default_factory=lambda: [None] * 5)


def _read_modern_obj_string(buf: io.BytesIO) -> str:
    """Read null-terminated string from object definition data."""
    chars = []
    while True:
        b = buf.read(1)
        if not b or b[0] == 0:
            break
        chars.append(chr(b[0]))
    return "".join(chars)


def decode_modern_obj_def(obj_id: int, data: bytes) -> ModernObjDef:
    """Parse a single modern object definition from opcode stream.

    Modern opcodes that affect collision: 14 (width), 15 (length),
    17 (not solid), 18 (not impenetrable), 19 (interactType/hasActions),
    30-34 (action strings), 74 (isHollow = not solid).

    All other opcodes are skipped but must be parsed correctly to avoid
    desynchronizing the stream.
    """
    d = ModernObjDef(obj_id=obj_id)
    buf = io.BytesIO(data)

    while True:
        raw = buf.read(1)
        if not raw:
            break
        opcode = raw[0]

        if opcode == 0:
            break
        elif opcode == 1:
            # models: u8 count, then (u16 model_id, u8 type) per entry
            count = read_u8(buf)
            for _ in range(count):
                read_u16(buf)  # model id
                read_u8(buf)  # model type
        elif opcode == 2:
            _read_modern_obj_string(buf)  # name
        elif opcode == 5:
            # models without types: u8 count, then u16 model_id per entry
            count = read_u8(buf)
            for _ in range(count):
                read_u16(buf)
        elif opcode == 14:
            d.width = read_u8(buf)
        elif opcode == 15:
            d.length = read_u8(buf)
        elif opcode == 17:
            d.solid = False
        elif opcode == 18:
            d.impenetrable = False
        elif opcode == 19:
            val = read_u8(buf)
            d.has_actions = val == 1
        elif opcode == 21:
            pass  # contouredGround = 0
        elif opcode == 22:
            pass  # nonFlatShading
        elif opcode == 23:
            pass  # modelClipped
        elif opcode == 24:
            read_u16(buf)  # animation id
        elif opcode == 27:
            pass  # clipType = 1
        elif opcode == 28:
            read_u8(buf)  # decorDisplacement
        elif opcode == 29:
            buf.read(1)  # ambient (signed byte)
        elif opcode in range(30, 35):
            action = _read_modern_obj_string(buf)
            d.actions[opcode - 30] = action if action != "hidden" else None
            if action and action != "hidden":
                d.has_actions = True
        elif opcode == 39:
            buf.read(1)  # contrast (signed byte)
        elif opcode == 40:
            # recolor: u8 count, then (u16 old, u16 new) per entry
            count = read_u8(buf)
            for _ in range(count):
                read_u16(buf)
                read_u16(buf)
        elif opcode == 41:
            # retexture: u8 count, then (u16 old, u16 new) per entry
            count = read_u8(buf)
            for _ in range(count):
                read_u16(buf)
                read_u16(buf)
        elif opcode == 60:
            read_u16(buf)  # mapAreaId
        elif opcode == 61:
            read_u16(buf)  # category
        elif opcode == 62:
            pass  # isRotated
        elif opcode == 64:
            pass  # shadow = false
        elif opcode == 65:
            read_u16(buf)  # modelSizeX
        elif opcode == 66:
            read_u16(buf)  # modelSizeH
        elif opcode == 67:
            read_u16(buf)  # modelSizeY
        elif opcode == 68:
            read_u16(buf)  # mapsceneID
        elif opcode == 69:
            read_u8(buf)  # surroundings
        elif opcode == 70:
            read_u16(buf)  # translateX (signed)
        elif opcode == 71:
            read_u16(buf)  # translateH (signed)
        elif opcode == 72:
            read_u16(buf)  # translateY (signed)
        elif opcode == 73:
            pass  # obstructsGround
        elif opcode == 74:
            d.solid = False  # isHollow
        elif opcode == 75:
            read_u8(buf)  # supportItems
        elif opcode == 77:
            # transforms: u16 varbit, u16 varp, u8 count, u16[count+1] ids
            read_u16(buf)  # varbitID
            read_u16(buf)  # varpID
            count = read_u8(buf)
            for _ in range(count + 1):
                read_u16(buf)
        elif opcode == 78:
            # bgsound: u16 soundId, u8 distance, u8 retain (rev220+)
            read_u16(buf)  # sound id
            read_u8(buf)  # distance
            read_u8(buf)  # retain
        elif opcode == 79:
            # randomsound: u16 tick1, u16 tick2, u8 distance, u8 retain, u8 count, u16[count] ids
            read_u16(buf)  # anInt2112
            read_u16(buf)  # anInt2113
            read_u8(buf)  # distance
            read_u8(buf)  # retain (rev220+)
            count = read_u8(buf)
            for _ in range(count):
                read_u16(buf)  # sound ids
        elif opcode == 81:
            # treeskew / contouredGround = value * 256
            read_u8(buf)
        elif opcode == 82:
            read_u16(buf)  # mapIconId
        elif opcode == 89:
            pass  # randomAnimStart
        elif opcode == 90:
            pass  # fixLocAnimAfterLocChange = true
        elif opcode == 91:
            read_u8(buf)  # bgsoundDropoffEasing
        elif opcode == 92:
            # transforms with default: u16 varbit, u16 varp, u16 default, u8 count, u16[count+1] ids
            read_u16(buf)  # varbitID
            read_u16(buf)  # varpID
            default_val = read_u16(buf)  # default transform
            count = read_u8(buf)
            for _ in range(count + 1):
                read_u16(buf)
        elif opcode == 93:
            # bgsoundFade: u8 curve1, u16 duration1, u8 curve2, u16 duration2
            read_u8(buf)
            read_u16(buf)
            read_u8(buf)
            read_u16(buf)
        elif opcode == 94:
            pass  # unknown94 = true
        elif opcode == 95:
            read_u8(buf)  # crossWorldSound
        elif opcode == 96:
            read_u8(buf)  # thickness/raise
        elif opcode == 249:
            # params map: u8 count, then (u8 is_string, u24 key, string|i32 value)
            count = read_u8(buf)
            for _ in range(count):
                is_string = read_u8(buf)
                # key is 3 bytes (u24)
                buf.read(3)
                if is_string == 1:
                    _read_modern_obj_string(buf)
                else:
                    buf.read(4)  # i32 value
        else:
            # unknown opcode — we can't safely skip, so stop parsing this def
            print(
                f"  warning: unknown obj opcode {opcode} at pos {buf.tell()} "
                f"for obj {obj_id}, stopping parse",
                file=sys.stderr,
            )
            break

    return d


def decode_modern_obj_defs(reader: ModernCacheReader) -> dict[int, ModernObjDef]:
    """Decode all object definitions from modern cache (index 2, group 6)."""
    files = reader.read_group(2, MODERN_OBJ_CONFIG_GROUP)
    defs: dict[int, ModernObjDef] = {}

    for obj_id, data in files.items():
        d = decode_modern_obj_def(obj_id, data)
        defs[obj_id] = d

    return defs


# --- modern map data parsing ---


def _read_extended_smart(buf: io.BytesIO) -> int:
    """Read extended smart: chains multiple read_smart calls for values > 32767.

    Modern OSRS map data uses this for object ID deltas to support IDs > 32767.
    If a smart value is exactly 32767, accumulate and read the next smart.
    """
    total = 0
    val = read_smart(buf)
    while val == 32767:
        total += 32767
        val = read_smart(buf)
    return total + val


def parse_objects_modern(
    data: bytes,
    flags: CollisionFlags,
    down_heights: set[tuple[int, int, int]],
    obj_defs: dict[int, ModernObjDef],
) -> int:
    """Parse modern-format object data and mark collision flags.

    Uses extended smart for object ID deltas (chains read_smart for IDs > 32767).
    Position deltas still use regular read_smart (max position < 16384).
    Returns count of collision-marked objects for diagnostics.
    """
    buf = io.BytesIO(data)
    obj_id = -1
    marked_count = 0

    while True:
        obj_id_offset = _read_extended_smart(buf)
        if obj_id_offset == 0:
            break

        obj_id += obj_id_offset
        obj_pos_info = 0

        while True:
            pos_offset = read_smart(buf)
            if pos_offset == 0:
                break
            obj_pos_info += pos_offset - 1

            raw_byte = buf.read(1)
            if not raw_byte:
                return marked_count
            obj_other_info = raw_byte[0]

            local_y = obj_pos_info & 0x3F
            local_x = (obj_pos_info >> 6) & 0x3F
            height = (obj_pos_info >> 12) & 0x3

            obj_type = obj_other_info >> 2
            direction = obj_other_info & 0x3

            # downHeights adjustment
            if (local_x, local_y, 1) in down_heights:
                height -= 1
                if height < 0:
                    continue
            elif (local_x, local_y, height) in down_heights:
                height -= 1

            if height < 0:
                continue

            d = obj_defs.get(obj_id)
            if d is None:
                continue

            if not d.solid:
                continue

            # swap width/length for N/S rotation
            if direction == DIR_NORTH or direction == DIR_SOUTH:
                size_x = d.length
                size_y = d.width
            else:
                size_x = d.width
                size_y = d.length

            if obj_type == OBJ_GROUND_PROP:
                if d.has_actions:
                    mark_occupant(flags, height, local_x, local_y, size_x, size_y, False)
                    marked_count += 1
            elif obj_type in (OBJ_GENERAL_PROP, OBJ_WALKABLE_PROP) or obj_type >= 12:
                mark_occupant(
                    flags, height, local_x, local_y, size_x, size_y, d.impenetrable
                )
                marked_count += 1
            elif obj_type == OBJ_DIAGONAL_WALL:
                mark_occupant(
                    flags, height, local_x, local_y, size_x, size_y, d.impenetrable
                )
                marked_count += 1
            elif 0 <= obj_type <= 3:
                mark_wall(
                    flags, direction, height, local_x, local_y, obj_type, d.impenetrable
                )
                marked_count += 1

    return marked_count


# --- map group lookup ---


def find_map_groups(
    reader: ModernCacheReader,
) -> dict[int, tuple[int | None, int | None]]:
    """Build mapping of mapsquare -> (terrain_group_id, object_group_id).

    Scans the index 5 manifest for groups whose djb2 name hashes match
    m{rx}_{ry} (terrain) or l{rx}_{ry} (objects) patterns.

    Returns dict mapping mapsquare (rx<<8|ry) to (terrain_gid, obj_gid).
    """
    manifest = reader.read_index_manifest(5)

    # build reverse lookup: name_hash -> group_id
    hash_to_gid: dict[int, int] = {}
    for gid in manifest.group_ids:
        nh = manifest.group_name_hashes.get(gid)
        if nh is not None:
            hash_to_gid[nh] = gid

    # try all plausible region coordinates
    result: dict[int, tuple[int | None, int | None]] = {}
    for rx in range(256):
        for ry in range(256):
            terrain_hash = djb2(f"m{rx}_{ry}")
            obj_hash = djb2(f"l{rx}_{ry}")

            terrain_gid = hash_to_gid.get(terrain_hash)
            obj_gid = hash_to_gid.get(obj_hash)

            if terrain_gid is not None or obj_gid is not None:
                mapsquare = (rx << 8) | ry
                result[mapsquare] = (terrain_gid, obj_gid)

    return result


def load_xtea_keys(keys_path: Path) -> dict[int, list[int]]:
    """Load XTEA keys from OpenRS2 JSON export.

    Returns dict mapping mapsquare -> [k0, k1, k2, k3].
    """
    with open(keys_path) as f:
        keys_data = json.load(f)

    keys: dict[int, list[int]] = {}
    for entry in keys_data:
        ms = entry["mapsquare"]
        keys[ms] = entry["key"]

    return keys


# --- main ---


def main() -> None:
    """Export collision maps from modern OSRS cache."""
    parser = argparse.ArgumentParser(
        description="export collision data from modern OpenRS2 OSRS cache"
    )
    parser.add_argument(
        "--cache",
        type=Path,
        default=Path("../reference/osrs-cache-modern"),
        help="path to modern cache directory",
    )
    parser.add_argument(
        "--keys",
        type=Path,
        default=Path("../reference/osrs-cache-modern/keys.json"),
        help="path to XTEA keys JSON from OpenRS2",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("data/zulrah.cmap"),
        help="output .cmap binary file",
    )
    parser.add_argument(
        "--regions",
        nargs="+",
        type=str,
        help="region coordinates as rx,ry pairs (e.g. 35,48 34,48)",
    )
    parser.add_argument(
        "--wilderness",
        action="store_true",
        help="export wilderness regions (rx=44-56, ry=48-62)",
    )
    parser.add_argument(
        "--all-regions",
        action="store_true",
        help="export all available regions",
    )
    parser.add_argument(
        "--ascii",
        action="store_true",
        help="print ASCII visualization of collision map",
    )
    args = parser.parse_args()

    if not args.cache.exists():
        sys.exit(f"cache directory not found: {args.cache}")
    if not args.keys.exists():
        sys.exit(f"XTEA keys file not found: {args.keys}")

    args.output.parent.mkdir(parents=True, exist_ok=True)

    print(f"reading modern cache from {args.cache}")
    reader = ModernCacheReader(args.cache)

    print("loading XTEA keys...")
    xtea_keys = load_xtea_keys(args.keys)
    print(f"  {len(xtea_keys)} region keys loaded")

    print("loading modern object definitions...")
    obj_defs = decode_modern_obj_defs(reader)
    print(f"  {len(obj_defs)} object definitions parsed")

    print("scanning index 5 for map groups...")
    map_groups = find_map_groups(reader)
    print(f"  {len(map_groups)} regions found in index 5")

    # determine which regions to export
    target_mapsquares: set[int] = set()

    if args.regions:
        for coord in args.regions:
            parts = coord.split(",")
            rx, ry = int(parts[0]), int(parts[1])
            ms = (rx << 8) | ry
            target_mapsquares.add(ms)
    elif args.wilderness:
        for rx in range(44, 57):
            for ry in range(48, 63):
                ms = (rx << 8) | ry
                if ms in map_groups:
                    target_mapsquares.add(ms)
    elif args.all_regions:
        target_mapsquares = set(map_groups.keys())
    else:
        # default: Zulrah region
        target_mapsquares.add((35 << 8) | 48)

    print(f"\nexporting {len(target_mapsquares)} regions...")

    output_regions: dict[int, CollisionFlags] = {}
    decoded = 0
    errors = 0
    total_obj_marked = 0

    for ms in sorted(target_mapsquares):
        rx = (ms >> 8) & 0xFF
        ry = ms & 0xFF

        if ms not in map_groups:
            print(f"  region ({rx},{ry}): not found in index 5")
            errors += 1
            continue

        terrain_gid, obj_gid = map_groups[ms]

        # parse terrain
        if terrain_gid is None:
            print(f"  region ({rx},{ry}): no terrain group")
            errors += 1
            continue

        terrain_data = reader.read_container(5, terrain_gid)
        if terrain_data is None:
            print(f"  region ({rx},{ry}): failed to read terrain")
            errors += 1
            continue

        flags, down_heights = parse_terrain(terrain_data)

        # parse objects (XTEA encrypted)
        obj_marked = 0
        if obj_gid is not None:
            key = xtea_keys.get(ms)
            if key is None:
                print(f"  region ({rx},{ry}): no XTEA key, skipping objects")
            else:
                raw_obj = reader._read_raw(5, obj_gid)
                if raw_obj is not None and len(raw_obj) >= 5:
                    # container: compression(1) + compressed_len(4) + encrypted_payload
                    # XTEA starts at byte 5 (decompressed_len is also encrypted)
                    compression = raw_obj[0]
                    compressed_len = struct.unpack(">I", raw_obj[1:5])[0]
                    decrypted = xtea_decrypt(raw_obj[5:], key)

                    if compression == 0:
                        obj_data = decrypted[:compressed_len]
                    else:
                        # decrypted[0:4] = decompressed_len, decrypted[4:] = compressed data
                        gzip_data = decrypted[4 : 4 + compressed_len]
                        if compression == 2:
                            # gzip — use raw inflate to avoid CRC issues from XTEA padding
                            obj_data = zlib.decompress(gzip_data[10:], -zlib.MAX_WBITS)
                        elif compression == 1:
                            # bzip2 — strip 'BZ' header
                            obj_data = bz2.decompress(b"BZh1" + gzip_data)

                    obj_marked = parse_objects_modern(
                        obj_data, flags, down_heights, obj_defs
                    )
                    total_obj_marked += obj_marked

        output_regions[ms] = flags
        decoded += 1

        # per-region stats
        blocked = sum(
            1
            for x in range(64)
            for y in range(64)
            if flags[0][x][y] & BLOCKED
        )
        walled = sum(
            1
            for x in range(64)
            for y in range(64)
            if flags[0][x][y] & (WALL_NORTH | WALL_SOUTH | WALL_EAST | WALL_WEST)
        )
        print(
            f"  region ({rx},{ry}): "
            f"{blocked} blocked, {walled} walled, {obj_marked} objects marked"
        )

    print(f"\ndecoded {decoded} regions, {errors} skipped")
    print(f"total objects marked for collision: {total_obj_marked}")

    # overall stats
    total_blocked = 0
    total_walled = 0
    for region_flags in output_regions.values():
        for x in range(64):
            for y in range(64):
                f = region_flags[0][x][y]
                if f & BLOCKED:
                    total_blocked += 1
                if f & (WALL_NORTH | WALL_SOUTH | WALL_EAST | WALL_WEST):
                    total_walled += 1

    print(f"height-0 totals: {total_blocked} blocked, {total_walled} walled")

    # ASCII visualization
    if args.ascii and len(output_regions) == 1:
        ms = next(iter(output_regions))
        rx = (ms >> 8) & 0xFF
        ry = ms & 0xFF
        region_flags = output_regions[ms]

        print(f"\n--- collision map for region ({rx},{ry}) height 0 ---")
        print("  legend: . = walkable, # = blocked (terrain), W = walled (object)")
        print("          B = blocked (object), X = blocked + walled")
        print()

        for local_y in range(63, -1, -1):
            row = []
            for local_x in range(64):
                f = region_flags[0][local_x][local_y]
                has_block = bool(f & BLOCKED)
                has_wall = bool(
                    f & (WALL_NORTH | WALL_SOUTH | WALL_EAST | WALL_WEST)
                )
                has_imp_block = bool(f & IMPENETRABLE_BLOCKED)

                if has_block and has_wall:
                    row.append("X")
                elif has_wall:
                    row.append("W")
                elif has_imp_block:
                    row.append("B")
                elif has_block:
                    row.append("#")
                else:
                    row.append(".")
            print("".join(row))

    write_cmap(args.output, output_regions)
    file_size = args.output.stat().st_size
    print(f"\nwrote {file_size:,} bytes to {args.output}")


if __name__ == "__main__":
    main()
