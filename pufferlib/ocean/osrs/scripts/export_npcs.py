"""Export NPC models at their spawn positions from a 317-format OSRS cache.

Reads NPC definitions from npc.dat/npc.idx in the config archive (model IDs,
size, recolors), spawn positions from npc_spawns.json, decodes 3D models from
cache index 1, merges multi-model NPCs, and outputs a binary .npcs file for
the raylib viewer.

NPCs use the same OBJ2 binary format as objects (with texture atlas support).

Usage:
    uv run python scripts/export_npcs.py \
        --cache ../reference/tarnish/game-server/data/cache \
        --output data/wilderness.npcs
"""

import argparse
import copy
import gzip
import io
import json
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from export_collision_map import (
    CONFIG_INDEX,
    CacheReader,
    _read_string,
    decode_archive,
    hash_archive_name,
)
from export_models import (
    MODEL_INDEX,
    ModelData,
    decode_model,
    expand_model,
    hsl15_to_rgb,
    load_texture_average_colors,
)
from export_objects import sample_height_bilinear
from export_terrain import RegionTerrain, build_heightmap, parse_terrain_full
from export_textures import (
    TextureAtlas,
    build_atlas,
    load_all_texture_sprites,
    write_atlas_binary,
)

# tarnish server data paths (relative to cache dir)
NPC_SPAWNS_PATH = Path("def/npc/npc_spawns.json")

# binary format constants
OBJ2_MAGIC = 0x4F424A32  # same format as objects v2


@dataclass
class NpcDef:
    """NPC definition decoded from cache npc.dat/npc.idx."""

    npc_id: int = 0
    name: str = ""
    model_ids: list[int] = field(default_factory=list)
    size: int = 1
    standing_anim: int = -1
    walk_anim: int = -1
    combat_level: int = 0
    width_scale: int = 128
    height_scale: int = 128
    recolor_src: list[int] = field(default_factory=list)
    recolor_dst: list[int] = field(default_factory=list)


@dataclass
class NpcSpawn:
    """NPC spawn position from npc_spawns.json."""

    npc_id: int = 0
    world_x: int = 0
    world_y: int = 0
    height: int = 0
    facing: str = "SOUTH"


