"""Export OSRS 3D models from a modern OSRS cache to a binary .models file.

Reads item definitions to find model IDs (inventory + male wield), then
decodes model geometry. Outputs a binary file consumable by osrs_pvp_models.h
and a generated C header mapping item IDs to model IDs.

Three model format variants are supported:
  - decodeOldFormat: 18-byte footer
  - decodeType2: 23-byte footer, magic 0xFF,0xFE at end-2
  - decodeType3: 26-byte footer, magic 0xFF,0xFD at end-2

Usage:
    uv run python scripts/export_models.py \
        --modern-cache ../../../.refs/osrs-cache-modern \
        --output data/equipment.models
"""

import argparse
import io
import math
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from modern_cache_reader import ModernCacheReader, decompress_container, read_string, read_u32
from export_textures import (
    TextureAtlas,
    build_atlas,
    load_texture_average_colors,
    load_texture_definitions,
    load_texture_sprites,
    write_texture_anim_binary,
)

_read_string = read_string

# --- constants ---

MODERN_MODEL_INDEX = 7
MODERN_CONFIG_OBJ_GROUP = 10
MODERN_CONFIG_IDK_GROUP = 3


@dataclass
class ItemDef:
    """Minimal item definition for model extraction."""

    item_id: int = 0
    name: str = ""
    inv_model: int = -1
    male_wield: int = -1
    male_wield2: int = -1
    male_wield3: int = -1
    male_offset: int = 0
    wearpos1: int = -1
    wearpos2: int = -1
    wearpos3: int = -1
    recolor_src: list[int] = field(default_factory=list)
    recolor_dst: list[int] = field(default_factory=list)


@dataclass
class IdentityKitDef:
    """Identity kit definition — a single body part mesh for player rendering.

    Body part IDs (male): 0=head, 1=jaw/beard, 2=torso, 3=arms, 4=hands, 5=legs, 6=feet.
    Female body parts are 7-13 (same order, offset by 7).
    """

    kit_id: int = 0
    body_part_id: int = -1
    body_models: list[int] = field(default_factory=list)
    original_colors: list[int] = field(default_factory=lambda: [0] * 6)
    replacement_colors: list[int] = field(default_factory=lambda: [0] * 6)
    valid_style: bool = False


# default male player appearance (kit indices from Config.DEFAULT_APPEARANCE)
DEFAULT_MALE_KITS = {
    0: 0,    # head (hair)
    1: 10,   # jaw (beard)
    2: 18,   # torso
    3: 26,   # arms
    4: 34,   # hands
    5: 36,   # legs
    6: 42,   # feet
}

# body part name labels for C header
BODY_PART_NAMES = ["HEAD", "JAW", "TORSO", "ARMS", "HANDS", "LEGS", "FEET"]

WEARPOS_RIGHT_HAND = 3
WEARPOS_TORSO = 4
WEARPOS_LEFT_HAND = 5
WEARPOS_ARMS = 6
WEARPOS_LEGS = 7
WEARPOS_HEAD = 8
WEARPOS_HANDS = 9
WEARPOS_FEET = 10
WEARPOS_JAW = 11

ITEM_RENDER_MODEL_MISSING = 0xFFFFFFFF
ITEM_RENDER_FLAG_TWO_HANDED = 1 << 0
ITEM_RENDER_FLAG_WEARPOS_AUTHORITY = 1 << 1
RUNEC_ITEM_RENDER_MAGIC = 0x4D455249
RUNEC_ITEM_RENDER_VERSION = 2

KNOWN_PLAYER_BAS: dict[int, tuple[int, int, int]] = {
    11802: (7053, 7052, 7043),
}

BODY_MASK_HEAD = 1 << BODY_PART_NAMES.index("HEAD")
BODY_MASK_JAW = 1 << BODY_PART_NAMES.index("JAW")
BODY_MASK_TORSO = 1 << BODY_PART_NAMES.index("TORSO")
BODY_MASK_ARMS = 1 << BODY_PART_NAMES.index("ARMS")
BODY_MASK_HANDS = 1 << BODY_PART_NAMES.index("HANDS")
BODY_MASK_LEGS = 1 << BODY_PART_NAMES.index("LEGS")
BODY_MASK_FEET = 1 << BODY_PART_NAMES.index("FEET")


def item_wearpos_value(wearpos: int) -> int:
    """Return a C-friendly wear position value."""

    if wearpos >= 0:
        return wearpos
    return ITEM_RENDER_MODEL_MISSING


def item_wearpos_values(item: ItemDef) -> tuple[int, int, int]:
    """Return all three C-friendly equipped wear position values."""

    return (
        item_wearpos_value(item.wearpos1),
        item_wearpos_value(item.wearpos2),
        item_wearpos_value(item.wearpos3),
    )


def item_render_anim_value(anim_id: int) -> int:
    """Return a C-friendly animation id value."""

    if anim_id >= 0:
        return anim_id
    return ITEM_RENDER_MODEL_MISSING


def load_runec_item_render_anims(path: Path) -> dict[int, tuple[int, int, int]]:
    """Load RuneC item ready, walk, and run animation metadata."""

    out = dict(KNOWN_PLAYER_BAS)
    if not path.is_file():
        return out

    blob = path.read_bytes()
    if len(blob) < 16:
        sys.exit(f"truncated RuneC item render map: {path}")

    magic, version, record_count, body_count = struct.unpack_from("<IIII", blob, 0)
    if magic != RUNEC_ITEM_RENDER_MAGIC:
        sys.exit(f"bad RuneC item render map magic in {path}: 0x{magic:08x}")
    if version < RUNEC_ITEM_RENDER_VERSION:
        return out

    offset = 16 + body_count * 4
    record_size = 13 * 4
    expected_size = offset + record_count * record_size
    if len(blob) < expected_size:
        sys.exit(f"truncated RuneC item render records: {path}")

    missing = ITEM_RENDER_MODEL_MISSING
    for i in range(record_count):
        fields = struct.unpack_from("<" + "I" * 13, blob, offset + i * record_size)
        item_id = fields[0]
        ready, walk, run = fields[10], fields[11], fields[12]
        if ready == missing and walk == missing and run == missing:
            continue
        out[item_id] = (
            -1 if ready == missing else int(ready),
            -1 if walk == missing else int(walk),
            -1 if run == missing else int(run),
        )

    return out


def item_render_flags(item: ItemDef) -> int:
    """Return viewer render flags derived from cache wear position data."""

    flags = 0
    wearposes = (item.wearpos1, item.wearpos2, item.wearpos3)
    if any(wearpos >= 0 for wearpos in wearposes):
        flags |= ITEM_RENDER_FLAG_WEARPOS_AUTHORITY
    if item.wearpos2 == WEARPOS_LEFT_HAND or item.wearpos3 == WEARPOS_LEFT_HAND:
        flags |= ITEM_RENDER_FLAG_TWO_HANDED
    return flags


def item_hide_body_mask(item: ItemDef) -> int:
    """Return default player body parts hidden by an equipped item."""

    mask = 0
    for wearpos in (item.wearpos1, item.wearpos2, item.wearpos3):
        if wearpos == WEARPOS_TORSO:
            mask |= BODY_MASK_TORSO
        elif wearpos == WEARPOS_ARMS:
            mask |= BODY_MASK_ARMS
        elif wearpos == WEARPOS_LEGS:
            mask |= BODY_MASK_LEGS
        elif wearpos == WEARPOS_HEAD:
            mask |= BODY_MASK_HEAD
        elif wearpos == WEARPOS_HANDS:
            mask |= BODY_MASK_HANDS
        elif wearpos == WEARPOS_FEET:
            mask |= BODY_MASK_FEET
        elif wearpos == WEARPOS_JAW:
            mask |= BODY_MASK_JAW
    return mask


def decode_identity_kits_modern(reader: ModernCacheReader) -> dict[int, IdentityKitDef]:
    """Decode identity kit definitions from modern cache config group 3.

    Same opcode format as 317: opcode 1=body_part, 2=body_models,
    3=valid_style, 40-49=recolor src, 50-59=recolor dst, 60-69=head models.
    """
    manifest = reader.read_index_manifest(2)
    if MODERN_CONFIG_IDK_GROUP not in manifest.group_ids:
        print("  warning: identity kit config group not found in modern cache")
        return {}

    file_ids = manifest.group_file_ids.get(MODERN_CONFIG_IDK_GROUP, [])
    kits: dict[int, IdentityKitDef] = {}

    for kit_id in sorted(file_ids):
        try:
            data = reader.read_config_entry(MODERN_CONFIG_IDK_GROUP, kit_id)
        except (KeyError, FileNotFoundError):
            continue

        kit = IdentityKitDef(kit_id=kit_id)
        buf = io.BytesIO(data)

        while True:
            opcode = buf.read(1)
            if not opcode:
                break
            op = opcode[0]
            if op == 0:
                break
            elif op == 1:
                kit.body_part_id = buf.read(1)[0]
            elif op == 2:
                n = buf.read(1)[0]
                kit.body_models = [
                    struct.unpack(">H", buf.read(2))[0] for _ in range(n)
                ]
            elif op == 3:
                kit.valid_style = True
            elif op == 5:
                n = buf.read(1)[0]
                kit.body_models = [read_u32(buf) for _ in range(n)]
            elif op == 40:
                n = buf.read(1)[0]
                for i in range(n):
                    src = struct.unpack(">H", buf.read(2))[0]
                    dst = struct.unpack(">H", buf.read(2))[0]
                    if i < len(kit.original_colors):
                        kit.original_colors[i] = src
                        kit.replacement_colors[i] = dst
            elif op == 41:
                n = buf.read(1)[0]
                buf.read(n * 4)
            elif 42 <= op < 50:
                kit.original_colors[op - 40] = struct.unpack(">H", buf.read(2))[0]
            elif 50 <= op < 60:
                kit.replacement_colors[op - 50] = struct.unpack(">H", buf.read(2))[0]
            elif 60 <= op < 70:
                buf.read(2)  # head model (not needed for body rendering)
            elif 70 <= op < 80:
                buf.read(4)

        kits[kit_id] = kit

    return kits


