"""Export placed map objects (walls, props, trees) from OSRS cache.

Supports both 317-format (tarnish) and modern OpenRS2 flat file caches.
Reads object placements from each region's object file, resolves their 3D models
from object definitions, decodes model geometry, applies rotation and
positioning, and outputs a binary .objects file for the raylib viewer.

Object types exported:
  - Walls (types 0-3, 9): straight walls, corners, diagonal walls
  - Wall decorations (types 4-8): windows, torches, banners on walls
  - Props (types 10-11): buildings, trees, statues, interactive objects
  - Roofing (types 12-21): roof pieces
  - Ground decorations (type 22): flowers, grass patches, paving, dirt marks

Usage (317 cache):
    uv run python scripts/export_objects.py \
        --cache ../reference/tarnish/game-server/data/cache \
        --output data/wilderness.objects

Usage (modern cache):
    uv run python scripts/export_objects.py \
        --modern-cache ../reference/osrs-cache-modern \
        --keys ../reference/osrs-cache-modern/keys.json \
        --regions "35,47 35,48" \
        --output data/zulrah.objects
"""

import argparse
import gzip
import io
import math
import struct
import sys
from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from export_collision_map import (
    CONFIG_INDEX,
    MAP_INDEX,
    CacheReader,
    _read_string,
    decode_archive,
    hash_archive_name,
    load_map_index,
    read_smart,
)
from export_terrain import RegionTerrain, build_heightmap, parse_terrain_full
from export_models import (
    MODEL_INDEX,
    ModelData,
    decode_model,
    expand_model,
    hsl15_to_rgb,
    load_model_modern,
    load_texture_average_colors,
)
from export_textures import (
    TextureAtlas,
    build_atlas,
    load_all_texture_sprites,
    write_atlas_binary,
)
from export_collision_map_modern import (
    _read_extended_smart,
    find_map_groups,
    load_xtea_keys,
)
from export_terrain import stitch_region_edges
from modern_cache_reader import ModernCacheReader

# --- object definition with model IDs ---

CONFIG_ARCHIVE = 2

# object types we want to render
WALL_TYPES = {0, 1, 2, 3, 9}
WALL_DECO_TYPES = {4, 5, 6, 7, 8}
PROP_TYPES = {10, 11}
ROOF_TYPES = {12, 13, 14, 15, 16, 17, 18, 19, 20, 21}
GROUND_DECO_TYPES = {22}
EXPORTED_TYPES = WALL_TYPES | WALL_DECO_TYPES | PROP_TYPES | ROOF_TYPES | GROUND_DECO_TYPES


@dataclass
class LocDef:
    """Object definition with model IDs and transform properties."""

    obj_id: int = 0
    name: str = ""
    width: int = 1
    length: int = 1
    solid: bool = True
    # model IDs and their associated type contexts
    model_ids: list[int] = field(default_factory=list)
    model_types: list[int] = field(default_factory=list)
    has_typed_models: bool = False  # True if opcode 1 was used (typed models)
    # transform
    model_size_x: int = 128  # scale factor (128 = 1.0x)
    model_size_h: int = 128
    model_size_y: int = 128
    offset_x: int = 0
    offset_h: int = 0
    offset_y: int = 0
    # rendering flags
    rotated: bool = False  # mirror model horizontally
    contoured_ground: bool = False
    decor_offset: int = 16  # wall decoration displacement (type 5), default 16 OSRS units
    # color remapping
    recolor_from: list[int] = field(default_factory=list)
    recolor_to: list[int] = field(default_factory=list)