def decode_npc_definitions(cache: CacheReader) -> dict[int, NpcDef]:
    """Decode NPC definitions from cache config archive (npc.dat/npc.idx)."""
    raw = cache.get(CONFIG_INDEX, 2)
    if raw is None:
        sys.exit("could not read config archive")

    archive = decode_archive(raw)

    npc_hash = hash_archive_name("npc.dat") & 0xFFFFFFFF
    idx_hash = hash_archive_name("npc.idx") & 0xFFFFFFFF

    npc_data = archive.get(npc_hash) or archive.get(hash_archive_name("npc.dat"))
    npc_idx = archive.get(idx_hash) or archive.get(hash_archive_name("npc.idx"))

    if npc_data is None or npc_idx is None:
        sys.exit("npc.dat/npc.idx not found in config archive")

    buf = io.BytesIO(npc_data)
    idx_buf = io.BytesIO(npc_idx)

    total = struct.unpack(">H", idx_buf.read(2))[0]
    defs: dict[int, NpcDef] = {}

    for npc_id in range(total):
        d = NpcDef(npc_id=npc_id)

        while True:
            opcode_byte = buf.read(1)
            if not opcode_byte:
                break
            opcode = opcode_byte[0]

            if opcode == 0:
                break
            elif opcode == 1:
                count = buf.read(1)[0]
                d.model_ids = [struct.unpack(">H", buf.read(2))[0] for _ in range(count)]
            elif opcode == 2:
                d.name = _read_string(buf)
            elif opcode == 3:
                _read_string(buf)  # description
            elif opcode == 12:
                d.size = struct.unpack(">b", buf.read(1))[0]
            elif opcode == 13:
                d.standing_anim = struct.unpack(">H", buf.read(2))[0]
            elif opcode == 14:
                d.walk_anim = struct.unpack(">H", buf.read(2))[0]
            elif opcode == 15:
                buf.read(2)  # unknown
            elif opcode == 16:
                buf.read(2)  # unknown
            elif opcode == 17:
                # walk + turn animations (4 ushorts)
                buf.read(8)
            elif opcode == 18:
                buf.read(2)  # unknown
            elif 30 <= opcode <= 34:
                _read_string(buf)  # actions[0..4]
            elif opcode == 40:
                count = buf.read(1)[0]
                for _ in range(count):
                    d.recolor_src.append(struct.unpack(">H", buf.read(2))[0])
                    d.recolor_dst.append(struct.unpack(">H", buf.read(2))[0])
            elif opcode == 41:
                count = buf.read(1)[0]
                for _ in range(count):
                    buf.read(4)  # retexture pairs
            elif opcode == 60:
                count = buf.read(1)[0]
                for _ in range(count):
                    buf.read(2)  # chathead model IDs
            elif 90 <= opcode <= 92:
                buf.read(2)  # unknown ushorts
            elif opcode == 93:
                pass  # drawMapDot = false
            elif opcode == 95:
                d.combat_level = struct.unpack(">H", buf.read(2))[0]
            elif opcode == 97:
                d.width_scale = struct.unpack(">H", buf.read(2))[0]
            elif opcode == 98:
                d.height_scale = struct.unpack(">H", buf.read(2))[0]
            elif opcode == 99:
                pass  # isVisible
            elif opcode == 100:
                buf.read(1)  # lightModifier
            elif opcode == 101:
                buf.read(1)  # shadowModifier * 5... actually just 1 byte in 317
            elif opcode == 102:
                # head icon sprite — read bitfield then per-set data
                bitfield = buf.read(1)[0]
                for bit in range(8):
                    if bitfield & (1 << bit):
                        # BigSmart2 for archive ID
                        peek = buf.read(1)[0]
                        if peek < 128:
                            pass  # 1-byte smart, already consumed
                        else:
                            buf.read(1)  # second byte of 2-byte smart
                        # sprite index (unsigned smart)
                        peek2 = buf.read(1)[0]
                        if peek2 < 128:
                            pass
                        else:
                            buf.read(1)
            elif opcode == 103:
                buf.read(2)  # rotation
            elif opcode == 106:
                # morph: varbit(2) + varp(2) + count(1) + children(count+1 * 2)
                buf.read(2)  # varbit
                buf.read(2)  # varp
                count = buf.read(1)[0]
                for _ in range(count + 1):
                    buf.read(2)
            elif opcode == 107:
                pass  # isInteractable
            elif opcode == 109:
                pass  # rotation flag
            elif opcode == 111:
                pass  # pet flag
            elif opcode == 114:
                buf.read(2)  # unknown
            elif opcode == 115:
                buf.read(8)  # 4x ushort
            elif opcode == 116:
                buf.read(2)  # unknown
            elif opcode == 117:
                buf.read(8)  # 4x ushort
            elif opcode == 118:
                # morph variant with default child
                buf.read(2)  # varbit
                buf.read(2)  # varp
                buf.read(2)  # default child
                count = buf.read(1)[0]
                for _ in range(count + 1):
                    buf.read(2)
            elif opcode == 249:
                count = buf.read(1)[0]
                for _ in range(count):
                    is_string = buf.read(1)[0]
                    buf.read(3)  # key
                    if is_string:
                        _read_string(buf)
                    else:
                        buf.read(4)
            else:
                print(f"  warning: unknown npc opcode {opcode} at npc {npc_id}", file=sys.stderr)
                break

        if d.model_ids:
            defs[npc_id] = d

    return defs


def load_npc_spawns(data_dir: Path) -> list[NpcSpawn]:
    """Load NPC spawn positions from npc_spawns.json."""
    spawns_path = data_dir / NPC_SPAWNS_PATH
    if not spawns_path.exists():
        sys.exit(f"npc_spawns.json not found at {spawns_path}")

    with open(spawns_path) as f:
        raw = json.load(f)

    spawns = []
    for entry in raw:
        spawns.append(NpcSpawn(
            npc_id=entry["id"],
            world_x=entry["position"]["x"],
            world_y=entry["position"]["y"],
            height=entry["position"]["height"],
            facing=entry.get("facing", "SOUTH"),
        ))
    return spawns


def merge_models(models: list[ModelData]) -> ModelData:
    """Merge multiple ModelData into a single composite model.

    Concatenates vertices, faces, and colors. Face indices are offset
    by the cumulative vertex count of preceding models.
    """
    if len(models) == 1:
        return models[0]

    merged = ModelData(model_id=models[0].model_id)
    vert_offset = 0

    for md in models:
        merged.vertices_x.extend(md.vertices_x)
        merged.vertices_y.extend(md.vertices_y)
        merged.vertices_z.extend(md.vertices_z)
        merged.face_a.extend(a + vert_offset for a in md.face_a)
        merged.face_b.extend(b + vert_offset for b in md.face_b)
        merged.face_c.extend(c + vert_offset for c in md.face_c)
        merged.face_colors.extend(md.face_colors)
        merged.face_textures.extend(md.face_textures)
        merged.vertex_count += md.vertex_count
        merged.face_count += md.face_count
        vert_offset += md.vertex_count

    return merged