def _parse_modern_item_entry(item_id: int, data: bytes) -> ItemDef:
    """Parse a single modern item definition from opcode stream.

    Modern OSRS cache (rev226+) has many additional opcodes beyond the 317 set.
    We handle all known opcodes from RuneLite's ItemLoader plus modern additions.
    Unknown opcodes cause a break since we can't determine their byte length.

    Opcode reference (modern OSRS, compiled from RuneLite + rev226 analysis):
      0: terminator
      1: inv_model (u16)
      2: name (string)
      4-6: zoom/rotation (u16 each)
      7-8: model offsets (u16 each)
      9: unknown string (removed in modern, but some revs have it)
      10: unknown u16
      11: stackable (flag)
      12: value (i32)
      13-14: wearPos1/2 (u8 each)
      15: isTradeable (flag, modern addition)
      16: membersObject (flag)
      17: unknown u8 (modern)
      18: unknown u8 (modern)
      19: unknown u8 (modern, very common)
      20: unknown u8 (modern)
      21: groundScaleX (u16, modern)
      22: groundScaleY (u16, modern — very common placeholder item flag?)
      23: maleWield + offset (u16 + i8)
      24: maleWield2 (u16)
      25: femaleWield + offset (u16 + i8)
      26: femaleWield2 (u16)
      27: wearPos3 (u8)
      28: unknown u8
      29: unknown u8 (very common, modern)
      30-34: ground options (strings)
      35-39: interface options (strings)
      40: recolor pairs (u8 count, then u16+u16 per pair)
      41: retexture pairs (u8 count, then u16+u16 per pair)
      42: shiftClickDropIndex (u8)
      43: sub-operations (u8 count, then u16+u16 per entry — modern)
      62: unknown u8 (modern)
      64: unknown flag (modern)
      65: isTradeable (flag)
      69: unknown u8 (modern)
      71: unknown u16 (modern)
      75: weight (u16)
      78-79: maleModel2/femaleModel2 (u16 each)
      90-93: head models (u16 each)
      94: category (u16)
      95: zan2d (u16)
      97-98: cert references (u16 each)
      100-109: count objects (u16+u16 each)
      110-112: resize (u16 each)
      113-114: ambient/contrast (u8 each)
      115: team (u8)
      139-140: unnoted/noted references (u16 each)
      148-149: placeholder references (u16 each)
      155: unknown u8 (modern)
      156: unknown u16 (modern)
      157: unknown u8 (modern)
      158: unknown u8 (modern)
      159: unknown u8 (modern)
      160: unknown u16 (modern, u8 count then u16s — like wearPos list)
      161: unknown u16 (modern)
      162: unknown u8 (modern)
      163: unknown u8 (modern)
      164: unknown string (modern)
      165: unknown u8 (modern)
      202: unknown u16 (modern, seen on some items)
      211: unknown u8 count then u16s (modern)
      249: params map (u8 count, then key-value pairs)
    """
    d = ItemDef(item_id=item_id)
    buf = io.BytesIO(data)

    while True:
        opcode_byte = buf.read(1)
        if not opcode_byte:
            break
        opcode = opcode_byte[0]

        if opcode == 0:
            break
        elif opcode == 1:
            d.inv_model = struct.unpack(">H", buf.read(2))[0]
        elif opcode == 2:
            d.name = _read_string(buf)
        elif opcode == 3:
            _read_string(buf)  # description
        elif opcode == 4:
            buf.read(2)  # modelZoom
        elif opcode == 5:
            buf.read(2)  # modelRotationY
        elif opcode == 6:
            buf.read(2)  # modelRotationX
        elif opcode == 7:
            buf.read(2)  # modelOffset1
        elif opcode == 8:
            buf.read(2)  # modelOffset2
        elif opcode == 9:
            _read_string(buf)  # unknown string
        elif opcode == 10:
            buf.read(2)  # unknown u16
        elif opcode == 11:
            pass  # stackable
        elif opcode == 12:
            buf.read(4)  # value
        elif opcode == 13:
            d.wearpos1 = buf.read(1)[0]
        elif opcode == 14:
            d.wearpos2 = buf.read(1)[0]
        elif opcode == 15:
            pass  # isTradeable (modern flag, 0 bytes)
        elif opcode == 16:
            pass  # membersObject
        elif opcode == 17:
            buf.read(1)  # unknown u8
        elif opcode == 18:
            buf.read(1)  # unknown u8
        elif opcode == 19:
            buf.read(1)  # unknown u8 (very common in modern)
        elif opcode == 20:
            buf.read(1)  # unknown u8
        elif opcode == 21:
            buf.read(2)  # groundScaleX
        elif opcode == 22:
            buf.read(2)  # groundScaleY
        elif opcode == 23:
            d.male_wield = struct.unpack(">H", buf.read(2))[0]
            d.male_offset = struct.unpack(">b", buf.read(1))[0]
        elif opcode == 24:
            d.male_wield2 = struct.unpack(">H", buf.read(2))[0]
        elif opcode == 25:
            buf.read(2)  # femaleWield
            buf.read(1)  # femaleOffset
        elif opcode == 26:
            buf.read(2)  # femaleWield2
        elif opcode == 27:
            d.wearpos3 = buf.read(1)[0]
        elif opcode == 28:
            buf.read(1)  # unknown u8
        elif opcode == 29:
            buf.read(1)  # unknown u8 (very common in modern)
        elif 30 <= opcode < 35:
            _read_string(buf)  # groundActions
        elif 35 <= opcode < 40:
            _read_string(buf)  # itemActions
        elif opcode == 40:
            count = buf.read(1)[0]
            for _ in range(count):
                src = struct.unpack(">H", buf.read(2))[0]
                dst = struct.unpack(">H", buf.read(2))[0]
                d.recolor_src.append(src)
                d.recolor_dst.append(dst)
        elif opcode == 41:
            count = buf.read(1)[0]
            for _ in range(count):
                buf.read(4)  # retextureFrom + retextureTo
        elif opcode == 42:
            buf.read(1)  # shiftClickDropIndex
        elif opcode == 43:
            buf.read(1)
            while True:
                subop = buf.read(1)
                if not subop or subop[0] == 0:
                    break
                _read_string(buf)
        elif opcode == 44:
            d.inv_model = read_u32(buf)
        elif opcode == 45:
            d.male_wield = read_u32(buf)
            d.male_offset = struct.unpack(">b", buf.read(1))[0]
        elif opcode == 46:
            d.male_wield2 = read_u32(buf)
        elif opcode == 47:
            d.male_wield3 = read_u32(buf)
        elif opcode == 48:
            read_u32(buf)
            buf.read(1)
        elif opcode == 49:
            read_u32(buf)
        elif opcode == 50:
            read_u32(buf)
        elif opcode in (51, 52, 53, 54):
            read_u32(buf)
        elif opcode == 62:
            buf.read(1)  # unknown u8
        elif opcode == 64:
            pass  # unknown flag (0 bytes)
        elif opcode == 65:
            pass  # isTradeable
        elif opcode == 66:
            buf.read(2)  # unknown u16
        elif opcode == 67:
            buf.read(2)  # unknown u16
        elif opcode == 68:
            buf.read(2)  # unknown u16
        elif opcode == 69:
            buf.read(1)  # unknown u8
        elif opcode == 71:
            buf.read(2)  # unknown u16
        elif opcode == 73:
            buf.read(2)  # unknown u16 (modern)
        elif opcode == 74:
            buf.read(2)  # unknown u16 (modern)
        elif opcode == 75:
            buf.read(2)  # weight
        elif opcode == 76:
            buf.read(2)  # unknown u16 (modern)
        elif opcode == 77:
            buf.read(2)  # unknown u16 (modern)
        elif opcode == 78:
            d.male_wield3 = struct.unpack(">H", buf.read(2))[0]
        elif opcode == 79:
            buf.read(2)  # femaleModel2
        elif opcode == 80:
            buf.read(2)  # unknown u16 (modern)
        elif opcode == 81:
            buf.read(2)  # unknown u16 (modern)
        elif opcode == 82:
            buf.read(2)  # unknown u16 (modern)
        elif opcode == 83:
            buf.read(2)  # unknown u16 (modern)
        elif opcode == 84:
            buf.read(2)  # unknown u16 (modern)
        elif opcode == 85:
            buf.read(2)  # unknown u16 (modern)
        elif opcode == 86:
            buf.read(2)  # unknown u16 (modern)
        elif opcode == 87:
            buf.read(2)  # unknown u16 (modern)
        elif opcode == 90:
            buf.read(2)  # maleHeadModel
        elif opcode == 91:
            buf.read(2)  # femaleHeadModel
        elif opcode == 92:
            buf.read(2)  # maleHeadModel2
        elif opcode == 93:
            buf.read(2)  # femaleHeadModel2
        elif opcode == 94:
            buf.read(2)  # category
        elif opcode == 95:
            buf.read(2)  # zan2d
        elif opcode == 97:
            buf.read(2)  # certID
        elif opcode == 98:
            buf.read(2)  # certTemplateID
        elif 100 <= opcode < 110:
            buf.read(4)  # stackIDs + stackAmounts
        elif opcode == 110:
            buf.read(2)  # resizeX
        elif opcode == 111:
            buf.read(2)  # resizeY
        elif opcode == 112:
            buf.read(2)  # resizeZ
        elif opcode == 113:
            buf.read(1)  # brightness
        elif opcode == 114:
            buf.read(1)  # contrast
        elif opcode == 115:
            buf.read(1)  # team
        elif opcode == 116:
            buf.read(2)  # unknown u16 (modern)
        elif opcode == 117:
            buf.read(2)  # unknown u16 (modern)
        elif opcode == 118:
            buf.read(2)  # unknown u16 (modern)
        elif opcode == 119:
            buf.read(1)  # unknown u8 (modern)
        elif opcode == 120:
            buf.read(1)  # unknown u8 (modern)
        elif opcode == 121:
            buf.read(1)  # unknown u8 (modern)
        elif opcode == 122:
            buf.read(1)  # unknown u8 (modern)
        elif opcode == 139:
            buf.read(2)  # unnotedId
        elif opcode == 140:
            buf.read(2)  # notedId
        elif opcode == 148:
            buf.read(2)  # placeholderId
        elif opcode == 149:
            buf.read(2)  # placeholderTemplateId
        elif opcode == 155:
            buf.read(1)  # unknown u8
        elif opcode == 156:
            buf.read(2)  # unknown u16
        elif opcode == 157:
            buf.read(1)  # unknown u8
        elif opcode == 158:
            buf.read(1)  # unknown u8
        elif opcode == 159:
            buf.read(1)  # unknown u8
        elif opcode == 160:
            count = buf.read(1)[0]
            for _ in range(count):
                buf.read(2)  # u16 per entry
        elif opcode == 161:
            buf.read(2)  # unknown u16
        elif opcode == 162:
            buf.read(1)  # unknown u8
        elif opcode == 163:
            buf.read(1)  # unknown u8
        elif opcode == 164:
            _read_string(buf)  # unknown string
        elif opcode == 165:
            buf.read(1)  # unknown u8
        elif opcode == 202:
            buf.read(2)  # unknown u16
        elif opcode == 211:
            count = buf.read(1)[0]
            for _ in range(count):
                buf.read(2)  # u16 per entry
        elif opcode == 249:
            count = buf.read(1)[0]
            for _ in range(count):
                is_string = buf.read(1)[0]
                buf.read(3)  # key (medium)
                if is_string:
                    _read_string(buf)
                else:
                    buf.read(4)  # int value
        else:
            print(
                f"  warning: unknown modern item opcode {opcode} at item {item_id}, "
                f"pos {buf.tell()}",
                file=sys.stderr,
            )
            break

    return d