def decode_loc_definitions(cache: CacheReader) -> dict[int, LocDef]:
    """Decode object definitions from loc.dat/loc.idx, capturing model IDs.

    Uses the idx file to build an offset table for each definition (matching
    the Java client's streamIndices approach). Each definition is read from
    its own slice of loc.dat, preventing any opcode parsing bug from
    corrupting subsequent definitions.
    """
    raw = cache.get(CONFIG_INDEX, CONFIG_ARCHIVE)
    if raw is None:
        sys.exit("could not read config archive")

    archive = decode_archive(raw)
    loc_hash = hash_archive_name("loc.dat") & 0xFFFFFFFF
    idx_hash = hash_archive_name("loc.idx") & 0xFFFFFFFF

    loc_data = archive.get(loc_hash) or archive.get(hash_archive_name("loc.dat"))
    loc_idx = archive.get(idx_hash) or archive.get(hash_archive_name("loc.idx"))

    if loc_data is None or loc_idx is None:
        sys.exit("loc.dat/loc.idx not found in config archive")

    idx_buf = io.BytesIO(loc_idx)
    total = struct.unpack(">H", idx_buf.read(2))[0]

    # build offset table from idx (each entry is uint16 size)
    offsets: list[int] = []
    pos = 0
    for _ in range(total):
        size_bytes = idx_buf.read(2)
        if len(size_bytes) < 2:
            break
        size = struct.unpack(">H", size_bytes)[0]
        if size == 0xFFFF:
            break
        offsets.append(pos)
        pos += size

    defs: dict[int, LocDef] = {}

    for obj_id in range(len(offsets)):
        # read this definition's slice from loc.dat
        start = offsets[obj_id]
        end = offsets[obj_id + 1] if obj_id + 1 < len(offsets) else len(loc_data)
        buf = io.BytesIO(loc_data[start:end])

        d = LocDef(obj_id=obj_id)

        while True:
            opcode = buf.read(1)
            if not opcode:
                break
            opcode = opcode[0]

            if opcode == 0:
                break
            elif opcode == 1:
                # model IDs with type contexts (first opcode wins, skip if already set)
                count = buf.read(1)[0]
                if not d.model_ids:
                    d.has_typed_models = True
                    for _ in range(count):
                        mid = struct.unpack(">H", buf.read(2))[0]
                        mtype = buf.read(1)[0]
                        d.model_ids.append(mid)
                        d.model_types.append(mtype)
                else:
                    buf.read(count * 3)  # skip
            elif opcode == 2:
                d.name = _read_string(buf)
            elif opcode == 3:
                pass  # description: Java reads 0 bytes for this opcode
            elif opcode == 5:
                # model IDs without types (first opcode wins, skip if already set)
                count = buf.read(1)[0]
                if not d.model_ids:
                    d.has_typed_models = False
                    for _ in range(count):
                        mid = struct.unpack(">H", buf.read(2))[0]
                        d.model_ids.append(mid)
                        d.model_types.append(10)
                else:
                    buf.read(count * 2)  # skip
            elif opcode == 14:
                d.width = buf.read(1)[0]
            elif opcode == 15:
                d.length = buf.read(1)[0]
            elif opcode == 17:
                d.solid = False
            elif opcode == 18:
                pass  # impenetrable
            elif opcode == 19:
                buf.read(1)  # hasActions
            elif opcode == 21:
                d.contoured_ground = True
            elif opcode == 22:
                pass  # nonFlatShading
            elif opcode == 23:
                pass  # modelClipped
            elif opcode == 24:
                buf.read(2)  # animation id
            elif opcode == 28:
                d.decor_offset = buf.read(1)[0]
            elif opcode == 29:
                buf.read(1)  # ambient
            elif opcode == 39:
                buf.read(1)  # contrast
            elif 30 <= opcode <= 34:
                _read_string(buf)  # actions
            elif opcode == 40:
                # recolors: first short = modifiedModelColors (color to FIND in model)
                # second short = originalModelColors (color to REPLACE WITH)
                # naming is backwards in the OSRS client — "modified" is the source,
                # "original" is the replacement. confirmed via Model.recolor(found, replace)
                # call: model.recolor(modifiedModelColors[i], originalModelColors[i])
                count = buf.read(1)[0]
                for _ in range(count):
                    d.recolor_from.append(struct.unpack(">H", buf.read(2))[0])
                    d.recolor_to.append(struct.unpack(">H", buf.read(2))[0])
            elif opcode == 41:
                count = buf.read(1)[0]
                for _ in range(count):
                    buf.read(2)  # texture from
                    buf.read(2)  # texture to
            elif opcode == 60:
                buf.read(2)  # mapAreaId
            elif opcode == 61:
                buf.read(2)  # category
            elif opcode == 62:
                d.rotated = True
            elif opcode == 64:
                pass  # shadow=false
            elif opcode == 65:
                d.model_size_x = struct.unpack(">H", buf.read(2))[0]
            elif opcode == 66:
                d.model_size_h = struct.unpack(">H", buf.read(2))[0]
            elif opcode == 67:
                d.model_size_y = struct.unpack(">H", buf.read(2))[0]
            elif opcode == 68:
                buf.read(2)  # mapscene
            elif opcode == 69:
                buf.read(1)  # surroundings
            elif opcode == 70:
                d.offset_x = struct.unpack(">H", buf.read(2))[0]
            elif opcode == 71:
                d.offset_h = struct.unpack(">H", buf.read(2))[0]
            elif opcode == 72:
                d.offset_y = struct.unpack(">H", buf.read(2))[0]
            elif opcode == 73:
                pass  # obstructsGround
            elif opcode == 74:
                d.solid = False
            elif opcode == 75:
                buf.read(1)  # supportItems
            elif opcode == 77:
                buf.read(2)  # varbit
                buf.read(2)  # varp
                count = buf.read(1)[0]
                for _ in range(count + 1):
                    buf.read(2)
            elif opcode == 78:
                buf.read(2)  # ambient sound
                buf.read(1)
            elif opcode == 79:
                buf.read(2)
                buf.read(2)
                buf.read(1)
                count = buf.read(1)[0]
                for _ in range(count):
                    buf.read(2)
            elif opcode == 81:
                buf.read(1)  # contoured ground percent
            elif opcode == 82:
                buf.read(2)  # map icon
            elif opcode == 89:
                pass  # randomize animation
            elif opcode == 92:
                buf.read(2)  # varbit
                buf.read(2)  # varp
                buf.read(2)  # default
                count = buf.read(1)[0]
                for _ in range(count + 1):
                    buf.read(2)
            elif opcode == 249:
                count = buf.read(1)[0]
                for _ in range(count):
                    is_string = buf.read(1)[0] == 1
                    buf.read(3)  # 3-byte medium
                    if is_string:
                        _read_string(buf)
                    else:
                        buf.read(4)
            # unknown opcodes: skip (read 0 bytes), matching Java behavior

        if d.model_ids:
            defs[obj_id] = d

    return defs


def _read_modern_obj_string(buf: io.BytesIO) -> str:
    """Read null-terminated string from modern object definition."""
    chars = []
    while True:
        b = buf.read(1)
        if not b or b[0] == 0:
            break
        chars.append(chr(b[0]))
    return "".join(chars)