def apply_recolors(md: ModelData, src: list[int], dst: list[int]) -> None:
    """Apply recolor pairs to a model's face colors (in-place)."""
    for i, color in enumerate(md.face_colors):
        for s, d in zip(src, dst):
            if color == s:
                md.face_colors[i] = d
                break


def apply_scale(md: ModelData, width_scale: int, height_scale: int) -> None:
    """Apply NPC width/height scale to model vertices (in-place).

    Default scale is 128 = 1.0x. Values >128 enlarge, <128 shrink.
    Width scale affects X and Z, height scale affects Y.
    """
    if width_scale == 128 and height_scale == 128:
        return

    ws = width_scale / 128.0
    hs = height_scale / 128.0

    for i in range(md.vertex_count):
        md.vertices_x[i] = int(md.vertices_x[i] * ws)
        md.vertices_y[i] = int(md.vertices_y[i] * hs)
        md.vertices_z[i] = int(md.vertices_z[i] * ws)


def facing_to_rotation(facing: str) -> int:
    """Convert facing direction string to OSRS rotation (0=W, 1=N, 2=E, 3=S)."""
    return {"WEST": 0, "NORTH": 1, "EAST": 2, "SOUTH": 3}.get(facing, 3)


def rotate_model_90(md: ModelData) -> None:
    """Rotate model 90 degrees clockwise around Y axis (in-place).

    Maps (x, z) -> (z, -x). One 90-degree CW step.
    """
    for i in range(md.vertex_count):
        x = md.vertices_x[i]
        z = md.vertices_z[i]
        md.vertices_x[i] = z
        md.vertices_z[i] = -x