def decode_item_definitions_modern(reader: ModernCacheReader) -> dict[int, ItemDef]:
    """Decode item definitions from modern cache (config index 2, group 6).

    Only parses items we actually need (SIM_ITEM_IDS) to avoid issues with
    unknown opcodes in items we don't care about.
    """
    files = reader.read_group(2, MODERN_CONFIG_OBJ_GROUP)
    defs: dict[int, ItemDef] = {}

    for item_id in SIM_ITEM_IDS:
        if item_id not in files:
            print(f"  warning: item {item_id} not in modern cache config group {MODERN_CONFIG_OBJ_GROUP}")
            continue
        d = _parse_modern_item_entry(item_id, files[item_id])
        if d.inv_model >= 0 or d.name:
            defs[item_id] = d

    return defs


def load_model_modern(reader: ModernCacheReader, model_id: int) -> bytes | None:
    """Load raw model bytes from modern cache (index 7, container-compressed)."""
    raw = reader._read_raw(MODERN_MODEL_INDEX, model_id)
    if raw is None:
        return None
    return decompress_container(raw)


# --- model geometry decoder ---


def read_smart_signed(buf: io.BytesIO) -> int:
    """Read a signed smart (same as Java Buffer.readSmart in tarnish).

    Single byte: value - 64 (range -64 to 63)
    Two bytes:   value - 49152 (range -16384 to 16383)
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
    return raw - 49152


@dataclass
class ModelData:
    """Decoded model geometry."""

    model_id: int = 0
    vertex_count: int = 0
    face_count: int = 0
    vertices_x: list[int] = field(default_factory=list)
    vertices_y: list[int] = field(default_factory=list)
    vertices_z: list[int] = field(default_factory=list)
    face_a: list[int] = field(default_factory=list)
    face_b: list[int] = field(default_factory=list)
    face_c: list[int] = field(default_factory=list)
    face_colors: list[int] = field(default_factory=list)  # 15-bit HSL per face
    face_textures: list[int] = field(default_factory=list)  # texture ID per face (-1 = none)
    vertex_skins: list[int] = field(default_factory=list)  # label group per vertex (for animation)
    face_priorities: list[int] = field(default_factory=list)  # per-face render priority (0-11)
    face_alphas: list[int] = field(default_factory=list)  # per-face alpha (0=opaque)
    face_skins: list[int] = field(default_factory=list)  # per-face alpha animation labels
    face_render_types: list[int] = field(default_factory=list)
    face_tex_coords: list[int] = field(default_factory=list)
    tex_u: list[int] = field(default_factory=list)
    tex_v: list[int] = field(default_factory=list)
    tex_w: list[int] = field(default_factory=list)
    tex_face_count: int = 0


def _read_ubyte(data: bytes, offset: int) -> int:
    return data[offset]


def _read_ushort(data: bytes, offset: int) -> int:
    return (data[offset] << 8) | data[offset + 1]


def decode_model(model_id: int, data: bytes) -> ModelData | None:
    """Decode a model from raw cache data. Handles all 3 format variants."""
    if len(data) < 18:
        return None

    # detect format from last 2 bytes (Java signed: -1=0xFF, -2=0xFE, -3=0xFD)
    last2 = data[-2]
    last1 = data[-1]

    if last2 == 0xFF and last1 == 0xFD:
        return _decode_type3(model_id, data)
    if last2 == 0xFF and last1 == 0xFE:
        return _decode_type2(model_id, data)
    if last2 == 0xFF and last1 == 0xFF:
        return _decode_type1(model_id, data)
    return _decode_old_format(model_id, data)


def _decode_vertices(
    data: bytes,
    flags_offset: int,
    x_offset: int,
    y_offset: int,
    z_offset: int,
    count: int,
) -> tuple[list[int], list[int], list[int]]:
    """Decode vertex positions from delta-encoded streams."""
    vx, vy, vz = [], [], []
    fbuf = io.BytesIO(data)
    fbuf.seek(flags_offset)
    xbuf = io.BytesIO(data)
    xbuf.seek(x_offset)
    ybuf = io.BytesIO(data)
    ybuf.seek(y_offset)
    zbuf = io.BytesIO(data)
    zbuf.seek(z_offset)

    cx, cy, cz = 0, 0, 0
    for _ in range(count):
        flags = fbuf.read(1)[0]
        dx = read_smart_signed(xbuf) if (flags & 1) else 0
        dy = read_smart_signed(ybuf) if (flags & 2) else 0
        dz = read_smart_signed(zbuf) if (flags & 4) else 0
        cx += dx
        cy += dy
        cz += dz
        vx.append(cx)
        vy.append(cy)
        vz.append(cz)

    return vx, vy, vz


def _decode_faces(
    data: bytes,
    index_offset: int,
    type_offset: int,
    count: int,
) -> tuple[list[int], list[int], list[int]]:
    """Decode triangle indices using strip encoding (4 face types)."""
    fa, fb, fc = [], [], []
    ibuf = io.BytesIO(data)
    ibuf.seek(index_offset)
    tbuf = io.BytesIO(data)
    tbuf.seek(type_offset)

    a, b, c, last = 0, 0, 0, 0
    for _ in range(count):
        ftype = tbuf.read(1)[0]
        if ftype == 1:
            a = read_smart_signed(ibuf) + last
            b = read_smart_signed(ibuf) + a
            c = read_smart_signed(ibuf) + b
            last = c
        elif ftype == 2:
            b = c
            c = read_smart_signed(ibuf) + last
            last = c
        elif ftype == 3:
            a = c
            c = read_smart_signed(ibuf) + last
            last = c
        elif ftype == 4:
            tmp = a
            a = b
            b = tmp
            c = read_smart_signed(ibuf) + last
            last = c
        else:
            # type 0 or unknown: skip
            fa.append(0)
            fb.append(0)
            fc.append(0)
            continue
        fa.append(a)
        fb.append(b)
        fc.append(c)

    return fa, fb, fc


def _decode_vertex_skins(
    data: bytes, offset: int, count: int, has_skins: int,
) -> list[int]:
    """Read per-vertex skin labels (label group assignments for animation).

    If has_skins == 1, reads one byte per vertex at offset.
    Otherwise all vertices get label 0 (single group).
    """
    if has_skins == 1:
        return [_read_ubyte(data, offset + i) for i in range(count)]
    return [0] * count


def _decode_face_priorities(
    data: bytes, offset: int, count: int, model_priority: int,
) -> list[int]:
    """Read per-face render priorities.

    If model_priority == 255, priorities are stored per-face as bytes at offset.
    Otherwise all faces share the model-level priority value.
    """
    if model_priority == 255:
        return [_read_ubyte(data, offset + i) for i in range(count)]
    return [model_priority] * count


def _decode_face_alphas(data: bytes, offset: int, count: int, has_alphas: int) -> list[int]:
    """Read per-face transparency bytes."""
    if has_alphas == 1:
        return [_read_ubyte(data, offset + i) for i in range(count)]
    return [0] * count


def _decode_face_skins(data: bytes, offset: int, count: int, has_skins: int) -> list[int]:
    """Read per-face alpha animation labels."""
    if has_skins == 1:
        return [_read_ubyte(data, offset + i) for i in range(count)]
    return [255] * count


def _decode_face_render_types(data: bytes, offset: int, count: int, has_types: bool) -> list[int]:
    """Read per-face render types."""
    if has_types:
        return [_read_ubyte(data, offset + i) for i in range(count)]
    return [0] * count


def _decode_face_colors(data: bytes, offset: int, count: int) -> list[int]:
    """Read face colors as unsigned shorts."""
    colors = []
    for i in range(count):
        colors.append(_read_ushort(data, offset + i * 2))
    return colors


def _decode_face_textures_from_stream(
    data: bytes, offset: int, count: int,
) -> list[int]:
    """Read face texture IDs as signed shorts (readUShort() - 1 in Java).

    Mirrors type1/type3 decoder: faceTextures[i] = readUShort() - 1.
    Result: -1 means no texture, >= 0 is a valid texture ID.
    """
    textures = []
    for i in range(count):
        val = _read_ushort(data, offset + i * 2) - 1
        # unsigned 0 → -1 (no texture), unsigned N → N-1 (texture ID)
        textures.append(val)
    return textures


def _decode_texture_render_types(data: bytes, count: int) -> list[int]:
    """Read model texture render types as signed Java bytes."""
    types: list[int] = []
    for i in range(count):
        value = data[i]
        if value >= 128:
            value -= 256
        types.append(value)
    return types


def _decode_texture_triangle_indices(
    data: bytes,
    offset: int,
    texture_count: int,
    texture_render_types: list[int] | None = None,
) -> tuple[list[int], list[int], list[int]]:
    """Read type-0 texture coordinate triangles."""
    tex_u = [0] * texture_count
    tex_v = [0] * texture_count
    tex_w = [0] * texture_count
    pos = offset
    for i in range(texture_count):
        render_type = (
            texture_render_types[i] & 0xFF
            if texture_render_types is not None and i < len(texture_render_types)
            else 0
        )
        if render_type == 0:
            tex_u[i] = _read_ushort(data, pos)
            tex_v[i] = _read_ushort(data, pos + 2)
            tex_w[i] = _read_ushort(data, pos + 4)
            pos += 6
    return tex_u, tex_v, tex_w


def _decode_face_texture_coords_from_stream(
    data: bytes,
    offset: int,
    face_textures: list[int],
    texture_count: int,
) -> list[int]:
    """Read per-face texture-coordinate triangle indexes."""
    coords = [-1] * len(face_textures)
    pos = offset
    for i, texture_id in enumerate(face_textures):
        if texture_id < 0:
            continue
        if pos >= len(data):
            break
        coord = data[pos] - 1
        pos += 1
        if 0 <= coord < texture_count:
            coords[i] = coord
    return coords


def _apply_render_type_textures_and_coords(
    data: bytes,
    render_type_offset: int,
    face_colors: list[int],
    face_count: int,
) -> tuple[list[int], list[int], list[int]]:
    """Handle faceRenderType texture flags."""
    face_textures = [-1] * face_count
    face_tex_coords = [-1] * face_count
    face_render_types = [0] * face_count
    for i in range(face_count):
        render_type = _read_ubyte(data, render_type_offset + i)
        if render_type & 1:
            face_render_types[i] = 1
        if render_type & 2:
            face_textures[i] = face_colors[i]
            face_colors[i] = 127
            face_tex_coords[i] = render_type >> 2
    return face_textures, face_tex_coords, face_render_types


def _apply_render_type_textures(
    data: bytes,
    render_type_offset: int,
    face_colors: list[int],
    face_count: int,
) -> list[int]:
    """Compatibility wrapper for older callers."""
    face_textures, _, _ = _apply_render_type_textures_and_coords(
        data, render_type_offset, face_colors, face_count,
    )
    return face_textures


def _drop_redundant_texture_coords(
    face_a: list[int],
    face_b: list[int],
    face_c: list[int],
    tex_u: list[int],
    tex_v: list[int],
    tex_w: list[int],
    face_tex_coords: list[int],
) -> list[int]:
    """Drop explicit texture coords matching the face triangle."""
    out = list(face_tex_coords)
    for i, coord in enumerate(out):
        if coord < 0 or coord >= len(tex_u):
            continue
        if (
            i < len(face_a)
            and face_a[i] == tex_u[coord]
            and face_b[i] == tex_v[coord]
            and face_c[i] == tex_w[coord]
        ):
            out[i] = -1
    return out


def _decode_type2(model_id: int, data: bytes) -> ModelData | None:
    """Decode type2 model data."""
    n = len(data)
    if n < 23:
        return None

    off = n - 23
    var9 = _read_ushort(data, off)
    var10 = _read_ushort(data, off + 2)
    var11 = _read_ubyte(data, off + 4)
    var12 = _read_ubyte(data, off + 5)
    var13 = _read_ubyte(data, off + 6)
    var14 = _read_ubyte(data, off + 7)
    var15 = _read_ubyte(data, off + 8)
    var16 = _read_ubyte(data, off + 9)
    _ = _read_ubyte(data, off + 10)
    var18 = _read_ushort(data, off + 11)
    var19 = _read_ushort(data, off + 13)
    _ = _read_ushort(data, off + 15)
    var21 = _read_ushort(data, off + 17)
    var22 = _read_ushort(data, off + 19)

    var23 = 0
    var24 = var23 + var9
    var25 = var24
    var24 += var10

    var26 = var24
    if var13 == 255:
        var24 += var10

    var27 = var24
    if var15 == 1:
        var24 += var10

    var28 = var24
    if var12 == 1:
        var24 += var10

    var29 = var24
    var24 += var22

    var30 = var24
    if var14 == 1:
        var24 += var10

    var31 = var24
    var24 += var21

    var32 = var24
    var24 += var10 * 2

    var33 = var24
    var24 += var11 * 6

    var34 = var24
    var24 += var18
    var35 = var24
    var24 += var19

    vx, vy, vz = _decode_vertices(data, var23, var34, var35, var24, var9)
    fa, fb, fc = _decode_faces(data, var31, var25, var10)
    colors = _decode_face_colors(data, var32, var10)
    tex_u, tex_v, tex_w = _decode_texture_triangle_indices(data, var33, var11, None)

    face_textures: list[int] = []
    face_tex_coords: list[int] = []
    face_render_types = [0] * var10
    if var12 == 1:
        face_textures, face_tex_coords, face_render_types = (
            _apply_render_type_textures_and_coords(data, var28, colors, var10)
        )
        face_tex_coords = _drop_redundant_texture_coords(
            fa, fb, fc, tex_u, tex_v, tex_w, face_tex_coords,
        )

    priorities = _decode_face_priorities(data, var26, var10, var13)
    alphas = _decode_face_alphas(data, var30, var10, var14)
    face_skins = _decode_face_skins(data, var27, var10, var15)
    skins = _decode_vertex_skins(data, var29, var9, var16)

    return ModelData(
        model_id=model_id,
        vertex_count=var9,
        face_count=var10,
        vertices_x=vx, vertices_y=vy, vertices_z=vz,
        face_a=fa, face_b=fb, face_c=fc,
        face_colors=colors,
        face_textures=face_textures,
        face_priorities=priorities,
        face_alphas=alphas,
        face_skins=face_skins,
        face_render_types=face_render_types,
        vertex_skins=skins,
        face_tex_coords=face_tex_coords,
        tex_u=tex_u,
        tex_v=tex_v,
        tex_w=tex_w,
        tex_face_count=var11,
    )


def _decode_old_format(model_id: int, data: bytes) -> ModelData | None:
    """Decode old-format model data."""
    n = len(data)
    if n < 18:
        return None

    off = n - 18
    var9 = _read_ushort(data, off)
    var10 = _read_ushort(data, off + 2)
    var11 = _read_ubyte(data, off + 4)
    var12 = _read_ubyte(data, off + 5)
    var13 = _read_ubyte(data, off + 6)
    var14 = _read_ubyte(data, off + 7)
    var15 = _read_ubyte(data, off + 8)
    var16 = _read_ubyte(data, off + 9)
    var17 = _read_ushort(data, off + 10)
    var18 = _read_ushort(data, off + 12)
    _ = _read_ushort(data, off + 14)
    var20 = _read_ushort(data, off + 16)

    var21 = 0
    var22 = var21 + var9
    var23 = var22
    var22 += var10

    var24 = var22
    if var13 == 255:
        var22 += var10

    var25 = var22
    if var15 == 1:
        var22 += var10

    var26 = var22
    if var12 == 1:
        var22 += var10

    var27 = var22
    if var16 == 1:
        var22 += var9

    var28 = var22
    if var14 == 1:
        var22 += var10

    var29 = var22
    var22 += var20

    var30 = var22
    var22 += var10 * 2

    var31 = var22
    var22 += var11 * 6

    var32 = var22
    var22 += var17
    var33 = var22
    var22 += var18

    vx, vy, vz = _decode_vertices(data, var21, var32, var33, var22, var9)
    fa, fb, fc = _decode_faces(data, var29, var23, var10)
    colors = _decode_face_colors(data, var30, var10)
    tex_u, tex_v, tex_w = _decode_texture_triangle_indices(data, var31, var11, None)

    face_textures: list[int] = []
    face_tex_coords: list[int] = []
    face_render_types = [0] * var10
    if var12 == 1:
        face_textures, face_tex_coords, face_render_types = (
            _apply_render_type_textures_and_coords(data, var26, colors, var10)
        )
        face_tex_coords = _drop_redundant_texture_coords(
            fa, fb, fc, tex_u, tex_v, tex_w, face_tex_coords,
        )

    priorities = _decode_face_priorities(data, var24, var10, var13)
    alphas = _decode_face_alphas(data, var28, var10, var14)
    face_skins = _decode_face_skins(data, var25, var10, var15)
    skins = _decode_vertex_skins(data, var27, var9, var16)

    return ModelData(
        model_id=model_id,
        vertex_count=var9,
        face_count=var10,
        vertices_x=vx, vertices_y=vy, vertices_z=vz,
        face_a=fa, face_b=fb, face_c=fc,
        face_colors=colors,
        face_textures=face_textures,
        face_priorities=priorities,
        face_alphas=alphas,
        face_skins=face_skins,
        face_render_types=face_render_types,
        vertex_skins=skins,
        face_tex_coords=face_tex_coords,
        tex_u=tex_u,
        tex_v=tex_v,
        tex_w=tex_w,
        tex_face_count=var11,
    )


def _decode_type1(model_id: int, data: bytes) -> ModelData | None:
    """Decode type1 model data."""
    n = len(data)
    if n < 23:
        return None

    off = n - 23
    var9 = _read_ushort(data, off)
    var10 = _read_ushort(data, off + 2)
    var11 = _read_ubyte(data, off + 4)
    var12 = _read_ubyte(data, off + 5)
    var13 = _read_ubyte(data, off + 6)
    var14 = _read_ubyte(data, off + 7)
    var15 = _read_ubyte(data, off + 8)
    var16 = _read_ubyte(data, off + 9)
    var17 = _read_ubyte(data, off + 10)
    var18 = _read_ushort(data, off + 11)
    var19 = _read_ushort(data, off + 13)
    var20 = _read_ushort(data, off + 15)
    var21 = _read_ushort(data, off + 17)
    var22 = _read_ushort(data, off + 19)

    texture_render_types = _decode_texture_render_types(data, var11)
    tex_type0 = sum(1 for t in texture_render_types if t == 0)
    tex_type13 = sum(1 for t in texture_render_types if 1 <= t <= 3)

    var26 = var11 + var9
    var56 = var26
    if var12 == 1:
        var26 += var10

    var28 = var26
    var26 += var10

    var29 = var26
    if var13 == 255:
        var26 += var10

    var30 = var26
    if var15 == 1:
        var26 += var10

    var31 = var26
    if var17 == 1:
        var26 += var9

    var32 = var26
    if var14 == 1:
        var26 += var10

    var33 = var26
    var26 += var21

    var34 = var26
    if var16 == 1:
        var26 += var10 * 2

    var35 = var26
    var26 += var22

    var36 = var26
    var26 += var10 * 2

    var37 = var26
    var26 += var18
    var38 = var26
    var26 += var19
    var39 = var26
    var26 += var20

    var40 = var26
    var26 += tex_type0 * 6
    var41 = var26
    var26 += tex_type13 * 6

    vx, vy, vz = _decode_vertices(data, var11, var37, var38, var39, var9)
    fa, fb, fc = _decode_faces(data, var33, var28, var10)
    colors = _decode_face_colors(data, var36, var10)
    face_render_types = _decode_face_render_types(data, var56, var10, var12 == 1)

    face_textures: list[int] = []
    face_tex_coords: list[int] = []
    if var16 == 1:
        face_textures = _decode_face_textures_from_stream(data, var34, var10)
        if var11 > 0:
            face_tex_coords = _decode_face_texture_coords_from_stream(
                data, var35, face_textures, var11,
            )

    tex_u, tex_v, tex_w = _decode_texture_triangle_indices(
        data, var40, var11, texture_render_types,
    )

    priorities = _decode_face_priorities(data, var29, var10, var13)
    alphas = _decode_face_alphas(data, var32, var10, var14)
    face_skins = _decode_face_skins(data, var30, var10, var15)
    skins = _decode_vertex_skins(data, var31, var9, var17)

    return ModelData(
        model_id=model_id,
        vertex_count=var9,
        face_count=var10,
        vertices_x=vx, vertices_y=vy, vertices_z=vz,
        face_a=fa, face_b=fb, face_c=fc,
        face_colors=colors,
        face_textures=face_textures,
        face_priorities=priorities,
        face_alphas=alphas,
        face_skins=face_skins,
        face_render_types=face_render_types,
        vertex_skins=skins,
        face_tex_coords=face_tex_coords,
        tex_u=tex_u,
        tex_v=tex_v,
        tex_w=tex_w,
        tex_face_count=var11,
    )


def _decode_type3(model_id: int, data: bytes) -> ModelData | None:
    """Decode type3 model data."""
    n = len(data)
    if n < 26:
        return None

    off = n - 26
    var9 = _read_ushort(data, off)
    var10 = _read_ushort(data, off + 2)
    var11 = _read_ubyte(data, off + 4)
    var12 = _read_ubyte(data, off + 5)
    var13 = _read_ubyte(data, off + 6)
    var14 = _read_ubyte(data, off + 7)
    var15 = _read_ubyte(data, off + 8)
    var16 = _read_ubyte(data, off + 9)
    var17 = _read_ubyte(data, off + 10)
    _ = _read_ubyte(data, off + 11)
    var19 = _read_ushort(data, off + 12)
    var20 = _read_ushort(data, off + 14)
    var21 = _read_ushort(data, off + 16)
    var22 = _read_ushort(data, off + 18)
    var23 = _read_ushort(data, off + 20)
    var24 = _read_ushort(data, off + 22)

    texture_render_types = _decode_texture_render_types(data, var11)
    tex_type0 = sum(1 for t in texture_render_types if t == 0)
    tex_type13 = sum(1 for t in texture_render_types if 1 <= t <= 3)

    var28 = var11 + var9
    var58 = var28
    if var12 == 1:
        var28 += var10

    var30 = var28
    var28 += var10

    var31 = var28
    if var13 == 255:
        var28 += var10

    var32 = var28
    if var15 == 1:
        var28 += var10

    var33 = var28
    var28 += var24

    var34 = var28
    if var14 == 1:
        var28 += var10

    var35 = var28
    var28 += var22

    var36 = var28
    if var16 == 1:
        var28 += var10 * 2

    var37 = var28
    var28 += var23

    var38 = var28
    var28 += var10 * 2

    var39 = var28
    var28 += var19
    var40 = var28
    var28 += var20
    var41 = var28
    var28 += var21

    var42 = var28
    var28 += tex_type0 * 6
    var43 = var28
    var28 += tex_type13 * 6

    vx, vy, vz = _decode_vertices(data, var11, var39, var40, var41, var9)
    fa, fb, fc = _decode_faces(data, var35, var30, var10)
    colors = _decode_face_colors(data, var38, var10)
    face_render_types = _decode_face_render_types(data, var58, var10, var12 == 1)

    face_textures: list[int] = []
    face_tex_coords: list[int] = []
    if var16 == 1:
        face_textures = _decode_face_textures_from_stream(data, var36, var10)
        if var11 > 0:
            face_tex_coords = _decode_face_texture_coords_from_stream(
                data, var37, face_textures, var11,
            )

    tex_u, tex_v, tex_w = _decode_texture_triangle_indices(
        data, var42, var11, texture_render_types,
    )

    priorities = _decode_face_priorities(data, var31, var10, var13)
    alphas = _decode_face_alphas(data, var34, var10, var14)
    face_skins = _decode_face_skins(data, var32, var10, var15)
    skins = _decode_vertex_skins(data, var33, var9, var17)

    return ModelData(
        model_id=model_id,
        vertex_count=var9,
        face_count=var10,
        vertices_x=vx, vertices_y=vy, vertices_z=vz,
        face_a=fa, face_b=fb, face_c=fc,
        face_colors=colors,
        face_textures=face_textures,
        face_priorities=priorities,
        face_alphas=alphas,
        face_skins=face_skins,
        face_render_types=face_render_types,
        vertex_skins=skins,
        face_tex_coords=face_tex_coords,
        tex_u=tex_u,
        tex_v=tex_v,
        tex_w=tex_w,
        tex_face_count=var11,
    )


# --- HSL to RGB conversion ---


def hsl15_to_rgb(hsl: int) -> tuple[int, int, int]:
    """Convert OSRS 15-bit HSL face color to RGB.

    HSL packing: (hue_sat << 7) | lightness
    where hue_sat = hue * 8 + saturation (0..511)
    hue: 0..63 (6 bits), saturation: 0..7 (3 bits), lightness: 0..127 (7 bits)

    Reimplements Rasterizer3D.Rasterizer3D_buildPalette without brightness adjustment.
    """
    hue_sat = (hsl >> 7) & 0x1FF
    lightness = hsl & 0x7F

    hue_f = (hue_sat >> 3) / 64.0 + 0.0078125
    sat_f = (hue_sat & 7) / 8.0 + 0.0625
    light_f = lightness / 128.0

    # HSL to RGB (same algorithm as Rasterizer3D)
    r, g, b = light_f, light_f, light_f

    if sat_f != 0.0:
        if light_f < 0.5:
            q = light_f * (1.0 + sat_f)
        else:
            q = light_f + sat_f - light_f * sat_f

        p = 2.0 * light_f - q

        h_r = hue_f + 1.0 / 3.0
        if h_r > 1.0:
            h_r -= 1.0
        h_g = hue_f
        h_b = hue_f - 1.0 / 3.0
        if h_b < 0.0:
            h_b += 1.0

        r = _hue_to_channel(p, q, h_r)
        g = _hue_to_channel(p, q, h_g)
        b = _hue_to_channel(p, q, h_b)

    ri = min(255, int(r * 256.0))
    gi = min(255, int(g * 256.0))
    bi = min(255, int(b * 256.0))
    return (max(0, ri), max(0, gi), max(0, bi))


def _hue_to_channel(p: float, q: float, t: float) -> float:
    """HSL hue-to-channel helper (same as Rasterizer3D)."""
    if 6.0 * t < 1.0:
        return p + (q - p) * 6.0 * t
    if 2.0 * t < 1.0:
        return q
    if 3.0 * t < 2.0:
        return p + (q - p) * (2.0 / 3.0 - t) * 6.0
    return p


# --- binary output ---

MDLS_MAGIC = 0x4D444C53  # "MDLS" (v1)
MDL2_MAGIC = 0x4D444C32  # "MDL2" (v2, adds animation data)
MDL3_MAGIC = 0x4D444C33  # "MDL3" (v3, adds expanded texcoords)
MDL4_MAGIC = 0x4D444C34  # "MDL4" (v4, adds face alpha animation labels)
MUV1_MAGIC = 0x3156554D  # "MUV1" optional dynamic texture projection block


def _merge_models(models: list[ModelData]) -> ModelData:
    """Merge multiple ModelData into a single model (concatenate geometry).

    Face vertex indices are offset by accumulated vertex counts.
    Used for identity kit body parts that consist of multiple sub-models.
    """
    merged = ModelData(model_id=models[0].model_id)

    vert_offset = 0
    tex_offset = 0
    for md in models:
        merged.vertices_x.extend(md.vertices_x)
        merged.vertices_y.extend(md.vertices_y)
        merged.vertices_z.extend(md.vertices_z)

        merged.face_a.extend(a + vert_offset for a in md.face_a)
        merged.face_b.extend(b + vert_offset for b in md.face_b)
        merged.face_c.extend(c + vert_offset for c in md.face_c)
        merged.face_colors.extend(md.face_colors)
        for face_idx in range(md.face_count):
            merged.face_priorities.append(
                md.face_priorities[face_idx]
                if face_idx < len(md.face_priorities) else 0
            )
            merged.face_alphas.append(
                md.face_alphas[face_idx]
                if face_idx < len(md.face_alphas) else 0
            )
            merged.face_skins.append(
                md.face_skins[face_idx]
                if face_idx < len(md.face_skins) else 255
            )
            merged.face_render_types.append(
                md.face_render_types[face_idx]
                if face_idx < len(md.face_render_types) else 0
            )
            merged.face_textures.append(
                md.face_textures[face_idx]
                if face_idx < len(md.face_textures) else -1
            )
            coord = (
                md.face_tex_coords[face_idx]
                if face_idx < len(md.face_tex_coords) else -1
            )
            merged.face_tex_coords.append(coord + tex_offset if coord >= 0 else -1)
        merged.tex_u.extend(u + vert_offset for u in md.tex_u)
        merged.tex_v.extend(v + vert_offset for v in md.tex_v)
        merged.tex_w.extend(w + vert_offset for w in md.tex_w)
        merged.vertex_skins.extend(md.vertex_skins)

        vert_offset += md.vertex_count
        tex_offset += md.tex_face_count
        merged.vertex_count += md.vertex_count
        merged.face_count += md.face_count
        merged.tex_face_count += md.tex_face_count

    return merged


def _default_face_uvs() -> tuple[tuple[float, float, float], tuple[float, float, float]]:
    return (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)


def _projected_face_uvs(
    model: ModelData,
    face_idx: int,
) -> tuple[tuple[float, float, float], tuple[float, float, float]]:
    """Compute RuneLite-style texture UVs for one face."""
    if face_idx >= len(model.face_tex_coords):
        return _default_face_uvs()
    tex_coord = model.face_tex_coords[face_idx]
    if tex_coord < 0 or tex_coord >= len(model.tex_u):
        return _default_face_uvs()
    if face_idx >= len(model.face_a):
        return _default_face_uvs()

    tri_a = model.face_a[face_idx]
    tri_b = model.face_b[face_idx]
    tri_c = model.face_c[face_idx]
    tex_a = model.tex_u[tex_coord]
    tex_b = model.tex_v[tex_coord]
    tex_c = model.tex_w[tex_coord]
    max_vertex = model.vertex_count
    if (
        tri_a < 0 or tri_a >= max_vertex
        or tri_b < 0 or tri_b >= max_vertex
        or tri_c < 0 or tri_c >= max_vertex
        or tex_a < 0 or tex_a >= max_vertex
        or tex_b < 0 or tex_b >= max_vertex
        or tex_c < 0 or tex_c >= max_vertex
    ):
        return _default_face_uvs()

    v1x = float(model.vertices_x[tex_a])
    v1y = float(model.vertices_y[tex_a])
    v1z = float(model.vertices_z[tex_a])
    v2x = float(model.vertices_x[tex_b]) - v1x
    v2y = float(model.vertices_y[tex_b]) - v1y
    v2z = float(model.vertices_z[tex_b]) - v1z
    v3x = float(model.vertices_x[tex_c]) - v1x
    v3y = float(model.vertices_y[tex_c]) - v1y
    v3z = float(model.vertices_z[tex_c]) - v1z
    v4x = float(model.vertices_x[tri_a]) - v1x
    v4y = float(model.vertices_y[tri_a]) - v1y
    v4z = float(model.vertices_z[tri_a]) - v1z
    v5x = float(model.vertices_x[tri_b]) - v1x
    v5y = float(model.vertices_y[tri_b]) - v1y
    v5z = float(model.vertices_z[tri_b]) - v1z
    v6x = float(model.vertices_x[tri_c]) - v1x
    v6y = float(model.vertices_y[tri_c]) - v1y
    v6z = float(model.vertices_z[tri_c]) - v1z

    v7x = v2y * v3z - v2z * v3y
    v7y = v2z * v3x - v2x * v3z
    v7z = v2x * v3y - v2y * v3x

    v8x = v3y * v7z - v3z * v7y
    v8y = v3z * v7x - v3x * v7z
    v8z = v3x * v7y - v3y * v7x
    denom_u = v8x * v2x + v8y * v2y + v8z * v2z
    if abs(denom_u) < 1.0e-6:
        return _default_face_uvs()
    inv_u = 1.0 / denom_u
    u0 = (v8x * v4x + v8y * v4y + v8z * v4z) * inv_u
    u1 = (v8x * v5x + v8y * v5y + v8z * v5z) * inv_u
    u2 = (v8x * v6x + v8y * v6y + v8z * v6z) * inv_u

    v8x = v2y * v7z - v2z * v7y
    v8y = v2z * v7x - v2x * v7z
    v8z = v2x * v7y - v2y * v7x
    denom_v = v8x * v3x + v8y * v3y + v8z * v3z
    if abs(denom_v) < 1.0e-6:
        return _default_face_uvs()
    inv_v = 1.0 / denom_v
    v0 = (v8x * v4x + v8y * v4y + v8z * v4z) * inv_v
    v1 = (v8x * v5x + v8y * v5y + v8z * v5z) * inv_v
    v2 = (v8x * v6x + v8y * v6y + v8z * v6z) * inv_v

    if not all(math.isfinite(value) for value in (u0, u1, u2, v0, v1, v2)):
        return _default_face_uvs()
    return (u0, u1, u2), (v0, v1, v2)


def _clamp_uv_component(value: float) -> float:
    if value < 0.0:
        return 0.0
    if value > 1.0:
        return 1.0
    return value


def _repeat_uv_component_with_margin(value: float, margin: float) -> float:
    if margin <= 0.0:
        return _clamp_uv_component(value)
    while value < -margin:
        value += 1.0
    while value > 1.0 + margin:
        value -= 1.0
    if value < -margin:
        return -margin
    if value > 1.0 + margin:
        return 1.0 + margin
    return value


def _atlas_face_uv(atlas: TextureAtlas, tex_id: int, u: float, v: float) -> tuple[float, float]:
    u_off, v_off, u_size, v_size = atlas.uv_map[tex_id]
    eps_u = 0.5 / max(1, atlas.width)
    eps_v = 0.5 / max(1, atlas.height)
    usable_u = max(0.0, u_size - eps_u * 2.0)
    usable_v = max(0.0, v_size - eps_v * 2.0)
    u = _clamp_uv_component(u)
    v = _repeat_uv_component_with_margin(v, float(getattr(atlas, "repeat_v_margin", 0.0)))
    return (
        u_off + eps_u + u * usable_u,
        v_off + eps_v + v * usable_v,
    )


def _atlas_face_uv_params(atlas: TextureAtlas, tex_id: int) -> tuple[float, float, float, float, float]:
    u_off, v_off, u_size, v_size = atlas.uv_map[tex_id]
    eps_u = 0.5 / max(1, atlas.width)
    eps_v = 0.5 / max(1, atlas.height)
    usable_u = max(0.0, u_size - eps_u * 2.0)
    usable_v = max(0.0, v_size - eps_v * 2.0)
    return (
        u_off + eps_u,
        v_off + eps_v,
        usable_u,
        usable_v,
        float(getattr(atlas, "repeat_v_margin", 0.0)),
    )


def expand_model(
    model: ModelData,
    tex_colors: dict[int, int] | None = None,
    atlas: TextureAtlas | None = None,
    bake_priority_offsets: bool = False,
) -> tuple[list[float], list[tuple[int, int, int, int]], list[float]]:
    """Expand indexed model to per-vertex (3 verts per face, no index buffer).

    Returns (flat_vertices[face_count*3*3], colors[face_count*3], uvs[face_count*3*2]).
    Each color is (r, g, b, 255).

    When atlas is provided:
    - Textured faces get UV coordinates mapped to atlas slots, vertex color = white
    - Non-textured faces get UV pointing to atlas white pixel, vertex color = HSL
    When atlas is None (legacy path):
    - Textured faces use averageRGB from tex_colors as vertex color
    - UVs are all zeros
    """
    verts: list[float] = []
    colors: list[tuple[int, int, int, int]] = []
    uvs: list[float] = []

    # compute minimum priority so we only offset faces that differ from baseline.
    # a model with all faces at priority 10 needs zero offset (no coplanar conflict).
    min_pri = min(model.face_priorities) if model.face_priorities else 0

    for fi in range(model.face_count):
        a = model.face_a[fi]
        b = model.face_b[fi]
        c = model.face_c[fi]
        alpha = (
            max(0, min(255, 255 - model.face_alphas[fi]))
            if fi < len(model.face_alphas)
            else 255
        )

        # bounds check
        if (
            a < 0
            or a >= model.vertex_count
            or b < 0
            or b >= model.vertex_count
            or c < 0
            or c >= model.vertex_count
        ):
            # degenerate face, emit zero triangle
            verts.extend([0.0] * 9)
            colors.extend([(128, 128, 128, 255)] * 3)
            uvs.extend([0.0] * 6)
            continue

        # vertex positions (OSRS Y is negative-up, we negate)
        ax, ay, az = float(model.vertices_x[a]), float(-model.vertices_y[a]), float(model.vertices_z[a])
        bx, by, bz = float(model.vertices_x[b]), float(-model.vertices_y[b]), float(model.vertices_z[b])
        cx, cy, cz = float(model.vertices_x[c]), float(-model.vertices_y[c]), float(model.vertices_z[c])

        pri = model.face_priorities[fi] if fi < len(model.face_priorities) else 0
        pri_delta = pri - min_pri
        if bake_priority_offsets and pri_delta > 0:
            e1x, e1y, e1z = bx - ax, by - ay, bz - az
            e2x, e2y, e2z = cx - ax, cy - ay, cz - az
            nx = e1y * e2z - e1z * e2y
            ny = e1z * e2x - e1x * e2z
            nz = e1x * e2y - e1y * e2x
            length = math.sqrt(nx * nx + ny * ny + nz * nz)
            if length > 0.001:
                bias = pri_delta * 0.15 / length
                nx *= bias
                ny *= bias
                nz *= bias
                ax -= nx
                ay -= ny
                az -= nz
                bx -= nx
                by -= ny
                bz -= nz
                cx -= nx
                cy -= ny
                cz -= nz

        verts.extend([ax, ay, az, bx, by, bz, cx, cy, cz])

        tex_id = (
            model.face_textures[fi]
            if fi < len(model.face_textures)
            else -1
        )

        if atlas and tex_id >= 0 and tex_id in atlas.uv_map:
            u_face, v_face = _projected_face_uvs(model, fi)
            uv_a = _atlas_face_uv(atlas, tex_id, u_face[0], v_face[0])
            uv_b = _atlas_face_uv(atlas, tex_id, u_face[1], v_face[1])
            uv_c = _atlas_face_uv(atlas, tex_id, u_face[2], v_face[2])
            uvs.extend([uv_a[0], uv_a[1], uv_b[0], uv_b[1], uv_c[0], uv_c[1]])

            colors.extend([(255, 255, 255, alpha)] * 3)
        else:
            if atlas:
                uvs.extend([atlas.white_u, atlas.white_v] * 3)
            else:
                uvs.extend([0.0] * 6)

            if atlas and tex_id >= 0:
                colors.extend([(255, 0, 255, alpha)] * 3)
                continue

            if tex_id >= 0 and tex_colors and tex_id in tex_colors:
                hsl = tex_colors[tex_id]
            else:
                hsl = model.face_colors[fi] if fi < len(model.face_colors) else 0
            r, g, b_col = hsl15_to_rgb(hsl)
            color = (r, g, b_col, alpha)
            colors.extend([color, color, color])

    return verts, colors, uvs


def _write_atlas_binary(path: Path, atlas: TextureAtlas) -> None:
    """Write an ATLS atlas companion for textured MDL3 model sets."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as f:
        f.write(struct.pack("<I", 0x41544C53))
        f.write(struct.pack("<I", atlas.width))
        f.write(struct.pack("<I", atlas.height))
        f.write(atlas.pixels)


