#!/usr/bin/env python3
"""Export Fortis Colosseum item models and inventory sprites.

The target item set is derived from the Colosseum loadout arrays, then merged
with the existing render header so synthetic model ids stay stable. The modern
cache supplies item definitions, inventory models, equipped model parts,
recolors, retextures, identity kits, and RuneLite-style inventory sprites.
"""

from __future__ import annotations

import argparse
import os
import re
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path


def _find_cache_pipeline() -> Path:
    """Locate the vendored cache pipeline inside the tracked tree."""
    env_override = os.environ.get("OSRS_CACHE_PIPELINE")
    candidates: list[Path] = []
    if env_override:
        candidates.append(Path(env_override))
    repo_root = Path(__file__).resolve().parents[3]
    candidates.append(repo_root / "ocean" / "osrs" / "tools" / "cache_pipeline")
    for candidate in candidates:
        if candidate.is_dir():
            return candidate
    searched = "\n  ".join(str(candidate) for candidate in candidates)
    raise SystemExit(f"export_colosseum_items: cache pipeline not found, searched:\n  {searched}")


CACHE_PIPELINE = _find_cache_pipeline()
sys.path.insert(0, str(CACHE_PIPELINE))

from export_item_render_models import (  # noqa: E402
    BODY_ARMS,
    BODY_FEET,
    BODY_HAIR,
    BODY_HANDS,
    BODY_JAW,
    BODY_LEGS,
    BODY_TORSO,
    RENDER_FLAG_TWO_HANDED,
    RENDER_FLAG_WEARPOS_AUTHORITY,
    WEARPOS_ARMS,
    WEARPOS_FEET,
    WEARPOS_HANDS,
    WEARPOS_HEAD,
    WEARPOS_JAW,
    WEARPOS_LEFT_HAND,
    WEARPOS_LEGS,
    WEARPOS_TORSO,
    apply_appearance_overrides,
    parse_identity_kit,
    translate_model_y,
)
from export_models import (  # noqa: E402
    BODY_PART_NAMES,
    DEFAULT_MALE_KITS,
    ModelData,
    _merge_models,
    decode_model,
    expand_model,
    load_model_modern,
)
from export_terrain import load_texture_average_colors_modern  # noqa: E402
from export_textures import TextureAtlas  # noqa: E402
from modern_cache_reader import ModernCacheReader  # noqa: E402
from rc_cache import RcCacheStore, decode_sprite_group  # noqa: E402

try:
    from PIL import Image
except ImportError as exc:  # pragma: no cover
    raise SystemExit("export_colosseum_items: Pillow is required for modifier sprites") from exc


MODERN_OBJ_GROUP = 10
MODERN_IDK_GROUP = 3
MISSING_U32 = 0xFFFFFFFF
BODY_MODEL_BASE = 0xF0000
ITEM_MODEL_BASE = 0xE0000
DRAGON_BOLT_MODEL_ID = 0xD0001

SPOTANIM_MODELS = {3080, 3135, 6375, 6381, 14215}
COLOSSEUM_WATER_SURGE_MODELS = {3116, 34617, 34618}

COLOSSEUM_NON_CONSUMABLE_DISPLAY_ITEM_IDS = [
    12006,
    27281,
]
COLOSSEUM_RUNTIME_EQUIPPABLE_ITEM_IDS = [
    27610,
    12006,
]
ENCOUNTER_MODELS = {
    14407,
    14408,
    14409,
    10415,
    20390,
    11221,
    26593,
    4086,
    33044,
    33043,
    33042,
    33045,
    20825,
    20824,
    20823,
    3136,
    26379,
    3131,
    29421,
}

COLOSSEUM_MODIFIER_SPRITE_IDS_BY_MODIFIER = {
    "bees": (5544, 5559, 5574),
    "blasphemy": (5538, 5553, 5568),
    "doom": (5543, 5558, 5573),
    "dynamic_duo": (5545,),
    "frailty": (5541, 5556, 5571),
    "mantimayhem": (5539, 5554, 5569),
    "myopia": (5547, 5562, 5577),
    "reentry": (5536, 5551, 5566),
    "red_flag": (5540,),
    "relentless": (5535, 5550, 5565),
    "solarflare": (5537, 5552, 5567),
    "quartet": (5546,),
    "totemic": (5542,),
    "volatility": (5534, 5549, 5564),
}

COLOSSEUM_MODIFIER_SPRITE_IDS = sorted({
    sprite_id
    for tiers in COLOSSEUM_MODIFIER_SPRITE_IDS_BY_MODIFIER.values()
    for sprite_id in tiers
})

SLOT_NAMES = {
    "SLOT_HEAD": 0,
    "SLOT_CAPE": 1,
    "SLOT_NECK": 2,
    "SLOT_WEAPON": 3,
    "SLOT_BODY": 4,
    "SLOT_SHIELD": 5,
    "SLOT_LEGS": 6,
    "SLOT_HANDS": 7,
    "SLOT_FEET": 8,
    "SLOT_RING": 9,
    "SLOT_AMMO": 10,
}

KNOWN_PLAYER_BAS = {
    11802: (7053, 7052, 7043),
    11804: (7053, 7052, 7043),
    11806: (7053, 7052, 7043),
    11808: (7053, 7052, 7043),
    26233: (7053, 7052, 7043),
    4153: (1662, 1663, 1664),
    19481: (7220, 7223, 7221),
    27690: (244, 247, 248),
}


@dataclass(frozen=True)
class GameItemInfo:
    """Gameplay item metadata from osrs_items_generated.h."""

    symbol: str
    item_id: int
    name: str
    equip_slot: int
    two_handed: bool


@dataclass
class CacheItemDef:
    """Modern cache item definition fields used for rendering."""

    item_id: int
    name: str = ""
    inv_model: int = -1
    zoom2d: int = 2000
    xan2d: int = 0
    yan2d: int = 0
    zan2d: int = 0
    x_offset2d: int = 0
    y_offset2d: int = 0
    resize_x: int = 128
    resize_y: int = 128
    resize_z: int = 128
    ambient: int = 0
    contrast: int = 0
    stackable: int = 0
    noted_id: int = -1
    noted_template: int = -1
    bought_id: int = -1
    bought_template_id: int = -1
    placeholder_id: int = -1
    placeholder_template_id: int = -1
    male_model_ids: tuple[int, int, int] = (-1, -1, -1)
    female_model_ids: tuple[int, int, int] = (-1, -1, -1)
    male_offset: int = 0
    female_offset: int = 0
    wearpos1: int = -1
    wearpos2: int = -1
    wearpos3: int = -1
    recolor_src: tuple[int, ...] = ()
    recolor_dst: tuple[int, ...] = ()
    retexture_src: tuple[int, ...] = ()
    retexture_dst: tuple[int, ...] = ()
    count_obj: tuple[int, ...] = ()
    count_amt: tuple[int, ...] = ()