def decode_loc_definitions_modern(reader: ModernCacheReader) -> dict[int, LocDef]:
    """Decode object definitions from modern cache, capturing model IDs and transforms.

    Group 6 in config index (2) contains object definitions. Each definition is
    stored as a separate file within the group, keyed by object ID.
    """
    files = reader.read_group(2, 6)
    if files is None:
        sys.exit("could not read object definitions from modern cache")

    defs: dict[int, LocDef] = {}

    for obj_id, data in files.items():
        d = LocDef(obj_id=obj_id)
        buf = io.BytesIO(data)

        while True:
            raw = buf.read(1)
            if not raw:
                break
            opcode = raw[0]

            if opcode == 0:
                break
            elif opcode == 1:
                count = buf.read(1)[0]
                d.has_typed_models = True
                for _ in range(count):
                    mid = struct.unpack(">H", buf.read(2))[0]
                    mtype = buf.read(1)[0]
                    d.model_ids.append(mid)
                    d.model_types.append(mtype)
            elif opcode == 2:
                d.name = _read_modern_obj_string(buf)
            elif opcode == 5:
                count = buf.read(1)[0]
                d.has_typed_models = False
                for _ in range(count):
                    mid = struct.unpack(">H", buf.read(2))[0]
                    d.model_ids.append(mid)
                    d.model_types.append(10)
            elif opcode == 14:
                d.width = buf.read(1)[0]
            elif opcode == 15:
                d.length = buf.read(1)[0]
            elif opcode == 17:
                d.solid = False
            elif opcode == 18:
                pass  # impenetrable
            elif opcode == 19:
                buf.read(1)  # interactType
            elif opcode == 21:
                d.contoured_ground = True
            elif opcode == 22:
                pass  # nonFlatShading
            elif opcode == 23:
                pass  # modelClipped
            elif opcode == 24:
                struct.unpack(">H", buf.read(2))  # animation id
            elif opcode == 27:
                pass  # clipType = 1
            elif opcode == 28:
                d.decor_offset = buf.read(1)[0]
            elif opcode == 29:
                buf.read(1)  # ambient
            elif opcode in range(30, 35):
                _read_modern_obj_string(buf)  # actions
            elif opcode == 39:
                buf.read(1)  # contrast
            elif opcode == 40:
                count = buf.read(1)[0]
                for _ in range(count):
                    d.recolor_from.append(struct.unpack(">H", buf.read(2))[0])
                    d.recolor_to.append(struct.unpack(">H", buf.read(2))[0])
            elif opcode == 41:
                count = buf.read(1)[0]
                for _ in range(count):
                    buf.read(2)  # texture from
                    buf.read(2)  # texture to
            elif opcode == 60:
                buf.read(2)  # mapAreaId
            elif opcode == 61:
                buf.read(2)  # category
            elif opcode == 62:
                d.rotated = True
            elif opcode == 64:
                pass  # shadow=false
            elif opcode == 65:
                d.model_size_x = struct.unpack(">H", buf.read(2))[0]
            elif opcode == 66:
                d.model_size_h = struct.unpack(">H", buf.read(2))[0]
            elif opcode == 67:
                d.model_size_y = struct.unpack(">H", buf.read(2))[0]
            elif opcode == 68:
                buf.read(2)  # mapscene
            elif opcode == 69:
                buf.read(1)  # surroundings
            elif opcode == 70:
                d.offset_x = struct.unpack(">h", buf.read(2))[0]
            elif opcode == 71:
                d.offset_h = struct.unpack(">h", buf.read(2))[0]
            elif opcode == 72:
                d.offset_y = struct.unpack(">h", buf.read(2))[0]
            elif opcode == 73:
                pass  # obstructsGround
            elif opcode == 74:
                d.solid = False
            elif opcode == 75:
                buf.read(1)  # supportItems
            elif opcode == 77:
                buf.read(2)  # varbit
                buf.read(2)  # varp
                count = buf.read(1)[0]
                for _ in range(count + 1):
                    buf.read(2)
            elif opcode == 78:
                buf.read(2)  # ambient sound
                buf.read(1)  # distance
                buf.read(1)  # retain
            elif opcode == 79:
                buf.read(2)
                buf.read(2)
                buf.read(1)  # distance
                buf.read(1)  # retain
                count = buf.read(1)[0]
                for _ in range(count):
                    buf.read(2)
            elif opcode == 81:
                buf.read(1)  # contoured ground percent
            elif opcode == 82:
                buf.read(2)  # map icon
            elif opcode == 89:
                pass  # randomize animation
            elif opcode == 90:
                pass  # fixLocAnimAfterLocChange
            elif opcode == 91:
                buf.read(1)  # bgsoundDropoffEasing
            elif opcode == 92:
                buf.read(2)  # varbit
                buf.read(2)  # varp
                buf.read(2)  # default
                count = buf.read(1)[0]
                for _ in range(count + 1):
                    buf.read(2)
            elif opcode == 93:
                buf.read(1)
                buf.read(2)
                buf.read(1)
                buf.read(2)
            elif opcode == 94:
                pass  # unknown94
            elif opcode == 95:
                buf.read(1)  # crossWorldSound
            elif opcode == 96:
                buf.read(1)  # thickness/raise
            elif opcode == 249:
                count = buf.read(1)[0]
                for _ in range(count):
                    is_string = buf.read(1)[0] == 1
                    buf.read(3)  # 3-byte key
                    if is_string:
                        _read_modern_obj_string(buf)
                    else:
                        buf.read(4)
            else:
                # unknown opcode — stop parsing to avoid desync
                break

        if d.model_ids:
            defs[obj_id] = d

    print(f"  {len(defs)} definitions with models (from {len(files)} total)")
    return defs