def write_models_binary(
    output_path: Path,
    models: list[ModelData],
    tex_colors: dict[int, int] | None = None,
    atlas: TextureAtlas | None = None,
    atlas_path: Path | None = None,
    bake_priority_offsets: bool = False,
) -> None:
    """Write models to .models binary format.

    V2 format adds animation data per model:
      - base vertex positions (indexed, pre-animation reference pose)
      - vertex skin labels (label group per base vertex, for animation transforms)
      - face indices (triangle index buffer into base vertices)

    Per-model binary layout:
      uint32 model_id
      uint16 expanded_vert_count    (face_count * 3, for rendering)
      uint16 face_count
      uint16 base_vert_count        (original indexed vertex count)
      float  expanded_verts[expanded_vert_count * 3]   (x,y,z)
      uint8  colors[expanded_vert_count * 4]            (r,g,b,a)
      float  texcoords[expanded_vert_count * 2]         (MDL3/MDL4 only)
      int16  base_verts[base_vert_count * 3]            (x,y,z — original OSRS coords, y NOT negated)
      uint8  vertex_skins[base_vert_count]              (label group per vertex)
      uint16 face_indices[face_count * 3]               (a,b,c per face)
      uint8  face_priorities[face_count]
      uint8  face_alphas[face_count]                    (MDL4 only)
      uint8  face_skins[face_count]                     (MDL4 only, 255 = none)
    """
    output_path.parent.mkdir(parents=True, exist_ok=True)
    has_textures = atlas is not None
    magic = MDL4_MAGIC if has_textures else MDL2_MAGIC

    if atlas is not None:
        _write_atlas_binary(atlas_path or output_path.with_suffix(".atlas"), atlas)

    with open(output_path, "wb") as f:
        # header
        f.write(struct.pack("<I", magic))
        f.write(struct.pack("<I", len(models)))

        # placeholder for offsets table
        offsets_pos = f.tell()
        f.write(b"\x00" * (len(models) * 4))

        offsets = []
        for model in models:
            offsets.append(f.tell())

            verts, colors, uvs = expand_model(
                model,
                tex_colors=tex_colors,
                atlas=atlas,
                bake_priority_offsets=bake_priority_offsets,
            )
            expanded_vert_count = model.face_count * 3

            # per-model header
            f.write(struct.pack("<I", model.model_id))
            f.write(struct.pack("<H", expanded_vert_count))
            f.write(struct.pack("<H", model.face_count))
            f.write(struct.pack("<H", model.vertex_count))

            # expanded vertices for rendering (float32 x3 per vertex)
            for v in verts:
                f.write(struct.pack("<f", v))

            # colors (uint8 x4 per expanded vertex)
            for r, g, b, a in colors:
                f.write(struct.pack("BBBB", r, g, b, a))

            if has_textures:
                for uv in uvs:
                    f.write(struct.pack("<f", uv))

            # base vertex positions (int16 x3, original OSRS coords)
            for i in range(model.vertex_count):
                x = max(-32768, min(32767, model.vertices_x[i]))
                y = max(-32768, min(32767, model.vertices_y[i]))
                z = max(-32768, min(32767, model.vertices_z[i]))
                f.write(struct.pack("<hhh", x, y, z))

            # vertex skins (uint8 per base vertex)
            skins = model.vertex_skins or [0] * model.vertex_count
            for s in skins:
                f.write(struct.pack("B", s))

            # face indices (uint16 x3 per face)
            for i in range(model.face_count):
                a = max(0, min(65535, model.face_a[i]))
                b = max(0, min(65535, model.face_b[i]))
                c = max(0, min(65535, model.face_c[i]))
                f.write(struct.pack("<HHH", a, b, c))

            # face render priorities (uint8 per face, for z-fighting resolution)
            priorities = model.face_priorities or [0] * model.face_count
            for i in range(model.face_count):
                pri = priorities[i] if i < len(priorities) else 0
                f.write(struct.pack("B", pri))

            if magic == MDL4_MAGIC:
                face_alphas = model.face_alphas or [0] * model.face_count
                face_skins = model.face_skins or [255] * model.face_count
                for i in range(model.face_count):
                    alpha = face_alphas[i] if i < len(face_alphas) else 0
                    f.write(struct.pack("B", max(0, min(255, alpha))))
                for i in range(model.face_count):
                    skin = face_skins[i] if i < len(face_skins) else 255
                    f.write(struct.pack("B", max(0, min(255, skin))))

            if has_textures and atlas is not None:
                f.write(struct.pack("<II", MUV1_MAGIC, model.face_count))
                for i in range(model.face_count):
                    tex_id = (
                        model.face_textures[i]
                        if i < len(model.face_textures) else -1
                    )
                    if tex_id < 0 or tex_id not in atlas.uv_map:
                        f.write(struct.pack(
                            "<BxxxHHHfffff",
                            0,
                            0,
                            0,
                            0,
                            0.0,
                            0.0,
                            0.0,
                            0.0,
                            0.0,
                        ))
                        continue

                    coord = (
                        model.face_tex_coords[i]
                        if i < len(model.face_tex_coords) else -1
                    )
                    if 0 <= coord < len(model.tex_u):
                        tex_a = model.tex_u[coord]
                        tex_b = model.tex_v[coord]
                        tex_c = model.tex_w[coord]
                    else:
                        tex_a = model.face_a[i]
                        tex_b = model.face_b[i]
                        tex_c = model.face_c[i]

                    if (
                        tex_a < 0 or tex_a > 0xFFFF
                        or tex_b < 0 or tex_b > 0xFFFF
                        or tex_c < 0 or tex_c > 0xFFFF
                    ):
                        f.write(struct.pack(
                            "<BxxxHHHfffff",
                            0,
                            0,
                            0,
                            0,
                            0.0,
                            0.0,
                            0.0,
                            0.0,
                            0.0,
                        ))
                        continue

                    u_base, v_base, u_scale, v_scale, repeat_v_margin = (
                        _atlas_face_uv_params(atlas, tex_id)
                    )
                    f.write(struct.pack(
                        "<BxxxHHHfffff",
                        1,
                        tex_a,
                        tex_b,
                        tex_c,
                        u_base,
                        v_base,
                        u_scale,
                        v_scale,
                        repeat_v_margin,
                    ))

        # go back and write offsets
        f.seek(offsets_pos)
        for off in offsets:
            f.write(struct.pack("<I", off))