@dataclass(frozen=True)
class HeaderRow:
    """One item_models.h render mapping row."""

    item_id: int
    inv_model: int
    wield_model: int
    hide_body_mask: int
    equip_slot: int
    wearpos1: int
    wearpos2: int
    wearpos3: int
    render_flags: int
    ready_anim_id: int
    walk_anim_id: int
    run_anim_id: int


@dataclass
class ExportReport:
    """Post-export verification details for the final summary."""

    loadout_item_ids: list[int] = field(default_factory=list)
    visible_worn_item_ids: list[int] = field(default_factory=list)
    sprite_item_ids: list[int] = field(default_factory=list)
    no_worn_model_item_ids: list[int] = field(default_factory=list)


@dataclass(frozen=True)
class ConsumableClickRow:
    """One row from OSRS_CONSUMABLE_CLICK_REGISTRY."""

    raw_osrs_id: int
    click_action: str
    consumable_kind: str
    dose_count: int


def be_u16(data: bytes, pos: int) -> tuple[int, int]:
    return struct.unpack_from(">H", data, pos)[0], pos + 2


def be_i16(data: bytes, pos: int) -> tuple[int, int]:
    return struct.unpack_from(">h", data, pos)[0], pos + 2


def be_i8(data: bytes, pos: int) -> tuple[int, int]:
    return struct.unpack_from(">b", data, pos)[0], pos + 1


def be_u8(data: bytes, pos: int) -> tuple[int, int]:
    return data[pos], pos + 1


def be_u32(data: bytes, pos: int) -> tuple[int, int]:
    return struct.unpack_from(">I", data, pos)[0], pos + 4


def read_string(data: bytes, pos: int) -> tuple[str, int]:
    end = data.find(b"\x00", pos)
    if end < 0:
        raise ValueError("unterminated cache string")
    return data[pos:end].decode("cp1252", "replace"), end + 1


def signed_u16_offset(value: int) -> int:
    return value - 65536 if value > 32767 else value


def skip_params(data: bytes, pos: int) -> int:
    count, pos = be_u8(data, pos)
    for _ in range(count):
        is_string, pos = be_u8(data, pos)
        pos += 3
        if is_string:
            _value, pos = read_string(data, pos)
        else:
            pos += 4
    return pos


def parse_cache_item(item_id: int, data: bytes) -> CacheItemDef:
    item = CacheItemDef(item_id=item_id)
    male = [-1, -1, -1]
    female = [-1, -1, -1]
    recolor_src: list[int] = []
    recolor_dst: list[int] = []
    retexture_src: list[int] = []
    retexture_dst: list[int] = []
    count_obj = [0] * 10
    count_amt = [0] * 10
    saw_count = False

    pos = 0
    while pos < len(data):
        opcode = data[pos]
        pos += 1
        if opcode == 0:
            break
        if opcode == 1:
            item.inv_model, pos = be_u16(data, pos)
        elif opcode == 2:
            item.name, pos = read_string(data, pos)
        elif opcode == 3:
            _text, pos = read_string(data, pos)
        elif opcode == 4:
            item.zoom2d, pos = be_u16(data, pos)
        elif opcode == 5:
            item.xan2d, pos = be_u16(data, pos)
        elif opcode == 6:
            item.yan2d, pos = be_u16(data, pos)
        elif opcode == 7:
            raw, pos = be_u16(data, pos)
            item.x_offset2d = signed_u16_offset(raw)
        elif opcode == 8:
            raw, pos = be_u16(data, pos)
            item.y_offset2d = signed_u16_offset(raw)
        elif opcode == 9:
            _text, pos = read_string(data, pos)
        elif opcode in (10, 21, 22, 66, 67, 68, 71, 73, 74, 76, 77, 80, 81,
                        82, 83, 84, 85, 86, 87, 94, 116, 117, 118, 156, 161,
                        202):
            pos += 2
        elif opcode == 11:
            item.stackable = 1
        elif opcode == 12:
            pos += 4
        elif opcode == 13:
            item.wearpos1, pos = be_i8(data, pos)
        elif opcode == 14:
            item.wearpos2, pos = be_i8(data, pos)
        elif opcode in (15, 16, 64, 65):
            pass
        elif opcode in (17, 18, 19, 20, 28, 29, 62, 69, 115, 119, 120, 121,
                        122, 155, 157, 158, 159, 162, 163, 165):
            pos += 1
        elif opcode == 23:
            male[0], pos = be_u16(data, pos)
            item.male_offset, pos = be_i8(data, pos)
        elif opcode == 24:
            male[1], pos = be_u16(data, pos)
        elif opcode == 25:
            female[0], pos = be_u16(data, pos)
            item.female_offset, pos = be_i8(data, pos)
        elif opcode == 26:
            female[1], pos = be_u16(data, pos)
        elif opcode == 27:
            item.wearpos3, pos = be_i8(data, pos)
        elif 30 <= opcode < 40:
            _text, pos = read_string(data, pos)
        elif opcode == 40:
            count, pos = be_u8(data, pos)
            for _ in range(count):
                src, pos = be_u16(data, pos)
                dst, pos = be_u16(data, pos)
                recolor_src.append(src)
                recolor_dst.append(dst)
        elif opcode == 41:
            count, pos = be_u8(data, pos)
            for _ in range(count):
                src, pos = be_u16(data, pos)
                dst, pos = be_u16(data, pos)
                retexture_src.append(src)
                retexture_dst.append(dst)
        elif opcode == 42:
            pos += 1
        elif opcode == 43:
            pos += 1
            while pos < len(data):
                subop = data[pos] - 1
                pos += 1
                if subop == -1:
                    break
                _text, pos = read_string(data, pos)
        elif opcode == 44:
            item.inv_model, pos = be_u32(data, pos)
        elif opcode == 45:
            male[0], pos = be_u32(data, pos)
            item.male_offset, pos = be_i8(data, pos)
        elif opcode == 46:
            male[1], pos = be_u32(data, pos)
        elif opcode == 47:
            male[2], pos = be_u32(data, pos)
        elif opcode == 48:
            female[0], pos = be_u32(data, pos)
            item.female_offset, pos = be_i8(data, pos)
        elif opcode == 49:
            female[1], pos = be_u32(data, pos)
        elif opcode == 50:
            female[2], pos = be_u32(data, pos)
        elif opcode in (51, 52, 53, 54):
            pos += 4
        elif opcode == 75:
            _weight, pos = be_i16(data, pos)
        elif opcode == 78:
            male[2], pos = be_u16(data, pos)
        elif opcode == 79:
            female[2], pos = be_u16(data, pos)
        elif opcode in (90, 91, 92, 93):
            pos += 2
        elif opcode == 95:
            item.zan2d, pos = be_u16(data, pos)
        elif opcode == 97:
            item.noted_id, pos = be_u16(data, pos)
        elif opcode == 98:
            item.noted_template, pos = be_u16(data, pos)
        elif 100 <= opcode < 110:
            idx = opcode - 100
            count_obj[idx], pos = be_u16(data, pos)
            count_amt[idx], pos = be_u16(data, pos)
            saw_count = True
        elif opcode == 110:
            item.resize_x, pos = be_u16(data, pos)
        elif opcode == 111:
            item.resize_y, pos = be_u16(data, pos)
        elif opcode == 112:
            item.resize_z, pos = be_u16(data, pos)
        elif opcode == 113:
            item.ambient, pos = be_i8(data, pos)
        elif opcode == 114:
            item.contrast, pos = be_i8(data, pos)
        elif opcode == 139:
            item.bought_id, pos = be_u16(data, pos)
        elif opcode == 140:
            item.bought_template_id, pos = be_u16(data, pos)
        elif opcode == 148:
            item.placeholder_id, pos = be_u16(data, pos)
        elif opcode == 149:
            item.placeholder_template_id, pos = be_u16(data, pos)
        elif opcode == 160:
            count, pos = be_u8(data, pos)
            pos += count * 2
        elif opcode == 164:
            _text, pos = read_string(data, pos)
        elif opcode == 211:
            count, pos = be_u8(data, pos)
            pos += count * 2
        elif opcode == 249:
            pos = skip_params(data, pos)
        else:
            raise ValueError(f"unknown item opcode {opcode} for {item_id} at {pos - 1}")

    item.male_model_ids = tuple(male)
    item.female_model_ids = tuple(female)
    item.recolor_src = tuple(recolor_src)
    item.recolor_dst = tuple(recolor_dst)
    item.retexture_src = tuple(retexture_src)
    item.retexture_dst = tuple(retexture_dst)
    if saw_count:
        item.count_obj = tuple(count_obj)
        item.count_amt = tuple(count_amt)
    return item