def main() -> None:
    parser = argparse.ArgumentParser(description="export OSRS NPC models from 317 cache")
    parser.add_argument("--cache", type=Path, default=Path("../reference/tarnish/game-server/data/cache"))
    parser.add_argument("--output", type=Path, default=Path("data/wilderness.npcs"))
    parser.add_argument("--radius", type=int, default=3, help="region radius around fight area center")
    args = parser.parse_args()

    if not args.cache.exists():
        sys.exit(f"cache directory not found: {args.cache}")

    # data dir is parent of cache dir (game-server/data/)
    data_dir = args.cache.parent

    print(f"reading cache from {args.cache}")
    cache = CacheReader(args.cache)

    print("decoding NPC definitions...")
    npc_defs = decode_npc_definitions(cache)
    print(f"  {len(npc_defs)} NPCs with models")

    print("loading NPC spawns...")
    all_spawns = load_npc_spawns(data_dir)
    print(f"  {len(all_spawns)} total spawns")

    # filter to our export area (same region logic as objects)
    center_rx, center_ry = 47, 55
    r = args.radius
    min_wx = (center_rx - r) * 64
    max_wx = (center_rx + r + 1) * 64
    min_wy = (center_ry - r) * 64
    max_wy = (center_ry + r + 1) * 64

    spawns = [
        s for s in all_spawns
        if min_wx <= s.world_x < max_wx and min_wy <= s.world_y < max_wy
        and s.height == 0  # plane 0 only for now
    ]
    print(f"  {len(spawns)} spawns in export area")

    # load map index for terrain heightmap
    from export_collision_map import MAP_INDEX, load_map_index
    region_defs = load_map_index(cache)
    target_regions = {
        (rd.region_x, rd.region_y): rd
        for rd in region_defs
        if center_rx - r <= rd.region_x <= center_rx + r
        and center_ry - r <= rd.region_y <= center_ry + r
    }

    # parse terrain for heightmap
    print("parsing terrain for ground heights...")
    terrain_parsed: dict[tuple[int, int], RegionTerrain] = {}
    for (rx, ry), rd in sorted(target_regions.items()):
        terrain_data = cache.get(MAP_INDEX, rd.terrain_file)
        if terrain_data is None:
            continue
        try:
            terrain_data = gzip.decompress(terrain_data)
        except Exception:
            continue
        rt = parse_terrain_full(terrain_data, rx * 64, ry * 64)
        rt.region_x = rx
        rt.region_y = ry
        terrain_parsed[(rx, ry)] = rt

    heightmap = build_heightmap(terrain_parsed) if terrain_parsed else None

    # load texture data for atlas
    tex_colors = load_texture_average_colors(cache)
    sprites = load_all_texture_sprites(cache)
    atlas = build_atlas(sprites) if sprites else None

    if atlas:
        atlas_path = args.output.with_suffix(".atlas")
        write_atlas_binary(atlas_path, atlas)
        print(f"  atlas: {atlas.width}x{atlas.height}")

    # decode and place NPC models
    print("decoding NPC models...")
    model_cache: dict[int, ModelData] = {}
    all_verts: list[float] = []
    all_colors: list[int] = []
    all_uvs: list[float] = []
    npc_count = 0
    missing_def = 0
    missing_model = 0

    model_scale = 1.0 / 128.0  # OSRS model units to tile units

    for spawn in spawns:
        npc_def = npc_defs.get(spawn.npc_id)
        if npc_def is None:
            missing_def += 1
            continue

        if not npc_def.model_ids:
            missing_def += 1
            continue

        # decode all sub-models for this NPC
        sub_models: list[ModelData] = []
        all_found = True
        for mid in npc_def.model_ids:
            if mid not in model_cache:
                raw = cache.get(MODEL_INDEX, mid)
                if raw is None:
                    all_found = False
                    break
                try:
                    raw = gzip.decompress(raw)
                except Exception:
                    all_found = False
                    break
                md = decode_model(mid, raw)
                if md is None:
                    all_found = False
                    break
                model_cache[mid] = md
            sub_models.append(copy.deepcopy(model_cache[mid]))

        if not all_found or not sub_models:
            missing_model += 1
            continue

        # merge multi-model NPC into single model
        merged = merge_models(sub_models)

        # apply recolors
        if npc_def.recolor_src:
            apply_recolors(merged, npc_def.recolor_src, npc_def.recolor_dst)

        # apply NPC scale
        apply_scale(merged, npc_def.width_scale, npc_def.height_scale)

        # apply facing rotation
        rotation = facing_to_rotation(spawn.facing)
        for _ in range(rotation):
            rotate_model_90(merged)

        # expand to per-vertex arrays (same as object exporter)
        verts, colors, face_uvs = expand_model(merged, tex_colors, atlas)

        # position in world space
        # NPC center: tile + 0.5 (or + size/2 for larger NPCs)
        npc_size = max(1, npc_def.size)
        center_off = npc_size / 2.0
        wx = float(spawn.world_x) + center_off
        wz = -(float(spawn.world_y) + center_off)  # negate for our coord system

        # sample height at NPC center (bilinear for smooth terrain like GE bowl)
        center_world_x = float(spawn.world_x) + center_off
        center_world_y = float(spawn.world_y) + center_off
        ground_y = sample_height_bilinear(heightmap, center_world_x, center_world_y) if heightmap else 0.0

        # winding fix: swap vertex 0 and 2 per triangle (same as objects)
        for i in range(0, len(verts), 9):
            for c in range(3):
                verts[i + c], verts[i + 6 + c] = verts[i + 6 + c], verts[i + c]
            colors[i // 3], colors[i // 3 + 2] = colors[i // 3 + 2], colors[i // 3]
            uv_base = (i // 3) * 2
            face_uvs[uv_base], face_uvs[uv_base + 4] = face_uvs[uv_base + 4], face_uvs[uv_base]
            face_uvs[uv_base + 1], face_uvs[uv_base + 5] = face_uvs[uv_base + 5], face_uvs[uv_base + 1]

        # transform to world coordinates
        for i in range(0, len(verts), 3):
            verts[i] = verts[i] * model_scale + wx
            verts[i + 1] = verts[i + 1] * model_scale + ground_y
            verts[i + 2] = -verts[i + 2] * model_scale + wz

        all_verts.extend(verts)
        # flatten color tuples to flat RGBA bytes
        for rgba in colors:
            all_colors.extend(rgba)
        all_uvs.extend(face_uvs)
        npc_count += 1

    total_verts = len(all_verts) // 3
    total_tris = total_verts // 3
    print(f"  {npc_count} NPCs placed, {missing_def} missing defs, {missing_model} missing models")
    print(f"  {total_verts:,} vertices, {total_tris:,} triangles")
    print(f"  {len(model_cache)} unique models decoded")

    # write binary (same OBJ2 format as objects)
    args.output.parent.mkdir(parents=True, exist_ok=True)

    min_wx_out = min((s.world_x for s in spawns), default=0)
    min_wy_out = min((s.world_y for s in spawns), default=0)
    has_textures = atlas is not None

    with open(args.output, "wb") as f:
        f.write(struct.pack("<I", OBJ2_MAGIC if has_textures else 0x4F424A53))
        f.write(struct.pack("<I", npc_count))
        f.write(struct.pack("<i", min_wx_out))
        f.write(struct.pack("<i", min_wy_out))
        f.write(struct.pack("<I", total_verts))

        # vertices
        for v in all_verts:
            f.write(struct.pack("<f", v))

        # colors (RGBA)
        for c in all_colors:
            f.write(struct.pack("B", c))

        # texcoords (if atlas)
        if has_textures:
            for uv in all_uvs:
                f.write(struct.pack("<f", uv))

    file_size = args.output.stat().st_size
    print(f"\nwrote {file_size:,} bytes to {args.output}")


if __name__ == "__main__":
    main()