@dataclass
class PlacedObject:
    """A single placed object in the world."""

    obj_id: int = 0
    world_x: int = 0
    world_y: int = 0
    height: int = 0
    obj_type: int = 0  # 0-22
    rotation: int = 0  # 0-3 (W/N/E/S = 0/90/180/270 degrees)


def parse_object_placements(
    data: bytes,
    base_x: int,
    base_y: int,
) -> list[PlacedObject]:
    """Parse object placement binary for one region."""
    buf = io.BytesIO(data)
    obj_id = -1
    placements: list[PlacedObject] = []

    while True:
        obj_id_offset = read_smart(buf)
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
                return placements
            info = raw_byte[0]

            local_y = obj_pos_info & 0x3F
            local_x = (obj_pos_info >> 6) & 0x3F
            height = (obj_pos_info >> 12) & 0x3

            obj_type = info >> 2
            rotation = info & 0x3

            if obj_type not in EXPORTED_TYPES:
                continue

            # only export plane 0 for now — plane 1+ objects (upper floors, roofing)
            # need corresponding floor/ceiling geometry to look right.
            # the heightmaps dict already supports multi-plane; just widen this
            # filter when floor rendering is added.
            if height != 0:
                continue

            placements.append(
                PlacedObject(
                    obj_id=obj_id,
                    world_x=base_x + local_x,
                    world_y=base_y + local_y,
                    height=height,
                    obj_type=obj_type,
                    rotation=rotation,
                )
            )

    return placements


def parse_object_placements_modern(
    data: bytes,
    base_x: int,
    base_y: int,
) -> list[PlacedObject]:
    """Parse modern-format object placement data for one region.

    Uses extended smart for object ID deltas (IDs > 32767).
    """
    buf = io.BytesIO(data)
    obj_id = -1
    placements: list[PlacedObject] = []

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
                return placements
            info = raw_byte[0]

            local_y = obj_pos_info & 0x3F
            local_x = (obj_pos_info >> 6) & 0x3F
            height = (obj_pos_info >> 12) & 0x3

            obj_type = info >> 2
            rotation = info & 0x3

            if obj_type not in EXPORTED_TYPES:
                continue
            if height != 0:
                continue

            placements.append(
                PlacedObject(
                    obj_id=obj_id,
                    world_x=base_x + local_x,
                    world_y=base_y + local_y,
                    height=height,
                    obj_type=obj_type,
                    rotation=rotation,
                )
            )

    return placements


# --- model transform helpers ---


def rotate_model_90(model: ModelData) -> None:
    """Rotate model 90 degrees clockwise (OSRS rotation direction).

    In OSRS coordinate space: new_x = z, new_z = -x (clockwise when viewed from above).
    """
    for i in range(model.vertex_count):
        old_x = model.vertices_x[i]
        old_z = model.vertices_z[i]
        model.vertices_x[i] = old_z
        model.vertices_z[i] = -old_x


def mirror_model(model: ModelData) -> None:
    """Mirror model on Z axis and swap face winding. Matches OSRS Model.mirror()."""
    for i in range(model.vertex_count):
        model.vertices_z[i] = -model.vertices_z[i]
    for i in range(model.face_count):
        model.face_a[i], model.face_c[i] = model.face_c[i], model.face_a[i]


def apply_recolors(model: ModelData, loc: LocDef) -> None:
    """Apply color remapping from object definition to model face colors."""
    if not loc.recolor_from:
        return
    remap = dict(zip(loc.recolor_from, loc.recolor_to))
    for i in range(model.face_count):
        if i < len(model.face_colors) and model.face_colors[i] in remap:
            model.face_colors[i] = remap[model.face_colors[i]]


def scale_model(model: ModelData, size_x: int, size_h: int, size_y: int) -> None:
    """Scale model vertices (128 = 1.0x).

    Matches the OSRS client call: model.scale(modelSizeX, modelSizeY, modelHeight)
    where Model.scale(x, z, y) — second param is Z, third is Y.
    So: size_x→X, size_y→Z(depth), size_h→Y(height).
    """
    if size_x == 128 and size_h == 128 and size_y == 128:
        return
    for i in range(model.vertex_count):
        model.vertices_x[i] = model.vertices_x[i] * size_x // 128
        model.vertices_y[i] = model.vertices_y[i] * size_h // 128
        model.vertices_z[i] = model.vertices_z[i] * size_y // 128


def placement_type_to_model_type(obj_type: int) -> int:
    """Map placement type to the model type requested from the definition.

    Matches the OSRS client's addLocation → getModelSharelight calls:
    - Types 4-8 (wall decorations) all request model type 4
    - Type 11 requests model type 10 (same model, different scene height)
    - All other types request their own type number
    """
    if 4 <= obj_type <= 8:
        return 4
    if obj_type == 11:
        return 10
    return obj_type


def get_model_for_type(loc: LocDef, obj_type: int) -> int | None:
    """Get the model ID for a given placement type from the object definition.

    Matches the OSRS client's ObjectDefinition.model() logic:
    - If modelTypes is null (opcode 5 only), model is for type 10 (props) only.
    - If modelTypes exists (opcode 1), requires exact type match. Returns None otherwise.
    """
    if not loc.model_ids:
        return None

    model_type = placement_type_to_model_type(obj_type)

    if not loc.has_typed_models:
        # opcode 5: untyped models, only valid for model type 10 (props)
        if model_type != 10:
            return None
        return loc.model_ids[0]

    # opcode 1: typed models, require exact match
    for mid, mtype in zip(loc.model_ids, loc.model_types):
        if mtype == model_type:
            return mid

    return None