def parse_existing_header(path: Path) -> list[HeaderRow]:
    src = path.read_text()
    rows: list[HeaderRow] = []
    for match in re.finditer(r"\{\s*([^{}]+)\s*\}", src):
        values = [value.strip() for value in match.group(1).split(",")]
        if len(values) != 12:
            continue
        try:
            rows.append(HeaderRow(*(int(value, 0) for value in values)))
        except ValueError:
            continue
    if not rows:
        raise SystemExit(f"export_colosseum_items: no item model rows parsed from {path}")
    return rows


def parse_generated_items(path: Path) -> dict[str, GameItemInfo]:
    src = path.read_text()
    out: dict[str, GameItemInfo] = {}
    pattern = re.compile(r"\[(ITEM_[A-Z0-9_]+)\]\s*=\s*\{(.*?)\n\s*\},", re.S)
    for match in pattern.finditer(src):
        symbol = match.group(1)
        body = match.group(2)
        id_match = re.search(r"\.item_id\s*=\s*(\d+)", body)
        name_match = re.search(r'\.name\s*=\s*"([^"]+)"', body)
        slot_match = re.search(r"\.slot\s*=\s*(SLOT_[A-Z]+)", body)
        if not id_match or not name_match or not slot_match:
            continue
        slot_name = slot_match.group(1)
        if slot_name not in SLOT_NAMES:
            raise SystemExit(f"export_colosseum_items: unknown slot {slot_name} for {symbol}")
        out[symbol] = GameItemInfo(
            symbol=symbol,
            item_id=int(id_match.group(1)),
            name=name_match.group(1),
            equip_slot=SLOT_NAMES[slot_name],
            two_handed=".two_handed = 1" in body,
        )
    return out


def parse_colosseum_symbols(path: Path) -> list[str]:
    src = path.read_text()
    start = src.find("COLO_BEGINNER_MELEE_LOADOUT")
    end = src.find("COLO_SUPPLY_LOADOUTS", start)
    if start < 0 or end < 0:
        raise SystemExit("export_colosseum_items: Colosseum loadout block not found")
    seen: set[str] = set()
    out: list[str] = []
    for symbol in re.findall(r"ITEM_[A-Z0-9_]+", src[start:end]):
        if symbol == "ITEM_NONE" or symbol in seen:
            continue
        seen.add(symbol)
        out.append(symbol)
    return out


def strip_c_comments(src: str) -> str:
    """Remove C comments before parsing initializer values."""

    return re.sub(r"/\*.*?\*/|//[^\n]*", "", src, flags=re.S)


def extract_c_initializer_body(src: str, symbol: str) -> str:
    """Return the balanced initializer body following a C symbol."""

    start = src.find(symbol)
    if start < 0:
        raise SystemExit(f"export_colosseum_items: {symbol} not found")
    brace_start = src.find("{", start)
    if brace_start < 0:
        raise SystemExit(f"export_colosseum_items: {symbol} initializer not found")

    depth = 0
    for idx in range(brace_start, len(src)):
        char = src[idx]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return src[brace_start + 1:idx]
    raise SystemExit(f"export_colosseum_items: {symbol} initializer is unterminated")


def parse_colosseum_gameplay_inventory_loadout_ids(path: Path) -> list[int]:
    """Parse raw OSRS item ids from COLO_GAMEPLAY_INVENTORY_LOADOUT."""

    src = strip_c_comments(path.read_text())
    body = extract_c_initializer_body(src, "COLO_GAMEPLAY_INVENTORY_LOADOUT")
    return [int(value) for value in re.findall(r"\b\d+\b", body) if int(value) > 0]