def write_item_model_header(
    output_path: Path,
    mappings: list[tuple[int, int, int, int, int, int, int, int]],
) -> None:
    """Write C header with item → model ID mapping.

    Each entry carries cache wear positions, render flags, and hide masks.
    """
    output_path.parent.mkdir(parents=True, exist_ok=True)

    with open(output_path, "w") as f:
        f.write("/* generated by scripts/export_models.py — do not edit */\n")
        f.write("#ifndef ITEM_MODELS_H\n")
        f.write("#define ITEM_MODELS_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write("#define ITEM_RENDER_MODEL_MISSING 0xFFFFFFFFu\n")
        f.write("#define ITEM_RENDER_FLAG_TWO_HANDED 1u\n")
        f.write("#define ITEM_RENDER_FLAG_WEARPOS_AUTHORITY 2u\n\n")
        f.write("typedef struct {\n")
        f.write("    uint16_t item_id;\n")
        f.write("    uint32_t inv_model;\n")
        f.write("    uint32_t wield_model;\n")
        f.write("    uint32_t hide_body_mask;\n")
        f.write("    uint32_t wearpos1;\n")
        f.write("    uint32_t wearpos2;\n")
        f.write("    uint32_t wearpos3;\n")
        f.write("    uint32_t render_flags;\n")
        f.write("    uint32_t ready_anim_id;\n")
        f.write("    uint32_t walk_anim_id;\n")
        f.write("    uint32_t run_anim_id;\n")
        f.write("} ItemModelMapping;\n\n")
        f.write(
            f"#define ITEM_MODEL_COUNT {len(mappings)}\n\n"
        )
        f.write(
            "static const ItemModelMapping ITEM_MODEL_MAP[] = {\n"
        )
        for (
            item_id, inv, wield, hide_mask, wearpos1, wearpos2, wearpos3, flags,
            ready_anim, walk_anim, run_anim
        ) in mappings:
            f.write(
                f"    {{ {item_id}, {inv}, {wield}, {hide_mask}, "
                f"{wearpos1}, {wearpos2}, {wearpos3}, {flags}, "
                f"{ready_anim}, {walk_anim}, {run_anim} }},\n"
            )
        f.write("};\n\n")
        f.write("#endif /* ITEM_MODELS_H */\n")


def write_player_model_header(
    output_path: Path,
    body_part_model_ids: dict[int, int],
    item_mappings: list[tuple[int, int, int, int, int, int, int, int]],
    item_defs: dict[int, ItemDef],
) -> None:
    """Write C header for player model rendering.

    Provides:
    - Default body part model IDs (from identity kits)
    - Item ID to wield model lookup (for equipped items)
    - Equipment slot coverage info (which body parts to hide)
    """
    output_path.parent.mkdir(parents=True, exist_ok=True)

    with open(output_path, "w") as f:
        f.write("/* generated by scripts/export_models.py — do not edit */\n")
        f.write("#ifndef PLAYER_MODELS_H\n")
        f.write("#define PLAYER_MODELS_H\n\n")
        f.write("#include <stdint.h>\n\n")

        f.write("/* body part indices (male) */\n")
        for i, name in enumerate(BODY_PART_NAMES):
            f.write(f"#define BODY_PART_{name} {i}\n")
        f.write(f"#define BODY_PART_COUNT {len(BODY_PART_NAMES)}\n\n")

        f.write("/* default male body part model IDs (synthetic: 0xF0000 + part_id) */\n")
        f.write("static const uint32_t DEFAULT_BODY_MODELS[BODY_PART_COUNT] = {\n")
        for i in range(len(BODY_PART_NAMES)):
            mid = body_part_model_ids.get(i, 0xFFFFFFFF)
            f.write(f"    0x{mid:X},  /* {BODY_PART_NAMES[i]} */\n")
        f.write("};\n\n")

        f.write("#endif /* PLAYER_MODELS_H */\n")


# --- item IDs from our simulation (osrs_items.h) ---

SIM_ITEM_IDS = [
    10828,  # Helm of Neitiznot
    21795,  # Imbued god cape
    1712,   # Amulet of glory
    2503,   # Black d'hide body
    4091,   # Mystic robe top
    1079,   # Rune platelegs
    4093,   # Mystic robe bottom
    4151,   # Abyssal whip
    9185,   # Rune crossbow
    4710,   # Ahrim's staff
    5698,   # Dragon dagger
    12954,  # Dragon defender
    12829,  # Spirit shield
    7462,   # Barrows gloves
    3105,   # Climbing boots
    6737,   # Berserker ring
    9243,   # Diamond bolts (e)
    22324,  # Ghrazi rapier
    24417,  # Inquisitor's mace
    11791,  # Staff of the dead
    21006,  # Kodai wand
    24424,  # Volatile nightmare staff
    13867,  # Zuriel's staff
    11785,  # Armadyl crossbow
    26374,  # Zaryte crossbow
    13652,  # Dragon claws
    11802,  # Armadyl godsword
    25730,  # Ancient godsword
    4153,   # Granite maul
    21003,  # Elder maul
    11235,  # Dark bow
    19481,  # Heavy ballista
    22613,  # Vesta's longsword
    27690,  # Voidwaker
    22622,  # Statius's warhammer
    22636,  # Morrigan's javelin
    21018,  # Ancestral hat
    21021,  # Ancestral robe top
    21024,  # Ancestral robe bottom
    4712,   # Ahrim's robetop
    4714,   # Ahrim's robeskirt
    4736,   # Karil's leathertop
    11834,  # Bandos tassets
    12831,  # Blessed spirit shield
    6585,   # Amulet of fury
    12002,  # Occult necklace
    21295,  # Infernal cape
    13235,  # Eternal boots
    11770,  # Seers ring (i)
    25975,  # Lightbearer
    6889,   # Mage's book
    11212,  # Dragon arrows
    4751,   # Torag's platelegs
    4722,   # Dharok's platelegs
    4759,   # Verac's plateskirt
    4745,   # Torag's helm
    4716,   # Dharok's helm
    4753,   # Verac's helm
    4724,   # Guthan's helm
    21932,  # Opal dragon bolts (e)
    # --- Zulrah encounter items ---
    # tier 2 (BIS)
    21791,  # Imbued saradomin cape
    31113,  # Eye of ayak (charged)
    27251,  # Elidinis' ward (f)
    31106,  # Confliction gauntlets
    31097,  # Avernic treads (max)
    20657,  # Ring of suffering (ri)
    20997,  # Twisted bow
    27235,  # Masori mask (f)
    27238,  # Masori body (f)
    27241,  # Masori chaps (f)
    19547,  # Necklace of anguish
    28947,  # Dizana's quiver (uncharged)
    26235,  # Zaryte vambraces
    12926,  # Toxic blowpipe
    # tier 1 (mid)
    4708,   # Ahrim's hood
    19544,  # Tormented bracelet
    22481,  # Sanguinesti staff
    6920,   # Infinity boots
    20220,  # Holy blessing (god blessing)
    2550,   # Ring of recoil
    23971,  # Crystal helm
    22109,  # Ava's assembler
    23975,  # Crystal body
    23979,  # Crystal legs
    25865,  # Bow of faerdhinen (c)
    30070,  # Dragon hunter wand
    28945,  # Echo boots
    19921,  # Blessed d'hide boots
    # tier 0 (budget)
    4089,   # Mystic hat
    12899,  # Trident of the swamp
    12612,  # Book of darkness
    21326,  # Amethyst arrow
    4097,   # Mystic boots
    10382,  # Blessed coif (Guthix)
    2497,   # Black d'hide chaps
    12788,  # Magic shortbow (i)
    10499,  # Ava's accumulator
    # --- Inferno encounter items ---
    22326,  # Justiciar faceguard
    22327,  # Justiciar chestguard
    22328,  # Justiciar legguards
    4224,   # Crystal shield
    13237,  # Pegasian boots
    12817,  # Elysian spirit shield
    26243,  # Virtus robe top
    26245,  # Virtus robe bottom
    28310,  # Venator ring
]


def main() -> None:
    default_data_dir = Path(__file__).resolve().parents[1] / "data"
    parser = argparse.ArgumentParser(
        description="export OSRS 3D models from modern OpenRS2 cache"
    )
    parser.add_argument(
        "--modern-cache",
        type=Path,
        required=True,
        help="path to modern OpenRS2 flat file cache directory",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=default_data_dir / "equipment.models",
        help="output .models binary file",
    )
    parser.add_argument(
        "--header",
        type=Path,
        default=default_data_dir / "item_models.h",
        help="output C header with item→model mapping",
    )
    parser.add_argument(
        "--runec-item-render-map",
        type=Path,
        default=Path("refs/RuneC/data/models/item_render.map"),
        help="optional RuneC item render map for ready, walk, and run anim metadata",
    )
    parser.add_argument(
        "--extra-models",
        type=str,
        default=None,
        help="comma-separated extra model IDs to export (e.g. NPC models)",
    )
    args = parser.parse_args()

    cache_path = args.modern_cache
    if not cache_path.exists():
        sys.exit(f"cache directory not found: {cache_path}")

    print(f"reading modern cache from {cache_path}")
    modern_reader = ModernCacheReader(cache_path)

    print("loading item definitions...")
    item_defs = decode_item_definitions_modern(modern_reader)
    print(f"  loaded {len(item_defs)} item definitions")
    item_render_anims = load_runec_item_render_anims(args.runec_item_render_map)

    # build per-item wield models with recolors + maleWield2 merged
    print("building wield models...")
    needed_models: set[int] = set()  # raw inv models (no recolor needed for inventory icons)
    mappings: list[tuple[int, int, int, int, int, int, int, int, int, int, int]] = []
    wield_models: list[ModelData] = []  # recolored + merged wield models

    def _load_model(mid: int) -> ModelData | None:
        raw_m = load_model_modern(modern_reader, mid)
        if raw_m is None:
            return None
        return decode_model(mid, raw_m)

    for idx, item_id in enumerate(SIM_ITEM_IDS):
        item = item_defs.get(item_id)
        if item is None:
            print(f"  warning: item {item_id} not found in cache")
            continue

        inv = item.inv_model if item.inv_model >= 0 else 0xFFFFFFFF
        if inv != 0xFFFFFFFF:
            needed_models.add(inv)

        # build wield model: merge maleWield + maleWield2, apply recolors
        wield_synth = 0xFFFFFFFF
        wield_parts: list[ModelData] = []
        if item.male_wield >= 0:
            md = _load_model(item.male_wield)
            if md:
                wield_parts.append(md)
        if item.male_wield2 >= 0:
            md2 = _load_model(item.male_wield2)
            if md2:
                wield_parts.append(md2)
        if item.male_wield3 >= 0:
            md3 = _load_model(item.male_wield3)
            if md3:
                wield_parts.append(md3)

        if wield_parts:
            if len(wield_parts) == 1:
                merged = wield_parts[0]
            else:
                merged = _merge_models(wield_parts)

            # apply item recolors
            for src, dst in zip(item.recolor_src, item.recolor_dst):
                for fi in range(merged.face_count):
                    if merged.face_colors[fi] == src:
                        merged.face_colors[fi] = dst

            wield_synth = 0xE0000 + idx
            merged.model_id = wield_synth
            wield_models.append(merged)

        wearpos1, wearpos2, wearpos3 = item_wearpos_values(item)
        ready_anim, walk_anim, run_anim = item_render_anims.get(item_id, (-1, -1, -1))
        mappings.append((
            item_id,
            inv,
            wield_synth,
            item_hide_body_mask(item),
            wearpos1,
            wearpos2,
            wearpos3,
            item_render_flags(item),
            item_render_anim_value(ready_anim),
            item_render_anim_value(walk_anim),
            item_render_anim_value(run_anim),
        ))
        rc_info = f", {len(item.recolor_src)} recolors" if item.recolor_src else ""
        w2_info = f" + wield2={item.male_wield2}" if item.male_wield2 >= 0 else ""
        w3_info = f" + wield3={item.male_wield3}" if item.male_wield3 >= 0 else ""
        print(
            f"  {item.name} (id={item_id}): inv={inv}, "
            f"wield={item.male_wield}{w2_info}{w3_info}{rc_info}"
        )

    # decode identity kits for player body parts
    print("loading identity kits...")
    idk_defs = decode_identity_kits_modern(modern_reader)
    print(f"  loaded {len(idk_defs)} identity kits")

    # merge body part sub-models into single models for each default kit
    # uses synthetic model IDs 0xF0000 + body_part_id to avoid collisions
    body_part_model_ids: dict[int, int] = {}  # body_part_id -> synthetic model_id
    body_models: list[ModelData] = []

    for body_part_id, kit_idx in sorted(DEFAULT_MALE_KITS.items()):
        kit = idk_defs.get(kit_idx)
        if kit is None or not kit.body_models:
            print(f"  warning: kit {kit_idx} ({BODY_PART_NAMES[body_part_id]}) has no body models")
            continue

        # decode and merge sub-models
        sub_models: list[ModelData] = []
        for mid in kit.body_models:
            md = _load_model(mid)
            if md:
                sub_models.append(md)

        if not sub_models:
            print(f"  warning: could not decode body models for kit {kit_idx}")
            continue

        # merge into single model
        if len(sub_models) == 1:
            merged = sub_models[0]
        else:
            merged = _merge_models(sub_models)

        # apply kit recolors
        for i in range(6):
            if kit.original_colors[i] == 0:
                break
            for fi in range(merged.face_count):
                if merged.face_colors[fi] == kit.original_colors[i]:
                    merged.face_colors[fi] = kit.replacement_colors[i]

        # assign synthetic model ID
        synth_id = 0xF0000 + body_part_id
        merged.model_id = synth_id
        body_part_model_ids[body_part_id] = synth_id
        body_models.append(merged)
        print(f"  {BODY_PART_NAMES[body_part_id]}: kit {kit_idx}, "
              f"{len(kit.body_models)} sub-models -> {merged.vertex_count} verts")

    # add spotanim models (spell effects, projectiles)
    # parsed from spotanim.dat — model IDs for GFX 27/368/369/377/1468
    SPOTANIM_MODELS = {3080, 3135, 6375, 6381, 14215}
    needed_models |= SPOTANIM_MODELS
    print(f"added {len(SPOTANIM_MODELS)} spotanim models")

    # zulrah encounter: NPC models, snakelings, projectiles, clouds
    # from data/npc_models.h model IDs
    ENCOUNTER_MODELS = {
        14407,  # blue zulrah (magic form)
        14408,  # green zulrah (ranged form)
        14409,  # red zulrah (melee form)
        10415,  # snakeling
        20390,  # GFX 1044 ranged projectile (zulrah)
        11221,  # GFX 1045 cloud projectile
        26593,  # GFX 1046 magic projectile (zulrah)
        4086,   # object 11700 toxic cloud on ground
        # inferno pillars — "Rocky support" objects 30284-30287 (4 HP levels)
        33044,  # object 30284 — Rocky support (100% HP)
        33043,  # object 30285 — Rocky support (75% HP)
        33042,  # object 30286 — Rocky support (50% HP)
        33045,  # object 30287 — Rocky support (25% HP)
        # player weapon projectiles
        20825,  # GFX 1040 trident of swamp projectile
        20824,  # GFX 1042 trident impact
        20823,  # GFX 665 trident casting
        3136,   # GFX 15 rune arrow projectile
        26377,  # GFX 1120 dragon arrow projectile
        26379,  # GFX 1122 dragon dart projectile (blowpipe)
        3131,   # GFX 231 rune dart projectile
        29421,  # GFX 1043 blowpipe special attack
    }
    needed_models |= ENCOUNTER_MODELS
    print(f"added {len(ENCOUNTER_MODELS)} encounter NPC/projectile models")

    # add extra models from CLI (NPC models, etc.)
    if args.extra_models:
        extra = {int(x.strip()) for x in args.extra_models.split(",")}
        needed_models |= extra
        print(f"added {len(extra)} extra models from --extra-models: {sorted(extra)}")

    # dragon bolt (GFX 1468) is model 3135 with recolors: 41->1692, 61->670, 57->1825
    # build it as a synthetic recolored model like wield models
    dragon_bolt_base = _load_model(3135)
    if dragon_bolt_base:
        for src, dst in [(41, 1692), (61, 670), (57, 1825)]:
            for fi in range(dragon_bolt_base.face_count):
                if dragon_bolt_base.face_colors[fi] == src:
                    dragon_bolt_base.face_colors[fi] = dst
        dragon_bolt_base.model_id = 0xD0001  # synthetic ID for dragon bolt
        wield_models.append(dragon_bolt_base)
        print("  built dragon bolt model (recolored 3135 -> 0xD0001)")

    print(f"\n{len(needed_models)} unique equipment + spotanim models to export")

    # read and decode models from cache index 1
    decoded_models: list[ModelData] = []
    errors = 0

    for model_id in sorted(needed_models):
        raw = load_model_modern(modern_reader, model_id)

        if raw is None:
            print(f"  warning: model {model_id} not in cache")
            errors += 1
            continue

        model = decode_model(model_id, raw)
        if model is None:
            print(f"  warning: failed to decode model {model_id}")
            errors += 1
            continue

        decoded_models.append(model)

    # combine body models + wield models (recolored) + inv models (raw)
    all_models = body_models + wield_models + decoded_models

    print(
        f"\ndecoded {len(decoded_models)} inv + {len(wield_models)} wield "
        f"+ {len(body_models)} body models, {errors} errors"
    )

    # print stats
    total_verts = sum(m.vertex_count for m in all_models)
    total_faces = sum(m.face_count for m in all_models)
    print(f"total: {total_verts} vertices, {total_faces} faces")

    # write binary output (body + equipment models)
    print("loading texture atlas...")
    tex_colors = load_texture_average_colors(modern_reader)
    texture_defs = load_texture_definitions(modern_reader)
    atlas = build_atlas(load_texture_sprites(modern_reader), repeat_v_padding=128)
    write_models_binary(args.output, all_models, tex_colors=tex_colors, atlas=atlas)
    write_texture_anim_binary(args.output.with_suffix(".tanim"), atlas, texture_defs)
    file_size = args.output.stat().st_size
    print(f"\nwrote {file_size:,} bytes to {args.output}")

    # write C headers
    write_item_model_header(args.header, mappings)
    print(f"wrote {args.header}")

    write_player_model_header(
        args.header.parent / "player_models.h",
        body_part_model_ids,
        mappings,
        item_defs,
    )
    print(f"wrote {args.header.parent / 'player_models.h'}")


if __name__ == "__main__":
    main()