# --- binary output format ---

OBJS_MAGIC = 0x4F424A53  # "OBJS"


@dataclass
class ExpandedPlacement:
    """A placed object with its expanded vertex data ready for output."""

    world_x: int = 0
    world_y: int = 0
    obj_type: int = 0
    vertex_count: int = 0
    face_count: int = 0
    vertices: list[float] = field(default_factory=list)  # flat x,y,z
    colors: list[tuple[int, int, int, int]] = field(default_factory=list)
    uvs: list[float] = field(default_factory=list)  # flat u,v per vertex


OBJ2_MAGIC = 0x4F424A32  # "OBJ2" — v2 format with texture coordinates


def write_objects_binary(
    output_path: Path,
    placements: list[ExpandedPlacement],
    min_world_x: int,
    min_world_y: int,
    has_textures: bool = False,
) -> None:
    """Write placed objects to binary .objects file.

    v2 format (OBJ2, when has_textures=True):
      magic: uint32 "OBJ2"
      placement_count: uint32
      min_world_x: int32
      min_world_y: int32
      total_vertex_count: uint32
      vertices: float32[total_vertex_count * 3]
      colors: uint8[total_vertex_count * 4]
      texcoords: float32[total_vertex_count * 2]

    v1 format (OBJS, when has_textures=False):
      same as above without texcoords
    """
    output_path.parent.mkdir(parents=True, exist_ok=True)

    total_verts = sum(p.vertex_count for p in placements)
    magic = OBJ2_MAGIC if has_textures else OBJS_MAGIC

    with open(output_path, "wb") as f:
        f.write(struct.pack("<I", magic))
        f.write(struct.pack("<I", len(placements)))
        f.write(struct.pack("<i", min_world_x))
        f.write(struct.pack("<i", min_world_y))
        f.write(struct.pack("<I", total_verts))

        # write all vertices concatenated
        for p in placements:
            for v in p.vertices:
                f.write(struct.pack("<f", v))

        # write all colors concatenated
        for p in placements:
            for r, g, b, a in p.colors:
                f.write(struct.pack("4B", r, g, b, a))

        # write texture coordinates (v2 only)
        if has_textures:
            for p in placements:
                for uv in p.uvs:
                    f.write(struct.pack("<f", uv))


# --- main pipeline ---


def sample_heightmap(
    hm: tuple[int, int, int, int, list[float]],
    world_x: int,
    world_y: int,
) -> float:
    """Sample terrain height at a world tile corner. Returns height in world units."""
    hm_min_x, hm_min_y, hm_w, hm_h, heights = hm
    lx = world_x - hm_min_x
    ly = world_y - hm_min_y
    if 0 <= lx < hm_w and 0 <= ly < hm_h:
        return heights[lx + ly * hm_w]
    return 0.0


def sample_height_bilinear(
    hm: tuple[int, int, int, int, list[float]],
    world_x: float,
    world_y: float,
) -> float:
    """Bilinear interpolation of terrain height at fractional world coords.

    Matches the OSRS client's Model.hillskew() which interpolates between
    4 tile corner heights using sub-tile fractions.
    """
    hm_min_x, hm_min_y, hm_w, hm_h, heights = hm
    fx = world_x - hm_min_x
    fy = world_y - hm_min_y

    tx = int(fx)
    ty = int(fy)
    frac_x = fx - tx
    frac_y = fy - ty

    # clamp to valid range (edge tiles use nearest neighbor)
    tx = max(0, min(tx, hm_w - 2))
    ty = max(0, min(ty, hm_h - 2))

    h00 = heights[tx + ty * hm_w]
    h10 = heights[(tx + 1) + ty * hm_w]
    h01 = heights[tx + (ty + 1) * hm_w]
    h11 = heights[(tx + 1) + (ty + 1) * hm_w]

    h_south = h00 * (1.0 - frac_x) + h10 * frac_x
    h_north = h01 * (1.0 - frac_x) + h11 * frac_x
    return h_south * (1.0 - frac_y) + h_north * frac_y


def _load_model_317(cache: CacheReader, model_id: int) -> bytes | None:
    """Load raw model bytes from 317 cache."""
    raw = cache.get(MODEL_INDEX, model_id)
    if raw is None:
        return None
    try:
        return gzip.decompress(raw)
    except Exception:
        return None