def parse_consumable_click_registry(path: Path) -> list[ConsumableClickRow]:
    """Parse OSRS_CONSUMABLE_CLICK_REGISTRY rows from the shared C header."""

    src = strip_c_comments(path.read_text())
    body = extract_c_initializer_body(src, "OSRS_CONSUMABLE_CLICK_REGISTRY")
    rows: list[ConsumableClickRow] = []
    pattern = re.compile(
        r"\{\s*(\d+)\s*,\s*(OSRS_CLICK_[A-Z_]+)\s*,\s*"
        r"(OSRS_CONSUMABLE_[A-Z0-9_]+)\s*,\s*(\d+)\s*\}"
    )
    for raw_osrs_id, click_action, consumable_kind, dose_count in pattern.findall(body):
        rows.append(
            ConsumableClickRow(
                raw_osrs_id=int(raw_osrs_id),
                click_action=click_action,
                consumable_kind=consumable_kind,
                dose_count=int(dose_count),
            )
        )
    if not rows:
        raise SystemExit("export_colosseum_items: consumable click registry parsed empty")
    return rows


def consumable_row_after_drink(
    current: ConsumableClickRow,
    rows: list[ConsumableClickRow],
) -> ConsumableClickRow | None:
    """Resolve the next dose row from a current drink row."""

    next_dose_count = current.dose_count - 1
    if next_dose_count <= 0:
        return None
    matches = [
        row for row in rows
        if row.click_action == "OSRS_CLICK_DRINK"
        and row.consumable_kind == current.consumable_kind
        and row.dose_count == next_dose_count
    ]
    if len(matches) != 1:
        raise SystemExit(
            "export_colosseum_items: expected one next dose for "
            f"{current.raw_osrs_id}, got {len(matches)}"
        )
    return matches[0]


def colosseum_consumable_dose_variant_ids(
    gameplay_item_ids: list[int],
    rows: list[ConsumableClickRow],
) -> list[int]:
    """Derive reachable colosseum consumable dose ids from loadouts and registry."""

    by_id = {row.raw_osrs_id: row for row in rows}
    seen: set[int] = set()
    out: list[int] = []

    for item_id in gameplay_item_ids:
        root = by_id.get(item_id)
        if root is None or root.click_action != "OSRS_CLICK_DRINK" or root.dose_count <= 0:
            continue

        current: ConsumableClickRow | None = root
        for _ in range(len(rows)):
            if current is None:
                break
            if current.raw_osrs_id not in seen:
                seen.add(current.raw_osrs_id)
                out.append(current.raw_osrs_id)
            current = consumable_row_after_drink(current, rows)
        if current is not None:
            raise SystemExit(
                "export_colosseum_items: consumable dose chain cycles at "
                f"{current.raw_osrs_id}"
            )
    return out


def ordered_target_ids(existing_rows: list[HeaderRow], loadout_ids: list[int]) -> list[int]:
    ids = [row.item_id for row in existing_rows]
    seen = set(ids)
    for item_id in loadout_ids:
        if item_id not in seen:
            ids.append(item_id)
            seen.add(item_id)
    return ids


def has_wearpos_data(item: CacheItemDef) -> bool:
    return item.wearpos1 >= 0 or item.wearpos2 >= 0 or item.wearpos3 >= 0


def render_hide_mask(cache_item: CacheItemDef, equip_slot: int) -> int:
    mask = 0
    for wearpos in (cache_item.wearpos1, cache_item.wearpos2, cache_item.wearpos3):
        if wearpos == WEARPOS_TORSO:
            mask |= BODY_TORSO
        elif wearpos == WEARPOS_ARMS:
            mask |= BODY_ARMS
        elif wearpos == WEARPOS_LEGS:
            mask |= BODY_LEGS
        elif wearpos == WEARPOS_HEAD:
            mask |= BODY_HAIR
        elif wearpos == WEARPOS_HANDS:
            mask |= BODY_HANDS
        elif wearpos == WEARPOS_FEET:
            mask |= BODY_FEET
        elif wearpos == WEARPOS_JAW:
            mask |= BODY_JAW
    if mask == 0 and not has_wearpos_data(cache_item):
        if equip_slot == SLOT_NAMES["SLOT_BODY"]:
            mask |= BODY_TORSO
        elif equip_slot == SLOT_NAMES["SLOT_LEGS"]:
            mask |= BODY_LEGS
        elif equip_slot == SLOT_NAMES["SLOT_HANDS"]:
            mask |= BODY_HANDS
        elif equip_slot == SLOT_NAMES["SLOT_FEET"]:
            mask |= BODY_FEET
    return mask


def render_flags(cache_item: CacheItemDef, equip_slot: int, two_handed: bool) -> int:
    flags = 0
    if has_wearpos_data(cache_item):
        flags |= RENDER_FLAG_WEARPOS_AUTHORITY
    if equip_slot == SLOT_NAMES["SLOT_WEAPON"] and (
        two_handed
        or cache_item.wearpos2 == WEARPOS_LEFT_HAND
        or cache_item.wearpos3 == WEARPOS_LEFT_HAND
    ):
        flags |= RENDER_FLAG_TWO_HANDED
    return flags


def missing_to_u32(value: int) -> int:
    return MISSING_U32 if value < 0 else value


def u32_to_header(value: int) -> int:
    return value if value >= 0 else MISSING_U32


def item_info_by_id(items: dict[str, GameItemInfo]) -> dict[int, GameItemInfo]:
    return {item.item_id: item for item in items.values()}


def load_cache_items(reader: ModernCacheReader, item_ids: list[int]) -> dict[int, CacheItemDef]:
    files = reader.read_group(2, MODERN_OBJ_GROUP)
    out: dict[int, CacheItemDef] = {}
    missing = [item_id for item_id in item_ids if item_id not in files]
    if missing:
        raise SystemExit(
            "export_colosseum_items: item defs missing from cache: "
            + ", ".join(str(item_id) for item_id in missing)
        )
    for item_id in item_ids:
        out[item_id] = parse_cache_item(item_id, files[item_id])
    return out


def load_model_or_abort(reader: ModernCacheReader, model_id: int, context: str) -> ModelData:
    raw = load_model_modern(reader, model_id)
    if raw is None:
        raise SystemExit(f"export_colosseum_items: model {model_id} missing for {context}")
    model = decode_model(model_id, raw)
    if model is None:
        raise SystemExit(f"export_colosseum_items: model {model_id} failed to decode for {context}")
    return model


def build_body_models(reader: ModernCacheReader) -> list[ModelData]:
    idk_files = reader.read_group(2, MODERN_IDK_GROUP)
    models: list[ModelData] = []
    for body_part_id, kit_id in sorted(DEFAULT_MALE_KITS.items()):
        if kit_id not in idk_files:
            raise SystemExit(f"export_colosseum_items: identity kit {kit_id} missing")
        kit = parse_identity_kit(kit_id, idk_files[kit_id])
        if not kit.body_models:
            raise SystemExit(f"export_colosseum_items: identity kit {kit_id} has no body models")
        parts = [
            load_model_or_abort(reader, model_id, f"identity kit {kit_id}")
            for model_id in kit.body_models
        ]
        merged = parts[0] if len(parts) == 1 else _merge_models(parts)
        apply_appearance_overrides(
            merged,
            kit.recolor_src,
            kit.recolor_dst,
            kit.retexture_src,
            kit.retexture_dst,
        )
        merged.model_id = BODY_MODEL_BASE + body_part_id
        models.append(merged)
        print(f"body {BODY_PART_NAMES[body_part_id]} kit={kit_id} -> {merged.model_id}")
    return models


def build_item_models(
    reader: ModernCacheReader,
    ordered_ids: list[int],
    cache_items: dict[int, CacheItemDef],
) -> tuple[list[ModelData], dict[int, int], set[int]]:
    models: list[ModelData] = []
    wield_by_id: dict[int, int] = {}
    raw_model_ids: set[int] = set()
    for index, item_id in enumerate(ordered_ids):
        cache_item = cache_items[item_id]
        if cache_item.inv_model >= 0:
            raw_model_ids.add(cache_item.inv_model)
        male_ids = [model_id for model_id in cache_item.male_model_ids if model_id >= 0]
        if not male_ids:
            continue
        parts = [
            load_model_or_abort(reader, model_id, f"item {item_id}")
            for model_id in male_ids
        ]
        merged = parts[0] if len(parts) == 1 else _merge_models(parts)
        apply_appearance_overrides(
            merged,
            cache_item.recolor_src,
            cache_item.recolor_dst,
            cache_item.retexture_src,
            cache_item.retexture_dst,
        )
        translate_model_y(merged, cache_item.male_offset)
        merged.model_id = ITEM_MODEL_BASE + index
        wield_by_id[item_id] = merged.model_id
        models.append(merged)
    return models, wield_by_id, raw_model_ids


def build_raw_models(reader: ModernCacheReader, model_ids: set[int]) -> list[ModelData]:
    models: list[ModelData] = []
    for model_id in sorted(model_ids):
        models.append(load_model_or_abort(reader, model_id, f"raw model {model_id}"))
    dragon_bolt = load_model_or_abort(reader, 3135, "dragon bolt recolor")
    apply_appearance_overrides(dragon_bolt, (41, 61, 57), (1692, 670, 1825), (), ())
    dragon_bolt.model_id = DRAGON_BOLT_MODEL_ID
    models.append(dragon_bolt)
    return models


def build_header_rows(
    ordered_ids: list[int],
    existing_by_id: dict[int, HeaderRow],
    info_by_osrs_id: dict[int, GameItemInfo],
    cache_items: dict[int, CacheItemDef],
    wield_by_id: dict[int, int],
) -> list[HeaderRow]:
    rows: list[HeaderRow] = []
    for item_id in ordered_ids:
        cache_item = cache_items[item_id]
        info = info_by_osrs_id.get(item_id)
        previous = existing_by_id.get(item_id)
        if info is not None:
            equip_slot = info.equip_slot
            two_handed = info.two_handed
        elif previous is not None:
            equip_slot = previous.equip_slot
            two_handed = bool(previous.render_flags & RENDER_FLAG_TWO_HANDED)
        else:
            raise SystemExit(f"export_colosseum_items: no equip slot for item {item_id}")
        ready, walk, run = KNOWN_PLAYER_BAS.get(item_id, (-1, -1, -1))
        rows.append(HeaderRow(
            item_id=item_id,
            inv_model=u32_to_header(cache_item.inv_model),
            wield_model=wield_by_id.get(item_id, MISSING_U32),
            hide_body_mask=render_hide_mask(cache_item, equip_slot),
            equip_slot=equip_slot,
            wearpos1=missing_to_u32(cache_item.wearpos1),
            wearpos2=missing_to_u32(cache_item.wearpos2),
            wearpos3=missing_to_u32(cache_item.wearpos3),
            render_flags=render_flags(cache_item, equip_slot, two_handed),
            ready_anim_id=missing_to_u32(ready),
            walk_anim_id=missing_to_u32(walk),
            run_anim_id=missing_to_u32(run),
        ))
    return rows


def make_existing_atlas(path: Path, texture_ids: list[int]) -> TextureAtlas:
    raw = path.read_bytes()
    if len(raw) < 12:
        raise SystemExit(f"export_colosseum_items: atlas too small: {path}")
    magic, width, height = struct.unpack_from("<III", raw, 0)
    if magic != 0x41544C53:
        raise SystemExit(f"export_colosseum_items: bad atlas magic in {path}")
    cell_size = 128
    repeat_v_padding = 128
    slot_h = cell_size + repeat_v_padding * 2
    cols = width // cell_size
    uv_map: dict[int, tuple[float, float, float, float]] = {}
    anim_map: dict[int, tuple[int, int, int, int, int]] = {}
    for slot_idx, texture_id in enumerate(sorted(texture_ids), start=1):
        col = slot_idx % cols
        row = slot_idx // cols
        ax = col * cell_size
        ay = row * slot_h
        if ay + slot_h > height:
            raise SystemExit(f"export_colosseum_items: atlas lacks slot for texture {texture_id}")
        uv_map[texture_id] = (
            ax / width,
            (ay + repeat_v_padding) / height,
            cell_size / width,
            cell_size / height,
        )
        anim_map[texture_id] = (ax, ay, cell_size, slot_h, repeat_v_padding)
    return TextureAtlas(
        width=width,
        height=height,
        pixels=b"",
        uv_map=uv_map,
        anim_map=anim_map,
        repeat_v_margin=repeat_v_padding / cell_size,
        white_u=0.5 * cell_size / width,
        white_v=(repeat_v_padding + 0.5 * cell_size) / height,
    )