def process_placements(
    placements: list[PlacedObject],
    loc_defs: dict[int, LocDef],
    model_loader: "Callable[[int], bytes | None]",
    model_cache: dict[int, ModelData],
    heightmaps: dict[int, tuple[int, int, int, int, list[float]]] | None = None,
    tex_colors: dict[int, int] | None = None,
    atlas: TextureAtlas | None = None,
) -> list[ExpandedPlacement]:
    """Process raw placements into expanded vertex data ready for rendering.

    model_loader: callable(model_id) -> raw bytes or None. abstracts 317 vs modern cache.
    """
    results: list[ExpandedPlacement] = []
    skipped = 0
    model_miss = 0

    for po in placements:
        loc = loc_defs.get(po.obj_id)
        if loc is None:
            skipped += 1
            continue

        model_id = get_model_for_type(loc, po.obj_type)
        if model_id is None:
            skipped += 1
            continue

        # get or decode model
        if model_id not in model_cache:
            raw = model_loader(model_id)
            if raw is None:
                model_miss += 1
                continue
            md = decode_model(model_id, raw)
            if md is None:
                model_miss += 1
                continue
            model_cache[model_id] = md

        # deep copy the model so we can transform it without affecting cache
        src = model_cache[model_id]
        md = ModelData(
            model_id=src.model_id,
            vertex_count=src.vertex_count,
            face_count=src.face_count,
            vertices_x=list(src.vertices_x),
            vertices_y=list(src.vertices_y),
            vertices_z=list(src.vertices_z),
            face_a=list(src.face_a),
            face_b=list(src.face_b),
            face_c=list(src.face_c),
            face_colors=list(src.face_colors),
            face_textures=list(src.face_textures),
        )

        # apply color remapping
        apply_recolors(md, loc)

        # apply scale
        scale_model(md, loc.model_size_x, loc.model_size_h, loc.model_size_y)

        # for type 2 (diagonal walls), OSRS renders two model halves:
        #   half 1: rotation = 4 + r (mirrored + r rotations)
        #   half 2: rotation = (r + 1) & 3 (next rotation, no mirror)
        # for all other types: single model with rotation r
        if po.obj_type == 2:
            rotations_to_emit = [4 + po.rotation, (po.rotation + 1) & 3]
        else:
            rotations_to_emit = [po.rotation]

        all_verts: list[float] = []
        all_colors: list[tuple[int, int, int, int]] = []
        all_uvs: list[float] = []

        for eff_rot in rotations_to_emit:
            # deep copy per rotation variant
            md_copy = ModelData(
                model_id=md.model_id,
                vertex_count=md.vertex_count,
                face_count=md.face_count,
                vertices_x=list(md.vertices_x),
                vertices_y=list(md.vertices_y),
                vertices_z=list(md.vertices_z),
                face_a=list(md.face_a),
                face_b=list(md.face_b),
                face_c=list(md.face_c),
                face_colors=list(md.face_colors),
                face_textures=list(md.face_textures),
            )

            # mirror if rotation > 3 (XOR with definition's rotated flag)
            if loc.rotated ^ (eff_rot > 3):
                mirror_model(md_copy)

            # apply N * 90-degree rotations
            for _ in range(eff_rot % 4):
                rotate_model_90(md_copy)

            v, c, uv = expand_model(md_copy, tex_colors, atlas)
            all_verts.extend(v)
            all_colors.extend(c)
            all_uvs.extend(uv)

        verts = all_verts
        colors = all_colors
        face_uvs = all_uvs

        if not verts:
            continue

        # position in world: center model on its footprint
        # OSRS client: localX = (x << 7) + (sizeX << 6), i.e. x*128 + sizeX*64
        # in tile units: x + sizeX/2.0. Rotation 1 or 3 swaps width/length.
        model_scale = 1.0 / 128.0
        w = loc.width
        l = loc.length
        if po.rotation == 1 or po.rotation == 3:
            w, l = l, w
        wx = float(po.world_x) + w / 2.0
        # negate Z for right-handed coords (OSRS +Y north = -Z)
        wz = -(float(po.world_y) + l / 2.0)

        # type 5 wall decorations: offset away from parent wall
        # direction vectors from MapRegion: {1,0,-1,0} for X, {0,-1,0,1} for Y
        # default decorOffset=16 OSRS units; ideally from parent wall def
        if po.obj_type == 5:
            _deco_dir_x = (1, 0, -1, 0)
            _deco_dir_y = (0, -1, 0, 1)
            deco_off = loc.decor_offset * model_scale
            wx += _deco_dir_x[po.rotation] * deco_off
            wz -= _deco_dir_y[po.rotation] * deco_off

        # select heightmap for this object's plane
        heightmap = heightmaps.get(po.height) if heightmaps else None

        # sample terrain height at placement center
        ground_y = sample_heightmap(heightmap, po.world_x, po.world_y) if heightmap else 0.0

        # contoured ground: per-vertex terrain height interpolation
        # matches OSRS Model.hillskew() — bilinear interpolation at each vertex
        use_contouring = heightmap and (loc.contoured_ground or po.obj_type == 22)

        # apply offsets from definition (in model units, scale them)
        ox = float(loc.offset_x) * model_scale
        oh = float(loc.offset_h) * model_scale
        oy = float(loc.offset_y) * model_scale

        # transform vertices to world space
        # negate model Z to match our world coords (OSRS +Z → our -Z)
        # and swap triangle winding to preserve face normals after Z flip
        for i in range(0, len(verts), 9):
            # swap vertex 0 and vertex 2 within each triangle (reverses winding)
            for c in range(3):
                verts[i + c], verts[i + 6 + c] = verts[i + 6 + c], verts[i + c]
            colors[i // 3], colors[i // 3 + 2] = colors[i // 3 + 2], colors[i // 3]
            # swap UVs for vertex 0 and vertex 2 (same winding fix)
            uv_base = (i // 3) * 2
            face_uvs[uv_base], face_uvs[uv_base + 4] = face_uvs[uv_base + 4], face_uvs[uv_base]
            face_uvs[uv_base + 1], face_uvs[uv_base + 5] = face_uvs[uv_base + 5], face_uvs[uv_base + 1]

        for i in range(0, len(verts), 3):
            vx = verts[i] * model_scale + wx + ox
            vz = -verts[i + 2] * model_scale + wz - oy
            verts[i] = vx
            verts[i + 2] = vz

            if use_contouring:
                # bilinear terrain height at this vertex's world position
                # our world X maps directly to OSRS tile X
                # our world -Z maps to OSRS tile Y (north)
                vy_ground = sample_height_bilinear(heightmap, vx, -vz)
                # ground decorations (type 22) get a small Y offset to prevent
                # z-fighting with coplanar terrain — baked into vertex data since
                # glPolygonOffset isn't reliable through raylib's draw pipeline
                decal_offset = 0.01 if po.obj_type == 22 else 0.0
                verts[i + 1] = verts[i + 1] * model_scale + vy_ground + oh + decal_offset
            else:
                verts[i + 1] = verts[i + 1] * model_scale + ground_y + oh

        results.append(
            ExpandedPlacement(
                world_x=po.world_x,
                world_y=po.world_y,
                obj_type=po.obj_type,
                vertex_count=len(verts) // 3,
                face_count=len(verts) // 9,
                vertices=verts,
                colors=colors,
                uvs=face_uvs,
            )
        )

    if skipped:
        print(f"  skipped {skipped} placements (no definition/model)", file=sys.stderr)
    if model_miss:
        print(f"  {model_miss} model decode failures", file=sys.stderr)

    return results


def _build_and_write(
    args: argparse.Namespace,
    all_placements: list[PlacedObject],
    loc_defs: dict[int, LocDef],
    model_loader: Callable[[int], bytes | None],
    terrain_parsed: dict[tuple[int, int], RegionTerrain],
    tex_colors: dict[int, int],
    atlas: TextureAtlas | None = None,
) -> None:
    """Build geometry and write .objects binary (shared by 317 and modern paths)."""
    # stitch region edges for smooth heightmap
    if terrain_parsed:
        stitch_region_edges(terrain_parsed)

    # build heightmaps per plane
    heightmaps: dict[int, tuple[int, int, int, int, list[float]]] = {}
    if terrain_parsed:
        for plane in range(2):
            hm = build_heightmap(terrain_parsed, target_plane=plane)
            heightmaps[plane] = hm
        hm0 = heightmaps[0]
        print(f"  heightmap: {hm0[2]}x{hm0[3]} tiles, origin ({hm0[0]}, {hm0[1]})")

    # count by type
    type_counts: dict[int, int] = {}
    for po in all_placements:
        type_counts[po.obj_type] = type_counts.get(po.obj_type, 0) + 1
    for t in sorted(type_counts):
        print(f"    type {t:2d}: {type_counts[t]}")

    # process placements into expanded vertex data
    print("decoding models and building geometry...")
    model_geom_cache: dict[int, ModelData] = {}
    expanded = process_placements(
        all_placements, loc_defs, model_loader, model_geom_cache,
        heightmaps=heightmaps or None, tex_colors=tex_colors, atlas=atlas,
    )
    print(f"  {len(expanded)} objects with geometry, {len(model_geom_cache)} unique models decoded")

    total_verts = sum(p.vertex_count for p in expanded)
    total_tris = sum(p.face_count for p in expanded)
    print(f"  {total_verts:,} vertices, {total_tris:,} triangles")

    # compute bounds
    min_wx = min((p.world_x for p in expanded), default=0)
    min_wy = min((p.world_y for p in expanded), default=0)

    # write output
    has_textures = atlas is not None
    write_objects_binary(args.output, expanded, min_wx, min_wy, has_textures=has_textures)
    file_size = args.output.stat().st_size
    print(f"\nwrote {file_size:,} bytes to {args.output}")


def _main_317(args: argparse.Namespace) -> None:
    """Export objects from 317-format cache."""
    if not args.cache.exists():
        sys.exit(f"cache directory not found: {args.cache}")

    print(f"reading cache from {args.cache}")
    cache_reader = CacheReader(args.cache)

    print("loading object definitions...")
    loc_defs = decode_loc_definitions(cache_reader)
    print(f"  {len(loc_defs)} definitions with models")

    print("loading map index...")
    region_defs = load_map_index(cache_reader)
    print(f"  {len(region_defs)} regions in map index")

    # fight area center: region (47, 55)
    center_rx, center_ry = 47, 55
    r = args.radius

    target_regions = {
        (rd.region_x, rd.region_y): rd
        for rd in region_defs
        if center_rx - r <= rd.region_x <= center_rx + r
        and center_ry - r <= rd.region_y <= center_ry + r
    }
    print(f"  exporting {len(target_regions)} regions around ({center_rx}, {center_ry})")

    # parse all object placements
    all_placements: list[PlacedObject] = []
    errors = 0

    for (rx, ry), rd in sorted(target_regions.items()):
        obj_data = cache_reader.get(MAP_INDEX, rd.object_file)
        if obj_data is None:
            errors += 1
            continue

        try:
            obj_data = gzip.decompress(obj_data)
        except Exception:
            errors += 1
            continue

        base_x = rx * 64
        base_y = ry * 64
        placements = parse_object_placements(obj_data, base_x, base_y)
        all_placements.extend(placements)

    print(f"  {len(all_placements)} placements parsed, {errors} region errors")

    # parse terrain for heightmap (same regions)
    print("parsing terrain for ground heights...")
    terrain_parsed: dict[tuple[int, int], RegionTerrain] = {}
    for (rx, ry), rd in sorted(target_regions.items()):
        terrain_data = cache_reader.get(MAP_INDEX, rd.terrain_file)
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

    # load texture average colors for textured face rendering (fallback)
    tex_colors = load_texture_average_colors(cache_reader)
    print(f"  loaded {len(tex_colors)} texture average colors")

    # load texture sprites and build atlas
    print("loading texture sprites...")
    sprites = load_all_texture_sprites(cache_reader)
    print(f"  loaded {len(sprites)} texture sprites from cache")

    atlas = None
    atlas_path = args.output.with_suffix(".atlas")
    if sprites:
        atlas = build_atlas(sprites)
        print(f"  atlas: {atlas.width}x{atlas.height}, {len(atlas.uv_map)} textures mapped")
        write_atlas_binary(atlas_path, atlas)
        atlas_size = atlas_path.stat().st_size
        print(f"  wrote {atlas_size:,} bytes to {atlas_path}")

    loader_317 = lambda mid: _load_model_317(cache_reader, mid)
    _build_and_write(args, all_placements, loc_defs, loader_317, terrain_parsed,
                     tex_colors, atlas)


def _main_modern(args: argparse.Namespace) -> None:
    """Export objects from modern OpenRS2 cache."""
    if not args.modern_cache.exists():
        sys.exit(f"modern cache directory not found: {args.modern_cache}")

    print(f"reading modern cache from {args.modern_cache}")
    reader = ModernCacheReader(args.modern_cache)

    # load XTEA keys for object data decryption
    xtea_keys: dict[int, list[int]] = {}
    if args.keys and args.keys.exists():
        xtea_keys = load_xtea_keys(args.keys)
        print(f"  {len(xtea_keys)} XTEA keys loaded")

    print("loading object definitions from modern cache...")
    loc_defs = decode_loc_definitions_modern(reader)

    print("scanning index 5 for map groups...")
    map_groups = find_map_groups(reader)
    print(f"  {len(map_groups)} regions found in index 5")

    # determine target regions
    if not args.regions:
        sys.exit("--regions is required when using --modern-cache")

    target_coords: set[tuple[int, int]] = set()
    for coord in args.regions.split():
        parts = coord.split(",")
        target_coords.add((int(parts[0]), int(parts[1])))
    print(f"  exporting {len(target_coords)} specified regions")

    # parse object placements and terrain
    all_placements: list[PlacedObject] = []
    terrain_parsed: dict[tuple[int, int], RegionTerrain] = {}
    errors = 0

    for rx, ry in sorted(target_coords):
        ms = (rx << 8) | ry
        if ms not in map_groups:
            print(f"  region ({rx},{ry}): not found in index 5")
            errors += 1
            continue

        terrain_gid, loc_gid = map_groups[ms]

        # parse terrain
        if terrain_gid is not None:
            terrain_data = reader.read_container(5, terrain_gid)
            if terrain_data:
                rt = parse_terrain_full(terrain_data, rx * 64, ry * 64)
                rt.region_x = rx
                rt.region_y = ry
                terrain_parsed[(rx, ry)] = rt

        # parse object placements (XTEA encrypted in modern cache)
        if loc_gid is not None:
            import bz2
            import zlib

            from export_collision_map_modern import xtea_decrypt

            ms = (rx << 8) | ry
            key = xtea_keys.get(ms)
            if key is None:
                print(f"  region ({rx},{ry}): no XTEA key, skipping objects")
            else:
                raw_obj = reader._read_raw(5, loc_gid)
                loc_data = None
                if raw_obj is not None and len(raw_obj) >= 5:
                    # container: compression(1) + compressed_len(4) + encrypted payload
                    compression = raw_obj[0]
                    compressed_len = struct.unpack(">I", raw_obj[1:5])[0]
                    decrypted = xtea_decrypt(raw_obj[5:], key)

                    if compression == 0:
                        loc_data = decrypted[:compressed_len]
                    else:
                        # decrypted[0:4] = decompressed_len, then compressed data
                        gzip_data = decrypted[4:4 + compressed_len]
                        if compression == 2:
                            loc_data = zlib.decompress(gzip_data[10:], -zlib.MAX_WBITS)
                        elif compression == 1:
                            loc_data = bz2.decompress(b"BZh1" + gzip_data)

                if loc_data:
                    placements = parse_object_placements_modern(
                        loc_data, rx * 64, ry * 64,
                    )
                    all_placements.extend(placements)
                    print(f"  region ({rx},{ry}): {len(placements)} placements")
                else:
                    print(f"  region ({rx},{ry}): could not read/decrypt loc data")
                    errors += 1

    print(f"  {len(all_placements)} placements parsed, {errors} region errors")

    # no texture atlas for modern cache (textures not yet supported)
    tex_colors: dict[int, int] = {}

    loader_modern = lambda mid: load_model_modern(reader, mid)
    _build_and_write(args, all_placements, loc_defs, loader_modern, terrain_parsed,
                     tex_colors)


def main() -> None:
    parser = argparse.ArgumentParser(description="export OSRS placed objects from cache")
    parser.add_argument(
        "--cache",
        type=Path,
        default=Path("../reference/tarnish/game-server/data/cache"),
        help="path to 317-format cache directory",
    )
    parser.add_argument(
        "--modern-cache",
        type=Path,
        default=None,
        help="path to modern OpenRS2 cache directory (overrides --cache)",
    )
    parser.add_argument(
        "--keys",
        type=Path,
        default=None,
        help="path to XTEA keys JSON (for modern cache object data decryption)",
    )
    parser.add_argument(
        "--regions",
        type=str,
        default=None,
        help='space-separated region coordinates as rx,ry pairs (e.g. "35,47 35,48")',
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("data/wilderness.objects"),
        help="output .objects binary file",
    )
    parser.add_argument(
        "--radius",
        type=int,
        default=3,
        help="number of regions around the fight area center to export (default: 3)",
    )
    args = parser.parse_args()

    if args.modern_cache:
        _main_modern(args)
    else:
        _main_317(args)


if __name__ == "__main__":
    main()