def write_models_mdl4(
    path: Path,
    models: list[ModelData],
    tex_colors: dict[int, int],
    atlas: TextureAtlas,
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    offsets: list[int] = []
    with path.open("wb") as out:
        out.write(struct.pack("<II", 0x4D444C34, len(models)))
        offsets_pos = out.tell()
        out.write(b"\x00" * (len(models) * 4))
        for model in models:
            offsets.append(out.tell())
            verts, colors, uvs = expand_model(
                model,
                tex_colors=tex_colors,
                atlas=atlas,
                bake_priority_offsets=False,
                model_lighting="unlit",
            )
            expanded_vert_count = model.face_count * 3
            out.write(struct.pack("<IHHH", model.model_id, expanded_vert_count, model.face_count, model.vertex_count))
            for value in verts:
                out.write(struct.pack("<f", value))
            for red, green, blue, alpha in colors:
                out.write(struct.pack("BBBB", red, green, blue, alpha))
            for value in uvs:
                out.write(struct.pack("<f", value))
            for idx in range(model.vertex_count):
                out.write(struct.pack(
                    "<hhh",
                    max(-32768, min(32767, model.vertices_x[idx])),
                    max(-32768, min(32767, model.vertices_y[idx])),
                    max(-32768, min(32767, model.vertices_z[idx])),
                ))
            skins = model.vertex_skins or [0] * model.vertex_count
            for skin in skins:
                out.write(struct.pack("B", max(0, min(255, skin))))
            for idx in range(model.face_count):
                out.write(struct.pack(
                    "<HHH",
                    max(0, min(65535, model.face_a[idx])),
                    max(0, min(65535, model.face_b[idx])),
                    max(0, min(65535, model.face_c[idx])),
                ))
            priorities = model.face_priorities or [0] * model.face_count
            for idx in range(model.face_count):
                priority = priorities[idx] if idx < len(priorities) else 0
                out.write(struct.pack("B", max(0, min(255, priority))))
            for idx in range(model.face_count):
                alpha = model.face_alphas[idx] if idx < len(model.face_alphas) else 0
                out.write(struct.pack("B", max(0, min(255, alpha))))
            for idx in range(model.face_count):
                label = model.face_alpha_labels[idx] if idx < len(model.face_alpha_labels) else 255
                out.write(struct.pack("B", max(0, min(255, label))))
        out.seek(offsets_pos)
        for offset in offsets:
            out.write(struct.pack("<I", offset))


def write_item_header(path: Path, rows: list[HeaderRow]) -> None:
    with path.open("w") as out:
        out.write("/* generated by ocean/osrs/tools/export_colosseum_items.py, do not edit */\n")
        out.write("#ifndef ITEM_MODELS_H\n#define ITEM_MODELS_H\n\n")
        out.write("#include <stdint.h>\n\n")
        out.write("#define ITEM_RENDER_MODEL_MISSING 0xFFFFFFFFu\n")
        out.write("#define ITEM_RENDER_FLAG_TWO_HANDED 1u\n")
        out.write("#define ITEM_RENDER_FLAG_WEARPOS_AUTHORITY 2u\n\n")
        out.write("typedef struct {\n")
        out.write("    uint16_t item_id;\n")
        out.write("    uint32_t inv_model;\n")
        out.write("    uint32_t wield_model;\n")
        out.write("    uint32_t hide_body_mask;\n")
        out.write("    uint32_t equip_slot;\n")
        out.write("    uint32_t wearpos1;\n")
        out.write("    uint32_t wearpos2;\n")
        out.write("    uint32_t wearpos3;\n")
        out.write("    uint32_t render_flags;\n")
        out.write("    uint32_t ready_anim_id;\n")
        out.write("    uint32_t walk_anim_id;\n")
        out.write("    uint32_t run_anim_id;\n")
        out.write("} ItemModelMapping;\n\n")
        out.write(f"#define ITEM_MODEL_COUNT {len(rows)}\n\n")
        out.write("static const ItemModelMapping ITEM_MODEL_MAP[] = {\n")
        for row in rows:
            values = (
                row.item_id,
                row.inv_model,
                row.wield_model,
                row.hide_body_mask,
                row.equip_slot,
                row.wearpos1,
                row.wearpos2,
                row.wearpos3,
                row.render_flags,
                row.ready_anim_id,
                row.walk_anim_id,
                row.run_anim_id,
            )
            out.write("    { " + ", ".join(str(value) for value in values) + " },\n")
        out.write("};\n\n#endif /* ITEM_MODELS_H */\n")


def write_player_header(path: Path) -> None:
    with path.open("w") as out:
        out.write("/* generated by ocean/osrs/tools/export_colosseum_items.py, do not edit */\n")
        out.write("#ifndef PLAYER_MODELS_H\n#define PLAYER_MODELS_H\n\n")
        out.write("#include <stdint.h>\n\n")
        for idx, name in enumerate(BODY_PART_NAMES):
            out.write(f"#define BODY_PART_{name} {idx}\n")
        out.write("#define BODY_PART_COUNT 7\n\n")
        out.write("static const uint32_t DEFAULT_BODY_MODELS[BODY_PART_COUNT] = {\n")
        for idx, name in enumerate(BODY_PART_NAMES):
            out.write(f"    0x{BODY_MODEL_BASE + idx:X},  /* {name} */\n")
        out.write("};\n\n#endif /* PLAYER_MODELS_H */\n")


def write_item_sprite_tsv(path: Path, cache_items: dict[int, CacheItemDef]) -> None:
    header = [
        "item_id",
        "name",
        "inventory_model",
        "zoom2d",
        "xan2d",
        "yan2d",
        "zan2d",
        "x_offset2d",
        "y_offset2d",
        "resize_x",
        "resize_y",
        "resize_z",
        "ambient",
        "contrast",
        "stackable",
        "noted_id",
        "noted_template",
        "bought_id",
        "bought_template_id",
        "placeholder_id",
        "placeholder_template_id",
        "recolor_src",
        "recolor_dst",
        "retexture_src",
        "retexture_dst",
        "count_obj",
        "count_amt",
    ]
    def csv(values: tuple[int, ...]) -> str:
        return ",".join(str(value) for value in values)

    with path.open("w") as out:
        out.write("\t".join(header) + "\n")
        for item_id in sorted(cache_items):
            item = cache_items[item_id]
            row = [
                item.item_id,
                item.name,
                item.inv_model,
                item.zoom2d,
                item.xan2d,
                item.yan2d,
                item.zan2d,
                item.x_offset2d,
                item.y_offset2d,
                item.resize_x,
                item.resize_y,
                item.resize_z,
                item.ambient,
                item.contrast,
                item.stackable,
                item.noted_id,
                item.noted_template,
                item.bought_id,
                item.bought_template_id,
                item.placeholder_id,
                item.placeholder_template_id,
                csv(item.recolor_src),
                csv(item.recolor_dst),
                csv(item.retexture_src),
                csv(item.retexture_dst),
                csv(item.count_obj),
                csv(item.count_amt),
            ]
            out.write("\t".join(str(value) for value in row) + "\n")


def write_texture_tsv(path: Path, reader: ModernCacheReader) -> list[int]:
    texture_files = reader.read_group(9, 0)
    texture_ids: list[int] = []
    with path.open("w") as out:
        out.write("texture_id\tsprite_id\tmissing_color\tfield1778\tanimation_direction\tanimation_speed\n")
        for texture_id, data in sorted(texture_files.items()):
            if len(data) < 7:
                continue
            sprite_id = struct.unpack_from(">H", data, 0)[0]
            missing_color = struct.unpack_from(">H", data, 2)[0]
            field1778 = data[4]
            animation_direction = data[5]
            animation_speed = data[6]
            out.write(
                f"{texture_id}\t{sprite_id}\t{missing_color}\t{field1778}"
                f"\t{animation_direction}\t{animation_speed}\n"
            )
            texture_ids.append(texture_id)
    return texture_ids


def find_gradle_jar(group_path: str, artifact: str, version: str) -> Path:
    root = Path.home() / ".gradle" / "caches" / "modules-2" / "files-2.1" / group_path / artifact / version
    matches = sorted(root.glob(f"*/{artifact}-{version}.jar"))
    if not matches:
        raise SystemExit(f"export_colosseum_items: jar missing for {group_path}:{artifact}:{version}")
    return matches[-1]


def run_sprite_exporter(
    cache_dir: Path,
    output_dir: Path,
    item_tsv: Path,
    texture_tsv: Path,
    item_ids: list[int],
    java_source: Path,
) -> None:
    jars = [
        find_gradle_jar("net.runelite", "cache", "1.11.9"),
        find_gradle_jar("org.slf4j", "slf4j-api", "1.7.36"),
        find_gradle_jar("org.slf4j", "slf4j-simple", "1.7.36"),
        find_gradle_jar("com.google.guava", "guava", "30.1.1-jre"),
        find_gradle_jar("com.google.guava", "failureaccess", "1.0.1"),
        find_gradle_jar("org.apache.commons", "commons-compress", "1.21"),
        find_gradle_jar("net.java.dev.jna", "jna", "5.9.0"),
        find_gradle_jar("com.google.code.gson", "gson", "2.10.1"),
    ]
    classpath = os.pathsep.join(str(jar) for jar in jars)
    with tempfile.TemporaryDirectory(prefix="osrs-item-sprites-") as tmp:
        classes = Path(tmp) / "classes"
        classes.mkdir()
        subprocess.run(
            ["javac", "-cp", classpath, "-d", str(classes), str(java_source)],
            check=True,
        )
        runtime_classpath = os.pathsep.join([str(classes), classpath])
        subprocess.run(
            [
                "java",
                "-Djava.awt.headless=true",
                "-cp",
                runtime_classpath,
                "ExportModernItemSprites",
                "--cache",
                str(cache_dir),
                "--output",
                str(output_dir),
                "--items-tsv",
                str(item_tsv),
                "--textures-tsv",
                str(texture_tsv),
                "--ids",
                ",".join(str(item_id) for item_id in item_ids),
            ],
            check=True,
        )


def verify_pngs(paths: list[Path]) -> None:
    for path in paths:
        if not path.is_file():
            raise SystemExit(f"export_colosseum_items: sprite missing after export: {path}")
        data = path.read_bytes()
        if len(data) < 32 or not data.startswith(b"\x89PNG\r\n\x1a\n"):
            raise SystemExit(f"export_colosseum_items: invalid PNG output: {path}")


def export_modifier_sprites(cache_dir: Path, output_dir: Path) -> list[Path]:
    """Export cache sprite groups for active Colosseum modifier HUD icons."""
    sprite_dir = output_dir / "sprites" / "colosseum" / "modifiers"
    sprite_dir.mkdir(parents=True, exist_ok=True)
    store = RcCacheStore(cache_dir)
    paths: list[Path] = []
    for sprite_id in COLOSSEUM_MODIFIER_SPRITE_IDS:
        data = store.read_container(8, sprite_id)
        if data is None:
            raise SystemExit(f"export_colosseum_items: modifier sprite {sprite_id} missing")
        sprites = decode_sprite_group(data)
        if len(sprites) != 1:
            raise SystemExit(
                f"export_colosseum_items: modifier sprite {sprite_id} has "
                f"{len(sprites)} frames, expected 1"
            )
        sprite = sprites[0]
        path = sprite_dir / f"{sprite_id}.png"
        image = Image.frombytes("RGBA", (sprite.width, sprite.height), sprite.pixels)
        image.save(path)
        paths.append(path)
    verify_pngs(paths)
    return paths


def export_assets(args: argparse.Namespace) -> ExportReport:
    repo_root = Path(__file__).resolve().parents[3]
    output_dir = args.output_dir
    generated_items_path = repo_root / "ocean" / "osrs" / "osrs_items_generated.h"
    colosseum_model_path = repo_root / "ocean" / "osrs" / "encounters" / "colosseum" / "encounter_colosseum_model.inc"
    inventory_clicks_path = repo_root / "ocean" / "osrs" / "osrs_inventory_clicks.h"
    item_header_path = output_dir / "item_models.h"
    player_header_path = output_dir / "player_models.h"
    model_path = output_dir / "equipment.models"
    atlas_path = output_dir / "equipment.atlas"
    sprite_dir = output_dir / "sprites" / "items"

    generated_items = parse_generated_items(generated_items_path)
    info_by_id = item_info_by_id(generated_items)
    colosseum_symbols = parse_colosseum_symbols(colosseum_model_path)
    missing_symbols = [symbol for symbol in colosseum_symbols if symbol not in generated_items]
    if missing_symbols:
        raise SystemExit(
            "export_colosseum_items: loadout symbols missing from generated items: "
            + ", ".join(missing_symbols)
        )
    colosseum_ids = [generated_items[symbol].item_id for symbol in colosseum_symbols]
    model_item_ids = list(dict.fromkeys(
        colosseum_ids + COLOSSEUM_RUNTIME_EQUIPPABLE_ITEM_IDS
    ))
    gameplay_item_ids = parse_colosseum_gameplay_inventory_loadout_ids(colosseum_model_path)
    consumable_rows = parse_consumable_click_registry(inventory_clicks_path)
    consumable_dose_ids = colosseum_consumable_dose_variant_ids(
        gameplay_item_ids,
        consumable_rows,
    )
    display_only_ids = [
        item_id for item_id in (
            COLOSSEUM_NON_CONSUMABLE_DISPLAY_ITEM_IDS +
            consumable_dose_ids
        )
        if item_id not in model_item_ids
    ]

    existing_rows = parse_existing_header(item_header_path)
    existing_by_id = {row.item_id: row for row in existing_rows}
    ordered_ids = ordered_target_ids(existing_rows, model_item_ids)

    reader = ModernCacheReader(args.modern_cache)
    cache_items = load_cache_items(reader, ordered_ids)
    # display-only sprites: load their cache defs for the sprite TSV but keep them
    # out of the gear/header/worn-model paths.
    display_cache_items = load_cache_items(reader, display_only_ids)
    cache_items.update(display_cache_items)
    body_models = build_body_models(reader)
    item_models, wield_by_id, raw_model_ids = build_item_models(reader, ordered_ids, cache_items)
    raw_model_ids |= SPOTANIM_MODELS | COLOSSEUM_WATER_SURGE_MODELS | ENCOUNTER_MODELS
    raw_models = build_raw_models(reader, raw_model_ids)
    all_models = body_models + item_models + raw_models

    tex_colors = load_texture_average_colors_modern(reader)
    atlas = make_existing_atlas(atlas_path, sorted(tex_colors))
    write_models_mdl4(model_path, all_models, tex_colors, atlas)

    rows = build_header_rows(ordered_ids, existing_by_id, info_by_id, cache_items, wield_by_id)
    write_item_header(item_header_path, rows)
    write_player_header(player_header_path)

    with tempfile.TemporaryDirectory(prefix="osrs-colosseum-items-") as tmp:
        tmp_path = Path(tmp)
        item_tsv = tmp_path / "items.tsv"
        texture_tsv = tmp_path / "textures.tsv"
        write_item_sprite_tsv(item_tsv, cache_items)
        write_texture_tsv(texture_tsv, reader)
        sprite_export_ids = sorted(
            set(model_item_ids) | set(display_only_ids)
        )
        run_sprite_exporter(
            args.modern_cache,
            sprite_dir,
            item_tsv,
            texture_tsv,
            sprite_export_ids,
            repo_root / "ocean" / "osrs" / "tools" / "ExportModernItemSprites.java",
        )

    sprite_paths = [sprite_dir / f"{item_id}.png" for item_id in sprite_export_ids]
    verify_pngs(sprite_paths)
    modifier_sprite_paths = export_modifier_sprites(args.modern_cache, output_dir)

    rows_by_id = {row.item_id: row for row in rows}
    missing_model_rows = [
        item_id for item_id in model_item_ids
        if item_id not in rows_by_id
    ]
    if missing_model_rows:
        raise SystemExit(
            "export_colosseum_items: header rows missing after export: "
            + ", ".join(str(item_id) for item_id in missing_model_rows)
        )

    no_worn_model_item_ids: list[int] = []
    visible_worn_item_ids: list[int] = []
    for item_id in model_item_ids:
        cache_item = cache_items[item_id]
        row = rows_by_id[item_id]
        if row.inv_model == MISSING_U32:
            raise SystemExit(f"export_colosseum_items: inventory model missing for {item_id}")
        if any(model_id >= 0 for model_id in cache_item.male_model_ids):
            if row.wield_model == MISSING_U32:
                raise SystemExit(f"export_colosseum_items: worn model missing for {item_id}")
            visible_worn_item_ids.append(item_id)
        else:
            no_worn_model_item_ids.append(item_id)

    print(f"wrote {len(all_models)} models to {model_path}")
    print(f"wrote {len(rows)} item rows to {item_header_path}")
    print(f"wrote {len(sprite_paths)} Colosseum sprites to {sprite_dir}")
    print(
        f"wrote {len(modifier_sprite_paths)} Colosseum modifier sprites to "
        f"{output_dir / 'sprites' / 'colosseum' / 'modifiers'}"
    )
    return ExportReport(
        loadout_item_ids=sorted(set(colosseum_ids)),
        visible_worn_item_ids=sorted(set(visible_worn_item_ids)),
        sprite_item_ids=sorted(set(sprite_export_ids)),
        no_worn_model_item_ids=sorted(set(no_worn_model_item_ids)),
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="export Colosseum item visuals from the modern OSRS cache")
    parser.add_argument(
        "--modern-cache",
        type=Path,
        default=Path("/Users/valtterivalo/Projects/pufferlib-metal/.refs/osrs-cache-modern"),
        help="modern Jagex dat2 cache directory",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("ocean/osrs/data"),
        help="OSRS data output directory",
    )
    args = parser.parse_args()
    report = export_assets(args)
    print("loadout item ids:", ", ".join(str(item_id) for item_id in report.loadout_item_ids))
    print("visible worn model ids:", ", ".join(str(item_id) for item_id in report.visible_worn_item_ids))
    print("sprite ids:", ", ".join(str(item_id) for item_id in report.sprite_item_ids))
    if report.no_worn_model_item_ids:
        print("cache no worn model ids:", ", ".join(str(item_id) for item_id in report.no_worn_model_item_ids))


if __name__ == "__main__":
    main()
