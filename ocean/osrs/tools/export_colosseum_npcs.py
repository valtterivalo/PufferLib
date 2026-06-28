"""Export Fortis Colosseum NPC models and animations from the modern OSRS cache.

Reads NPC definitions for the Colosseum monsters and hazard entities, extracts
model and animation ids, decodes and merges meshes into ``colosseum_npcs.models``,
writes their sequences into ``colosseum_npcs.anims``, and emits a standalone
``npc_models_colosseum.h`` whose ``NPC_MODEL_MAP_COLOSSEUM_GEN`` table mirrors
``npc_models_inferno.h``.

The shared inferno exporter parses NPC defs with the legacy opcode set, which
predates modern opcode 61 (i32 model lists). Colosseum model ids exceed the
u16 range, so this exporter decodes defs with ``rc_cache.definitions`` instead.

Usage:
    uv run python ocean/osrs/tools/export_colosseum_npcs.py \
        --modern-cache .refs/osrs-cache-modern \
        --output-dir ocean/osrs/data
"""

from __future__ import annotations

import argparse
import copy
import math
import os
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path


def _find_cache_pipeline() -> Path:
    """Locate the vendored cache pipeline inside the tracked tree."""
    env_override = os.environ.get("OSRS_CACHE_PIPELINE")
    candidates = []
    if env_override:
        candidates.append(Path(env_override))
    repo_root = Path(__file__).resolve().parents[3]
    candidates.append(repo_root / "ocean" / "osrs" / "tools" / "cache_pipeline")
    for candidate in candidates:
        if candidate.is_dir():
            return candidate
    searched = "\n  ".join(str(c) for c in candidates)
    raise SystemExit(f"export_colosseum_npcs: cache pipeline not found, searched:\n  {searched}")


CACHE_PIPELINE = _find_cache_pipeline()
sys.path.insert(0, str(CACHE_PIPELINE))

from modern_cache_reader import ModernCacheReader
from rc_cache.definitions import decode_npc_definition
from rc_cache import (
    RcCacheStore,
    load_texture_average_colors,
    load_texture_sprites,
)
from export_textures import build_atlas
from export_models import (
    ModelData,
    _merge_models,
    decode_model,
    load_model_modern,
    write_models_binary,
)
from export_animations import (
    FrameDef,
    FrameBaseDef,
    _parse_normal_frame,
    load_modern_framebases,
)
from export_inferno_npcs import apply_recolors, apply_scale

MODERN_NPC_CONFIG_GROUP = 9
MODERN_SEQ_CONFIG_GROUP = 12
MODERN_FRAME_INDEX = 0
MODERN_FRAMEBASE_INDEX = 1
MODERN_MAYA_ANIM_INDEX = 22

SYNTHETIC_MODEL_BASE = 0xC0000

ANIM2_MAGIC = b"ANM2"
ANIM_FORMAT_VERSION_MAYA = 3
ANIM_HEADER_SIZE = 24
ANIM_FLAG_NORMAL_FRAMES = 1 << 0
ANIM_FLAG_PRESENTATION_METADATA_OMITTED = 1 << 1
ANIM_FLAG_MAYA_BAKED_FRAMES = 1 << 2
ANIM_FRAME_LEGACY = 0
ANIM_FRAME_MAYA_BAKED = 1

MAYA_GROUP_BONE_TRANSFORMS = 1
MAYA_GROUP_ALPHA = 4
MAYA_COMPONENT_INDEX = {
    1: 0,
    2: 1,
    3: 2,
    4: 3,
    5: 4,
    6: 5,
    7: 6,
    8: 7,
    9: 8,
    10: 0,
    11: 1,
    12: 2,
    13: 3,
    14: 4,
    15: 5,
    16: 0,
}

COLOSSEUM_NPC_IDS = {
    12816: "Fremennik warband berserker",
    12814: "Fremennik warband archer",
    12815: "Fremennik warband seer",
    12811: "Serpent shaman",
    12810: "Jaguar warrior",
    12817: "Javelin Colossus",
    12819: "Shockwave Colossus",
    12812: "Minotaur",
    12813: "Minotaur (Red Flag)",
    12818: "Manticore",
    12821: "Sol Heredit",
    12823: "Bee Swarm",
    12825: "Healing totem",
    12826: "Solar flare",
    10880: "Arceuus greater ghost thrall",
}

COLOSSEUM_ATTACK_ANIM_IDS = {
    12810: 10847,
    12811: 10859,
    12812: 10843,
    12813: 10843,
    12814: 10850,
    12815: 10853,
    12816: 10856,
    12817: 10892,
    12818: 10869,
    12819: 10903,
    12821: 10876,
    12823: 10823,
    12825: 10828,
    12826: 0xFFFF,
    10880: 11101,
}

COLOSSEUM_RENDER_ONLY_ANIM_IDS_BY_NPC = {
    12817: (10893,),
    12818: (10868,),
    12826: (10817,),
}

COLOSSEUM_DEATH_ANIM_IDS = {
    12810: 10848,
    12811: 10860,
    12812: 10845,
    12813: 10845,
    12814: 10851,
    12815: 10854,
    12816: 10857,
    12817: 10894,
    12818: 10866,
    12819: 10895,
    12821: 10888,
    12823: 0xFFFF,
    12825: 0xFFFF,
    12826: 0xFFFF,
    10880: 11595,
}

COLOSSEUM_PROJECTILE_ANIM_IDS = {
    693,
    7856,
    7857,
    10327,
    10328,
    10329,
    10330,
    10811,
    10812,
    10896,
    10900,
    10901,
}

COLOSSEUM_PROJECTILE_ANIM_MODEL_IDS = {
    693: 3116,
    7856: 34618,
    7857: 34617,
    10896: 52586,
    10900: 52587,
    10901: 52587,
}


class CacheBinaryReader:
    """Small big-endian reader for modern cache config and Maya payloads."""

    def __init__(self, data: bytes, label: str) -> None:
        self.data = data
        self.label = label
        self.offset = 0

    def need(self, size: int) -> None:
        if self.offset + size > len(self.data):
            raise SystemExit(f"{self.label}: truncated at offset {self.offset}")

    def read_u8(self) -> int:
        self.need(1)
        value = self.data[self.offset]
        self.offset += 1
        return value

    def read_i8(self) -> int:
        value = self.read_u8()
        return value - 256 if value >= 128 else value

    def read_u16(self) -> int:
        self.need(2)
        value = (self.data[self.offset] << 8) | self.data[self.offset + 1]
        self.offset += 2
        return value

    def read_i16(self) -> int:
        value = self.read_u16()
        return value - 65536 if value >= 32768 else value

    def read_i32(self) -> int:
        self.need(4)
        value = int.from_bytes(self.data[self.offset:self.offset + 4], "big", signed=True)
        self.offset += 4
        return value

    def read_medium(self) -> int:
        self.need(3)
        value = (
            (self.data[self.offset] << 16) |
            (self.data[self.offset + 1] << 8) |
            self.data[self.offset + 2]
        )
        self.offset += 3
        return value

    def read_float(self) -> float:
        self.need(4)
        value = struct.unpack(">f", self.data[self.offset:self.offset + 4])[0]
        self.offset += 4
        return value

    def read_short_smart(self) -> int:
        self.need(1)
        if self.data[self.offset] < 128:
            return self.read_u8() - 64
        return self.read_u16() - 49152

    def remaining(self) -> int:
        return len(self.data) - self.offset

    def skip(self, size: int) -> None:
        self.need(size)
        self.offset += size

    def read_string(self) -> str:
        start = self.offset
        while self.offset < len(self.data) and self.data[self.offset] != 0:
            self.offset += 1
        self.need(1)
        value = self.data[start:self.offset].decode("cp1252")
        self.offset += 1
        return value


@dataclass
class ColosseumSequence:
    """Modern sequence data with retained Maya fields."""

    seq_id: int
    frame_count: int = 0
    frame_delays: list[int] = field(default_factory=list)
    primary_frame_ids: list[int] = field(default_factory=list)
    frame_step: int = -1
    interleave_order: list[int] = field(default_factory=list)
    forced_priority: int = 5
    max_loops: int = 99
    precedence_animating: int = -1
    walk_flag: int = -1
    anim_maya_masks: list[int] = field(default_factory=list)
    maya_id: int = -1
    maya_start: int = -1
    maya_end: int = -1
    maya_frames: list[list[int]] = field(default_factory=list)

    def has_maya(self) -> bool:
        """Return whether this sequence is backed by a Maya cached model."""
        return self.maya_id >= 0

def parse_colosseum_sequence(seq_id: int, data: bytes) -> ColosseumSequence:
    """Parse a modern sequence and retain opcode 13 Maya metadata."""
    seq = ColosseumSequence(seq_id=seq_id)
    buf = CacheBinaryReader(data, f"sequence {seq_id}")
    while True:
        opcode = buf.read_u8()
        if opcode == 0:
            break
        if opcode == 1:
            seq.frame_count = buf.read_u16()
            seq.frame_delays = [buf.read_u16() for _ in range(seq.frame_count)]
            file_ids = [buf.read_u16() for _ in range(seq.frame_count)]
            group_ids = [buf.read_u16() for _ in range(seq.frame_count)]
            seq.primary_frame_ids = [
                (group_ids[i] << 16) | file_ids[i] for i in range(seq.frame_count)
            ]
        elif opcode == 2:
            seq.frame_step = buf.read_u16()
        elif opcode == 3:
            seq.interleave_order = [buf.read_u8() for _ in range(buf.read_u8())]
        elif opcode == 4:
            pass
        elif opcode == 5:
            seq.forced_priority = buf.read_u8()
        elif opcode in (6, 7):
            buf.read_u16()
        elif opcode == 8:
            seq.max_loops = buf.read_u8()
        elif opcode == 9:
            seq.precedence_animating = buf.read_u8()
        elif opcode == 10:
            seq.walk_flag = buf.read_u8()
        elif opcode == 11:
            buf.read_u8()
        elif opcode == 12:
            count = buf.read_u8()
            for _ in range(count * 2):
                buf.read_u16()
        elif opcode == 13:
            seq.maya_id = buf.read_i32()
        elif opcode == 14:
            count = buf.read_u16()
            for _ in range(count):
                buf.read_u16()
                buf.skip(6)
        elif opcode == 15:
            seq.maya_start = buf.read_u16()
            seq.maya_end = buf.read_u16()
        elif opcode == 16:
            buf.read_u8()
        elif opcode == 17:
            seq.anim_maya_masks = [buf.read_u8() for _ in range(buf.read_u8())]
        elif opcode == 18:
            buf.read_string()
        elif opcode == 19:
            pass
        else:
            raise SystemExit(f"export_colosseum_npcs: unknown sequence opcode {opcode} in {seq_id}")

    if seq.has_maya():
        if seq.maya_start < 0 or seq.maya_end <= seq.maya_start:
            raise SystemExit(f"export_colosseum_npcs: Maya sequence {seq_id} missing valid range")
        seq.frame_count = seq.maya_end - seq.maya_start
        seq.frame_delays = [1] * seq.frame_count
        seq.primary_frame_ids = [-1] * seq.frame_count
        seq.interleave_order = []
    elif seq.frame_count == 0:
        seq.frame_count = 1
        seq.frame_delays = [1]
        seq.primary_frame_ids = [-1]
    if seq.walk_flag == -1:
        seq.walk_flag = 0 if not seq.interleave_order and not seq.anim_maya_masks else 2
    return seq


def _decode_type3_animaya_weights(raw: bytes, vertex_count: int) -> tuple[list[list[int]], list[list[int]]]:
    """Decode type-3 model Animaya bone indices and weights."""
    if len(raw) < 26 or raw[-2:] != b"\xff\xfd":
        return [[] for _ in range(vertex_count)], [[] for _ in range(vertex_count)]

    footer = len(raw) - 26
    var9 = int.from_bytes(raw[footer:footer + 2], "big")
    var10 = int.from_bytes(raw[footer + 2:footer + 4], "big")
    var11 = raw[footer + 4]
    var12 = raw[footer + 5]
    var13 = raw[footer + 6]
    var14 = raw[footer + 7]
    var15 = raw[footer + 8]
    var16 = raw[footer + 9]
    var17 = raw[footer + 10]
    var18 = raw[footer + 11]
    var22 = int.from_bytes(raw[footer + 18:footer + 20], "big")
    var23 = int.from_bytes(raw[footer + 20:footer + 22], "big")
    var24 = int.from_bytes(raw[footer + 22:footer + 24], "big")
    if var9 != vertex_count:
        raise SystemExit("export_colosseum_npcs: Animaya vertex count mismatch")

    tex_type0 = 0
    tex_type13 = 0
    tex_type2 = 0
    for tex_idx in range(var11):
        tex_type = raw[tex_idx]
        if tex_type == 0:
            tex_type0 += 1
        if 1 <= tex_type <= 3:
            tex_type13 += 1
        if tex_type == 2:
            tex_type2 += 1

    offset = var11 + var9
    if var12 == 1:
        offset += var10
    offset += var10
    if var13 == 255:
        offset += var10
    if var15 == 1:
        offset += var10
    animaya_stream_offset = offset
    offset += var24
    if var14 == 1:
        offset += var10
    offset += var22
    if var16 == 1:
        offset += var10 * 2
    offset += var23
    offset += var10 * 2
    offset += int.from_bytes(raw[footer + 12:footer + 14], "big")
    offset += int.from_bytes(raw[footer + 14:footer + 16], "big")
    offset += int.from_bytes(raw[footer + 16:footer + 18], "big")
    offset += tex_type0 * 6
    offset += tex_type13 * 14
    offset += tex_type2 * 2

    bone_indices = [[] for _ in range(vertex_count)]
    bone_weights = [[] for _ in range(vertex_count)]
    if var18 != 1:
        return bone_indices, bone_weights

    reader = CacheBinaryReader(raw, "type3 Animaya weights")
    reader.offset = animaya_stream_offset + (var9 if var17 == 1 else 0)
    for vertex in range(vertex_count):
        count = reader.read_u8()
        for _ in range(count):
            bone_indices[vertex].append(reader.read_u8())
            bone_weights[vertex].append(reader.read_u8())
    return bone_indices, bone_weights


def attach_animaya_weights(model: ModelData, raw: bytes) -> None:
    """Attach Animaya bone weights to a decoded model object."""
    bone_indices, bone_weights = _decode_type3_animaya_weights(raw, model.vertex_count)
    setattr(model, "maya_bone_indices", bone_indices)
    setattr(model, "maya_bone_weights", bone_weights)


def merge_animaya_weights(merged: ModelData, parts: list[ModelData]) -> None:
    """Attach merged Animaya weights after concatenating model parts."""
    merged_indices: list[list[int]] = []
    merged_weights: list[list[int]] = []
    for part in parts:
        part_indices = getattr(part, "maya_bone_indices", [[] for _ in range(part.vertex_count)])
        part_weights = getattr(part, "maya_bone_weights", [[] for _ in range(part.vertex_count)])
        merged_indices.extend([list(values) for values in part_indices])
        merged_weights.extend([list(values) for values in part_weights])
    if len(merged_indices) != merged.vertex_count:
        raise SystemExit("export_colosseum_npcs: merged Animaya weight count mismatch")
    setattr(merged, "maya_bone_indices", merged_indices)
    setattr(merged, "maya_bone_weights", merged_weights)


@dataclass
class MayaCurveKey:
    """One key in a Maya curve channel."""

    frame: int
    value: float
    in_x: float
    in_y: float
    out_x: float
    out_y: float


@dataclass
class MayaCurve:
    """Evaluable Maya animation curve."""

    weighted: bool
    keys: list[MayaCurveKey]

    def evaluate(self, frame: int) -> float:
        """Evaluate the curve at an integer Maya frame."""
        if not self.keys:
            return 0.0
        if frame <= self.keys[0].frame:
            return self.keys[0].value
        if frame >= self.keys[-1].frame:
            return self.keys[-1].value
        left = self.keys[0]
        right = self.keys[-1]
        for idx in range(len(self.keys) - 1):
            if self.keys[idx].frame <= frame <= self.keys[idx + 1].frame:
                left = self.keys[idx]
                right = self.keys[idx + 1]
                break
        if left.out_x == 0.0 and left.out_y == 0.0:
            return left.value
        if left.out_x >= 3.3e38 and left.out_y >= 3.3e38:
            return right.value if frame != left.frame else left.value
        if right.frame == left.frame:
            return left.value

        p0x = float(left.frame)
        p0y = left.value
        p1x = p0x + left.out_x / 3.0
        p1y = p0y + left.out_y / 3.0
        p3x = float(right.frame)
        p3y = right.value
        p2x = p3x - right.in_x / 3.0
        p2y = p3y - right.in_y / 3.0
        lo = 0.0
        hi = 1.0
        target = float(frame)
        for _ in range(24):
            mid = (lo + hi) * 0.5
            x = cubic_bezier(p0x, p1x, p2x, p3x, mid)
            if x < target:
                lo = mid
            else:
                hi = mid
        t = (lo + hi) * 0.5
        return cubic_bezier(p0y, p1y, p2y, p3y, t)


@dataclass
class MayaBone:
    """Skeleton bone data needed for Maya skinning."""

    parent_index: int
    base_matrices: list[list[float]]
    default_rotations: list[list[float]]
    default_translations: list[list[float]]
    default_scales: list[list[float]]


@dataclass
class MayaSkeleton:
    """Parsed modern skeleton with optional Animaya bone transforms."""

    skeleton_id: int
    legacy_count: int
    bones: list[MayaBone]
    bind_frame_count: int


@dataclass
class MayaAnimation:
    """Parsed Maya animation payload and resolved skeleton."""

    maya_id: int
    version: int
    skeleton: MayaSkeleton
    bind_frame: int
    bone_curves: list[list[MayaCurve | None]]


@dataclass
class MayaBakeTarget:
    """Model-space inputs for client-order Maya baking."""

    renderer_model: ModelData
    skin_model: ModelData
    width_scale: int
    height_scale: int


def cubic_bezier(p0: float, p1: float, p2: float, p3: float, t: float) -> float:
    """Evaluate a scalar cubic Bezier."""
    u = 1.0 - t
    return u * u * u * p0 + 3.0 * u * u * t * p1 + 3.0 * u * t * t * p2 + t * t * t * p3


def mat_identity() -> list[float]:
    """Return the client matrix identity."""
    return [
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0,
    ]


def mat_zero() -> list[float]:
    """Return the client matrix zero value."""
    return [0.0] * 16


def mat_add_in_place(dst: list[float], src: list[float]) -> None:
    """Add src into dst elementwise."""
    for idx in range(16):
        dst[idx] += src[idx]


def mat_scale_uniform(m: list[float], scale: float) -> list[float]:
    """Scale every matrix element by a uniform scalar."""
    return [value * scale for value in m]


def mat_mul(a: list[float], b: list[float]) -> list[float]:
    """Port TransformationMatrix.method9426 for the client matrix layout."""
    return [
        b[12] * a[3] + b[8] * a[2] + a[0] * b[0] + b[4] * a[1],
        a[2] * b[9] + a[0] * b[1] + b[5] * a[1] + a[3] * b[13],
        a[3] * b[14] + b[6] * a[1] + b[2] * a[0] + a[2] * b[10],
        a[3] * b[15] + a[2] * b[11] + b[3] * a[0] + b[7] * a[1],
        a[6] * b[8] + a[5] * b[4] + b[0] * a[4] + a[7] * b[12],
        b[13] * a[7] + a[5] * b[5] + b[1] * a[4] + a[6] * b[9],
        b[14] * a[7] + a[6] * b[10] + b[2] * a[4] + a[5] * b[6],
        b[15] * a[7] + a[4] * b[3] + a[5] * b[7] + a[6] * b[11],
        b[12] * a[11] + b[8] * a[10] + a[8] * b[0] + a[9] * b[4],
        b[13] * a[11] + b[1] * a[8] + b[5] * a[9] + a[10] * b[9],
        b[14] * a[11] + a[8] * b[2] + b[6] * a[9] + a[10] * b[10],
        a[10] * b[11] + a[9] * b[7] + a[8] * b[3] + a[11] * b[15],
        b[12] * a[15] + b[4] * a[13] + b[0] * a[12] + a[14] * b[8],
        b[13] * a[15] + b[1] * a[12] + b[5] * a[13] + b[9] * a[14],
        a[15] * b[14] + b[10] * a[14] + b[6] * a[13] + a[12] * b[2],
        a[14] * b[11] + a[13] * b[7] + b[3] * a[12] + a[15] * b[15],
    ]


def mat_inverse(m: list[float]) -> list[float]:
    """Invert a 4x4 matrix with Gauss-Jordan elimination."""
    rows = [[m[r * 4 + c] for c in range(4)] + [1.0 if r == c else 0.0 for c in range(4)]
            for r in range(4)]
    for col in range(4):
        pivot = max(range(col, 4), key=lambda r: abs(rows[r][col]))
        if abs(rows[pivot][col]) < 1e-8:
            raise SystemExit("export_colosseum_npcs: singular Maya bind matrix")
        rows[col], rows[pivot] = rows[pivot], rows[col]
        factor = rows[col][col]
        rows[col] = [value / factor for value in rows[col]]
        for row in range(4):
            if row == col:
                continue
            scale = rows[row][col]
            rows[row] = [rows[row][i] - scale * rows[col][i] for i in range(8)]
    return [rows[r][c + 4] for r in range(4) for c in range(4)]


def mat_default_rotation(m: list[float]) -> list[float]:
    """Port TransformationMatrix.method9420 rotation extraction."""
    x = -math.asin(max(-1.0, min(1.0, m[6])))
    cos_x = math.cos(x)
    y = 0.0
    z = 0.0
    if abs(cos_x) > 0.005:
        y = math.atan2(m[2], m[10])
        z = math.atan2(m[4], m[5])
    else:
        if m[6] < 0.0:
            y = math.atan2(m[1], m[0])
        else:
            y = -math.atan2(m[1], m[0])
    return [x, y, z]


def mat_default_scale(m: list[float]) -> list[float]:
    """Port TransformationMatrix.method9495 scale extraction."""
    return [
        math.sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]),
        math.sqrt(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]),
        math.sqrt(m[8] * m[8] + m[9] * m[9] + m[10] * m[10]),
    ]


def rotation_x(angle: float) -> list[float]:
    """Return a client-layout X rotation matrix."""
    c = math.cos(angle)
    s = math.sin(angle)
    return [
        1.0, 0.0, 0.0, 0.0,
        0.0, c, s, 0.0,
        0.0, -s, c, 0.0,
        0.0, 0.0, 0.0, 1.0,
    ]


def rotation_y(angle: float) -> list[float]:
    """Return a client-layout Y rotation matrix."""
    c = math.cos(angle)
    s = math.sin(angle)
    return [
        c, 0.0, -s, 0.0,
        0.0, 1.0, 0.0, 0.0,
        s, 0.0, c, 0.0,
        0.0, 0.0, 0.0, 1.0,
    ]


def rotation_z(angle: float) -> list[float]:
    """Return a client-layout Z rotation matrix."""
    c = math.cos(angle)
    s = math.sin(angle)
    return [
        c, s, 0.0, 0.0,
        -s, c, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0,
    ]


def scale_xyz(x: float, y: float, z: float) -> list[float]:
    """Return a non-uniform scale matrix."""
    out = mat_identity()
    out[0] = x
    out[5] = y
    out[10] = z
    return out


def compose_local_transform(rotation: list[float], translation: list[float], scale: list[float]) -> list[float]:
    """Build the local bone transform in class146 order."""
    matrix = mat_identity()
    matrix = mat_mul(matrix, rotation_z(rotation[2]))
    matrix = mat_mul(matrix, rotation_x(rotation[0]))
    matrix = mat_mul(matrix, rotation_y(rotation[1]))
    matrix = mat_mul(matrix, scale_xyz(scale[0], scale[1], scale[2]))
    matrix[12] = translation[0]
    matrix[13] = translation[1]
    matrix[14] = translation[2]
    return matrix


def transform_vertex(matrix: list[float], x: int, y: int, z: int) -> tuple[int, int, int]:
    """Apply the client Maya transform to one model vertex."""
    fx = float(x)
    fy = float(-y)
    fz = float(-z)
    out_x = matrix[0] * fx + matrix[4] * fy + matrix[8] * fz + matrix[12]
    out_y = -(matrix[1] * fx + matrix[5] * fy + matrix[9] * fz + matrix[13])
    out_z = -(matrix[2] * fx + matrix[6] * fy + matrix[10] * fz + matrix[14])
    return int(out_x), int(out_y), int(out_z)


def clamp_i16(value: int) -> int:
    """Clamp a baked vertex coordinate to the renderer's int16 storage."""
    return max(-32768, min(32767, value))


def scale_model_coord(value: int, scale: int) -> int:
    """Scale a model coordinate with client integer truncation semantics."""
    scaled = value * scale
    if scaled >= 0:
        return scaled // 128
    return -((-scaled) // 128)


def apply_npc_scale_to_baked_frame(
    frame: list[int],
    width_scale: int,
    height_scale: int,
) -> list[int]:
    """Apply NPC resize to a baked client-space vertex frame."""
    if width_scale == 128 and height_scale == 128:
        return frame
    scaled: list[int] = []
    for offset in range(0, len(frame), 3):
        scaled.extend([
            clamp_i16(scale_model_coord(frame[offset], width_scale)),
            clamp_i16(scale_model_coord(frame[offset + 1], height_scale)),
            clamp_i16(scale_model_coord(frame[offset + 2], width_scale)),
        ])
    return scaled


def parse_maya_curve(reader: CacheBinaryReader, version: int) -> MayaCurve:
    """Parse class139 curve data."""
    key_count = reader.read_u16()
    reader.read_u8()
    reader.read_u8()
    reader.read_u8()
    weighted = reader.read_u8() != 0
    keys: list[MayaCurveKey] = []
    for _ in range(key_count):
        keys.append(MayaCurveKey(
            frame=reader.read_i16(),
            value=reader.read_float(),
            in_x=reader.read_float(),
            in_y=reader.read_float(),
            out_x=reader.read_float(),
            out_y=reader.read_float(),
        ))
    return MayaCurve(weighted=weighted, keys=keys)


def parse_maya_skeleton(skeleton_id: int, data: bytes) -> MayaSkeleton:
    """Parse a modern Skeleton including optional class251 bone transforms."""
    reader = CacheBinaryReader(data, f"skeleton {skeleton_id}")
    legacy_count = reader.read_u8()
    for _ in range(legacy_count):
        reader.read_u8()
    label_lengths = [reader.read_u8() for _ in range(legacy_count)]
    for length in label_lengths:
        for _ in range(length):
            reader.read_u8()
    if reader.remaining() <= 0:
        raise SystemExit(f"export_colosseum_npcs: skeleton {skeleton_id} missing Maya bone data")

    bone_count = reader.read_u16()
    if bone_count <= 0:
        raise SystemExit(f"export_colosseum_npcs: skeleton {skeleton_id} has zero Maya bones")
    bind_frame_count = reader.read_u8()
    bones: list[MayaBone] = []
    for _ in range(bone_count):
        parent_index = reader.read_i16()
        base_matrices: list[list[float]] = []
        for _frame in range(bind_frame_count):
            matrix = [reader.read_float() for _ in range(16)]
            reader.read_float()
            reader.read_float()
            reader.read_float()
            base_matrices.append(matrix)
        default_rotations: list[list[float]] = []
        default_translations: list[list[float]] = []
        default_scales: list[list[float]] = []
        for matrix in base_matrices:
            default_rotations.append(mat_default_rotation(mat_inverse(matrix)))
            default_translations.append([matrix[12], matrix[13], matrix[14]])
            default_scales.append(mat_default_scale(matrix))
        bones.append(MayaBone(
            parent_index=parent_index,
            base_matrices=base_matrices,
            default_rotations=default_rotations,
            default_translations=default_translations,
            default_scales=default_scales,
        ))
    return MayaSkeleton(
        skeleton_id=skeleton_id,
        legacy_count=legacy_count,
        bones=bones,
        bind_frame_count=bind_frame_count,
    )


def read_maya_animation(reader: ModernCacheReader, maya_id: int) -> MayaAnimation:
    """Load a Maya animation payload from cache index 22 and its skeleton from index 1."""
    group_id = (maya_id >> 16) & 0xFFFF
    file_id = maya_id & 0xFFFF
    files = reader.read_group(MODERN_MAYA_ANIM_INDEX, group_id)
    if file_id not in files:
        raise SystemExit(f"export_colosseum_npcs: Maya file {maya_id} missing from index 22")
    payload = files[file_id]
    payload_reader = CacheBinaryReader(payload, f"Maya animation {maya_id}")
    version = payload_reader.read_u8()
    skeleton_id = payload_reader.read_u16()
    skeleton_files = reader.read_group(MODERN_FRAMEBASE_INDEX, skeleton_id)
    if 0 not in skeleton_files:
        raise SystemExit(f"export_colosseum_npcs: Maya skeleton {skeleton_id} file 0 missing")
    skeleton = parse_maya_skeleton(skeleton_id, skeleton_files[0])
    payload_reader.read_u16()
    payload_reader.read_u16()
    bind_frame = payload_reader.read_u8()
    curve_count = payload_reader.read_u16()
    if bind_frame < 0 or bind_frame >= skeleton.bind_frame_count:
        raise SystemExit(
            f"export_colosseum_npcs: Maya {maya_id} bind frame {bind_frame} outside skeleton range"
        )
    bone_curves: list[list[MayaCurve | None]] = [
        [None] * 9 for _ in range(len(skeleton.bones))
    ]
    for _ in range(curve_count):
        group = payload_reader.read_u8()
        bone_index = payload_reader.read_short_smart()
        component_ordinal = payload_reader.read_u8()
        curve = parse_maya_curve(payload_reader, version)
        if group == MAYA_GROUP_ALPHA:
            continue
        if group != MAYA_GROUP_BONE_TRANSFORMS:
            raise SystemExit(f"export_colosseum_npcs: Maya {maya_id} has unsupported curve group {group}")
        component = MAYA_COMPONENT_INDEX.get(component_ordinal)
        if component is None or component >= 9:
            raise SystemExit(
                f"export_colosseum_npcs: Maya {maya_id} has unsupported component {component_ordinal}"
            )
        if bone_index < 0 or bone_index >= len(bone_curves):
            raise SystemExit(f"export_colosseum_npcs: Maya {maya_id} bone index {bone_index} invalid")
        bone_curves[bone_index][component] = curve
    return MayaAnimation(
        maya_id=maya_id,
        version=version,
        skeleton=skeleton,
        bind_frame=bind_frame,
        bone_curves=bone_curves,
    )


def maya_bone_local_transform(animation: MayaAnimation, bone_index: int, frame: int) -> list[float]:
    """Evaluate one bone's local transform for a Maya frame."""
    bone = animation.skeleton.bones[bone_index]
    rotation = list(bone.default_rotations[animation.bind_frame])
    translation = list(bone.default_translations[animation.bind_frame])
    scale = list(bone.default_scales[animation.bind_frame])
    curves = animation.bone_curves[bone_index]
    for component in range(3):
        if curves[component] is not None:
            rotation[component] = curves[component].evaluate(frame)
    for component in range(3):
        curve = curves[component + 3]
        if curve is not None:
            translation[component] = curve.evaluate(frame)
    for component in range(3):
        curve = curves[component + 6]
        if curve is not None:
            scale[component] = curve.evaluate(frame)
    return compose_local_transform(rotation, translation, scale)


def maya_world_matrices(animation: MayaAnimation, frame: int) -> list[list[float]]:
    """Evaluate every final bone matrix for one Maya frame."""
    skeleton = animation.skeleton
    local = [
        maya_bone_local_transform(animation, bone_index, frame)
        for bone_index in range(len(skeleton.bones))
    ]
    current_world: list[list[float] | None] = [None] * len(skeleton.bones)
    bind_world: list[list[float] | None] = [None] * len(skeleton.bones)

    def current_for(index: int) -> list[float]:
        cached = current_world[index]
        if cached is not None:
            return cached
        parent = skeleton.bones[index].parent_index
        matrix = local[index]
        if parent >= 0:
            matrix = mat_mul(matrix, current_for(parent))
        current_world[index] = matrix
        return matrix

    def bind_for(index: int) -> list[float]:
        cached = bind_world[index]
        if cached is not None:
            return cached
        bone = skeleton.bones[index]
        matrix = bone.base_matrices[animation.bind_frame]
        if bone.parent_index >= 0:
            matrix = mat_mul(matrix, bind_for(bone.parent_index))
        bind_world[index] = matrix
        return matrix

    final_matrices: list[list[float]] = []
    for bone_index in range(len(skeleton.bones)):
        final_matrices.append(mat_mul(mat_inverse(bind_for(bone_index)), current_for(bone_index)))
    return final_matrices


def bake_maya_frame(animation: MayaAnimation, model: ModelData, frame: int) -> list[int]:
    """Bake a Maya animation frame into model-space vertex triples."""
    bone_indices: list[list[int]] = getattr(model, "maya_bone_indices", [])
    bone_weights: list[list[int]] = getattr(model, "maya_bone_weights", [])
    if len(bone_indices) != model.vertex_count or len(bone_weights) != model.vertex_count:
        raise SystemExit(f"export_colosseum_npcs: model {model.model_id} missing Animaya weights")
    matrices = maya_world_matrices(animation, frame)
    baked: list[int] = []
    animated_vertices = 0
    for vertex in range(model.vertex_count):
        x = model.vertices_x[vertex]
        y = model.vertices_y[vertex]
        z = model.vertices_z[vertex]
        if bone_indices[vertex]:
            weighted = mat_zero()
            for bone_index, weight in zip(bone_indices[vertex], bone_weights[vertex], strict=True):
                if bone_index >= len(matrices):
                    raise SystemExit(
                        f"export_colosseum_npcs: model {model.model_id} references missing Maya bone {bone_index}"
                    )
                mat_add_in_place(weighted, mat_scale_uniform(matrices[bone_index], weight / 255.0))
            x, y, z = transform_vertex(weighted, x, y, z)
            animated_vertices += 1
        baked.extend([clamp_i16(x), clamp_i16(y), clamp_i16(z)])
    if animated_vertices == 0:
        raise SystemExit(f"export_colosseum_npcs: model {model.model_id} has no animated Maya vertices")
    return baked


def assert_maya_bind_pose_matches_renderer_model(
    animation: MayaAnimation,
    target: MayaBakeTarget,
    seq_id: int,
) -> None:
    """Assert no-animation Maya skinning reproduces the exported base mesh."""
    rest_animation = MayaAnimation(
        maya_id=animation.maya_id,
        version=animation.version,
        skeleton=animation.skeleton,
        bind_frame=animation.bind_frame,
        bone_curves=[[None] * 9 for _ in animation.skeleton.bones],
    )
    baked = bake_maya_frame(rest_animation, target.skin_model, animation.bind_frame)
    baked = apply_npc_scale_to_baked_frame(
        baked, target.width_scale, target.height_scale)
    model = target.renderer_model
    if len(baked) != model.vertex_count * 3:
        raise SystemExit(
            f"export_colosseum_npcs: Maya bind-pose vertex count drift "
            f"seq {seq_id} frame={len(baked) // 3} model={model.vertex_count}"
        )

    epsilon = 3
    max_abs = 0
    worst_vertex = -1
    worst_base = (0, 0, 0)
    worst_baked = (0, 0, 0)
    for vertex in range(model.vertex_count):
        base = (
            model.vertices_x[vertex],
            model.vertices_y[vertex],
            model.vertices_z[vertex],
        )
        got = tuple(baked[vertex * 3 + axis] for axis in range(3))
        local = max(abs(got[axis] - base[axis]) for axis in range(3))
        if local > max_abs:
            max_abs = local
            worst_vertex = vertex
            worst_base = base
            worst_baked = got

    if max_abs > epsilon:
        raise SystemExit(
            f"export_colosseum_npcs: Maya bind pose mismatch seq {seq_id} "
            f"maya {animation.maya_id} max_abs={max_abs} vertex={worst_vertex} "
            f"base={worst_base} baked={worst_baked}"
        )


def bake_maya_sequence(
    reader: ModernCacheReader,
    seq: ColosseumSequence,
    target: MayaBakeTarget,
) -> None:
    """Bake every frame in a Maya-backed sequence against its NPC model."""
    animation = read_maya_animation(reader, seq.maya_id)
    assert_maya_bind_pose_matches_renderer_model(animation, target, seq.seq_id)
    seq.maya_frames = [
        apply_npc_scale_to_baked_frame(
            bake_maya_frame(animation, target.skin_model, frame),
            target.width_scale,
            target.height_scale,
        )
        for frame in range(seq.maya_start, seq.maya_end)
    ]
    if len(seq.maya_frames) != seq.frame_count:
        raise SystemExit(f"export_colosseum_npcs: Maya sequence {seq.seq_id} frame count drift")


def load_animation_model(reader: ModernCacheReader, model_id: int) -> ModelData:
    """Decode a model used only for baked animation frames."""
    raw = load_model_modern(reader, model_id)
    if raw is None:
        raise SystemExit(f"export_colosseum_npcs: animation model {model_id} missing")
    model = decode_model(model_id, raw)
    if model is None:
        raise SystemExit(f"export_colosseum_npcs: animation model {model_id} failed to decode")
    attach_animaya_weights(model, raw)
    return model


def build_npc_models(
    reader: ModernCacheReader,
    npc_files: dict[int, bytes],
) -> tuple[list[ModelData], dict[int, dict[str, int]], dict[int, MayaBakeTarget]]:
    """Decode, merge, recolor, and scale each Colosseum NPC mesh.

    Returns the merged models keyed by synthetic id plus a mapping from npc id
    to {synthetic_model_id, idle_anim, attack_anim, walk_anim, run_anim}
    for the C header.
    """
    missing_attack_anim_npc_ids = sorted(set(COLOSSEUM_NPC_IDS) - set(COLOSSEUM_ATTACK_ANIM_IDS))
    if missing_attack_anim_npc_ids:
        raise SystemExit(
            "export_colosseum_npcs: attack animation missing for npc ids "
            + ", ".join(str(npc_id) for npc_id in missing_attack_anim_npc_ids)
        )
    missing_death_anim_npc_ids = sorted(set(COLOSSEUM_NPC_IDS) - set(COLOSSEUM_DEATH_ANIM_IDS))
    if missing_death_anim_npc_ids:
        raise SystemExit(
            "export_colosseum_npcs: death animation missing for npc ids "
            + ", ".join(str(npc_id) for npc_id in missing_death_anim_npc_ids)
        )
    unknown_render_only_anim_npc_ids = sorted(
        set(COLOSSEUM_RENDER_ONLY_ANIM_IDS_BY_NPC) - set(COLOSSEUM_NPC_IDS)
    )
    if unknown_render_only_anim_npc_ids:
        raise SystemExit(
            "export_colosseum_npcs: render-only animation configured for unknown npc ids "
            + ", ".join(str(npc_id) for npc_id in unknown_render_only_anim_npc_ids)
        )

    models: list[ModelData] = []
    mapping: dict[int, dict[str, int]] = {}
    sequence_models: dict[int, MayaBakeTarget] = {}

    for npc_id, label in sorted(COLOSSEUM_NPC_IDS.items()):
        if npc_id not in npc_files:
            raise SystemExit(f"export_colosseum_npcs: npc {npc_id} ({label}) missing from cache")
        npc = decode_npc_definition(npc_id, npc_files[npc_id])
        if not npc.complete:
            raise SystemExit(
                f"export_colosseum_npcs: npc {npc_id} ({label}) hit unknown opcode "
                f"{npc.unknown_opcode}"
            )
        if not npc.models:
            raise SystemExit(f"export_colosseum_npcs: npc {npc_id} ({label}) has no models")

        parts: list[ModelData] = []
        for model_id in npc.models:
            raw = load_model_modern(reader, model_id)
            if raw is None:
                raise SystemExit(
                    f"export_colosseum_npcs: model {model_id} missing for npc {npc_id}"
                )
            decoded = decode_model(model_id, raw)
            if decoded is None:
                raise SystemExit(
                    f"export_colosseum_npcs: model {model_id} failed to decode for npc {npc_id}"
                )
            attach_animaya_weights(decoded, raw)
            parts.append(decoded)

        merged = parts[0] if len(parts) == 1 else _merge_models(parts)
        merge_animaya_weights(merged, parts)
        if npc.recolor_from:
            apply_recolors(merged, npc.recolor_from, npc.recolor_to)
        skin_model = copy.deepcopy(merged)
        apply_scale(merged, npc.width_scale, npc.height_scale)
        merged.model_id = SYNTHETIC_MODEL_BASE + npc_id
        models.append(merged)
        bake_target = MayaBakeTarget(
            renderer_model=merged,
            skin_model=skin_model,
            width_scale=npc.width_scale,
            height_scale=npc.height_scale,
        )

        idle_anim = npc.stand_anim if npc.stand_anim >= 0 else 0xFFFF
        attack_anim = COLOSSEUM_ATTACK_ANIM_IDS[npc_id]
        walk_anim = npc.walk_anim if npc.walk_anim >= 0 else 0xFFFF
        run_anim = npc.run_anim if npc.run_anim >= 0 else 0xFFFF
        death_anim = COLOSSEUM_DEATH_ANIM_IDS[npc_id]
        mapping[npc_id] = {
            "synthetic_model_id": merged.model_id,
            "idle_anim": idle_anim,
            "attack_anim": attack_anim,
            "walk_anim": walk_anim,
            "run_anim": run_anim,
            "death_anim": death_anim,
        }
        render_only_anim_ids = COLOSSEUM_RENDER_ONLY_ANIM_IDS_BY_NPC.get(npc_id, ())
        for anim_id in (
            idle_anim, attack_anim, walk_anim, run_anim, death_anim, *render_only_anim_ids
        ):
            if anim_id != 0xFFFF:
                sequence_models[anim_id] = bake_target
        print(
            f"  npc {npc_id} ({npc.name}): {merged.vertex_count}v {merged.face_count}f "
            f"idle={idle_anim} attack={attack_anim} walk={walk_anim} "
            f"run={run_anim} death={death_anim}"
        )

    return models, mapping, sequence_models


def collect_anim_ids(mapping: dict[int, dict[str, int]]) -> set[int]:
    """Gather every non-sentinel exported sequence id."""
    anim_ids: set[int] = set(COLOSSEUM_PROJECTILE_ANIM_IDS)
    for entry in mapping.values():
        for key in ("idle_anim", "attack_anim", "walk_anim", "run_anim", "death_anim"):
            value = entry[key]
            if value != 0xFFFF:
                anim_ids.add(value)
    for render_only_anim_ids in COLOSSEUM_RENDER_ONLY_ANIM_IDS_BY_NPC.values():
        anim_ids.update(render_only_anim_ids)
    return anim_ids


def assert_rigged_death_anims_are_maya(
    sequences: dict[int, ColosseumSequence],
    mapping: dict[int, dict[str, int]],
) -> None:
    """Abort if an Animaya-rigged NPC keeps a legacy framebase death sequence.

    A model whose idle or attack sequence baked as Maya is Animaya-rigged: its
    vertices carry Maya bone weights, not the legacy label-groups a framebase
    animation drives. Playing a legacy death sequence on that mesh flings vertex
    groups into the sky. Enforce a Maya death for every rigged NPC so a wrong id
    fails the export instead of shipping a skyward-warp death to the viewer.
    """
    for npc_id, entry in sorted(mapping.items()):
        idle = entry["idle_anim"]
        attack = entry["attack_anim"]
        death = entry["death_anim"]
        rigged = (idle != 0xFFFF and sequences[idle].has_maya()) or (
            attack != 0xFFFF and sequences[attack].has_maya()
        )
        if not rigged or death == 0xFFFF:
            continue
        if not sequences[death].has_maya():
            raise SystemExit(
                f"export_colosseum_npcs: npc {npc_id} is Animaya-rigged but death "
                f"sequence {death} is a legacy framebase animation; a legacy frame on "
                f"a Maya-skinned mesh warps the corpse into the sky. Set the model's "
                f"Maya death sequence id in COLOSSEUM_DEATH_ANIM_IDS."
            )


def export_animations(
    reader: ModernCacheReader,
    output_path: Path,
    anim_ids: set[int],
    sequence_models: dict[int, MayaBakeTarget],
    mapping: dict[int, dict[str, int]],
) -> None:
    """Resolve legacy and Maya sequences for the requested ids and write v3 binary."""
    seq_files = reader.read_group(2, MODERN_SEQ_CONFIG_GROUP)

    sequences: dict[int, ColosseumSequence] = {}
    for seq_id in sorted(anim_ids):
        if seq_id not in seq_files:
            raise SystemExit(f"export_colosseum_npcs: sequence {seq_id} missing from cache")
        seq = parse_colosseum_sequence(seq_id, seq_files[seq_id])
        if seq.has_maya():
            target = sequence_models.get(seq_id)
            if target is None:
                model_id = COLOSSEUM_PROJECTILE_ANIM_MODEL_IDS.get(seq_id)
                if model_id is None:
                    raise SystemExit(
                        f"export_colosseum_npcs: Maya sequence {seq_id} has no owning model"
                    )
                model = load_animation_model(reader, model_id)
                target = MayaBakeTarget(
                    renderer_model=model,
                    skin_model=model,
                    width_scale=128,
                    height_scale=128,
                )
            bake_maya_sequence(reader, seq, target)
            print(
                f"  sequence {seq_id}: Maya id={seq.maya_id} "
                f"frames={seq.frame_count} vertices={target.renderer_model.vertex_count}"
            )
        sequences[seq_id] = seq

    assert_rigged_death_anims_are_maya(sequences, mapping)

    needed_groups: set[int] = set()
    for seq in sequences.values():
        if seq.has_maya():
            continue
        for frame_id in seq.primary_frame_ids:
            if frame_id != -1:
                needed_groups.add(frame_id >> 16)

    needed_base_ids: set[int] = set()
    raw_frame_data: dict[int, dict[int, bytes]] = {}
    for group_id in sorted(needed_groups):
        try:
            files = reader.read_group(MODERN_FRAME_INDEX, group_id)
        except (KeyError, FileNotFoundError):
            raise SystemExit(f"export_colosseum_npcs: frame archive {group_id} missing")
        raw_frame_data[group_id] = files
        for file_data in files.values():
            if len(file_data) >= 2:
                needed_base_ids.add((file_data[0] << 8) | file_data[1])

    framebases = load_modern_framebases(reader, needed_base_ids)

    all_frames: dict[int, dict[int, FrameDef]] = {}
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

    write_colosseum_animations_binary(output_path, framebases, all_frames, sequences)


def _as_i8(value: int) -> int:
    """Clamp a value into signed int8 range."""
    return max(-128, min(127, int(value)))


def write_colosseum_animations_binary(
    output_path: Path,
    framebases: dict[int, FrameBaseDef],
    all_frames: dict[int, dict[int, FrameDef]],
    sequences: dict[int, ColosseumSequence],
) -> None:
    """Write Colosseum animation data with explicit legacy and Maya frame kinds."""
    needed_bases: set[int] = set()
    for seq in sequences.values():
        if seq.has_maya():
            if len(seq.maya_frames) != seq.frame_count:
                raise SystemExit(f"export_colosseum_npcs: Maya sequence {seq.seq_id} was not baked")
            continue
        for frame_id in seq.primary_frame_ids:
            if frame_id == -1:
                continue
            group_id = frame_id >> 16
            file_id = frame_id & 0xFFFF
            group = all_frames.get(group_id)
            if group is None or file_id not in group:
                raise SystemExit(
                    f"export_colosseum_npcs: frame {frame_id} missing for sequence {seq.seq_id}"
                )
            needed_bases.add(group[file_id].framebase_id)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    sorted_bases = sorted(needed_bases)
    sequence_frame_count = sum(seq.frame_count for seq in sequences.values())
    with output_path.open("wb") as f:
        f.write(ANIM2_MAGIC)
        f.write(struct.pack(
            "<HHIIII",
            ANIM_FORMAT_VERSION_MAYA,
            ANIM_HEADER_SIZE,
            len(sorted_bases),
            len(sequences),
            sequence_frame_count,
            ANIM_FLAG_NORMAL_FRAMES |
            ANIM_FLAG_PRESENTATION_METADATA_OMITTED |
            ANIM_FLAG_MAYA_BAKED_FRAMES,
        ))

        for base_id in sorted_bases:
            fb = framebases[base_id]
            f.write(struct.pack("<H", base_id))
            f.write(struct.pack("B", fb.slot_count))
            for transform_type in fb.types:
                f.write(struct.pack("B", transform_type))
            for frame_map in fb.frame_maps:
                f.write(struct.pack("B", len(frame_map)))
                for entry in frame_map:
                    f.write(struct.pack("B", entry))

        for seq in sequences.values():
            f.write(struct.pack("<H", seq.seq_id))
            f.write(struct.pack("<H", seq.frame_count))
            f.write(struct.pack("B", len(seq.interleave_order)))
            for value in seq.interleave_order:
                f.write(struct.pack("B", value))
            f.write(struct.pack("b", _as_i8(seq.walk_flag)))
            for frame_index in range(seq.frame_count):
                delay = seq.frame_delays[frame_index]
                f.write(struct.pack("<H", max(0, delay)))
                if seq.has_maya():
                    frame = seq.maya_frames[frame_index]
                    vertex_count = len(frame) // 3
                    f.write(struct.pack("B", ANIM_FRAME_MAYA_BAKED))
                    f.write(struct.pack("<H", vertex_count))
                    for coord in frame:
                        f.write(struct.pack("<h", coord))
                    continue

                f.write(struct.pack("B", ANIM_FRAME_LEGACY))
                frame_id = seq.primary_frame_ids[frame_index]
                if frame_id == -1:
                    f.write(struct.pack("<HB", 0xFFFF, 0))
                    continue
                group_id = frame_id >> 16
                file_id = frame_id & 0xFFFF
                frame = all_frames[group_id][file_id]
                f.write(struct.pack("<H", frame.framebase_id))
                f.write(struct.pack("B", frame.translator_count))
                for transform_index in range(frame.translator_count):
                    f.write(struct.pack("B", frame.slot_indices[transform_index]))
                    f.write(struct.pack("<h", frame.dx[transform_index]))
                    f.write(struct.pack("<h", frame.dy[transform_index]))
                    f.write(struct.pack("<h", frame.dz[transform_index]))

    print(
        f"  wrote {output_path}: {len(sorted_bases)} framebases, "
        f"{len(sequences)} sequences, {sequence_frame_count} frames"
    )


def write_colosseum_header(
    header_path: Path,
    mapping: dict[int, dict[str, int]],
) -> None:
    """Emit the standalone NPC_MODEL_MAP_COLOSSEUM_GEN table for the viewer."""
    lines: list[str] = []
    lines.append("/* generated by ocean/osrs/tools/export_colosseum_npcs.py -- do not edit */")
    lines.append("#ifndef NPC_MODELS_COLOSSEUM_H")
    lines.append("#define NPC_MODELS_COLOSSEUM_H")
    lines.append("")
    lines.append('#include <stdint.h>')
    lines.append('#include "npc_models.h"  /* for NpcModelMapping typedef */')
    lines.append("")
    lines.append("static const NpcModelMapping NPC_MODEL_MAP_COLOSSEUM_GEN[] = {")
    for npc_id, entry in sorted(mapping.items()):
        label = COLOSSEUM_NPC_IDS[npc_id]
        lines.append(
            f"    {{{npc_id}, 0x{entry['synthetic_model_id']:X}, "
            f"{entry['idle_anim']}, {entry['attack_anim']}, "
            f"{entry['walk_anim']}, {entry['run_anim']}}},  /* {label} */"
        )
    lines.append("};")
    lines.append("")
    lines.append("#endif /* NPC_MODELS_COLOSSEUM_H */")
    lines.append("")
    header_path.write_text("\n".join(lines), encoding="utf-8")


def patch_npc_models_header(npc_models_path: Path) -> None:
    """Idempotently wire the Colosseum table into the shared npc_models.h.

    Adds the ``npc_models_colosseum.h`` include and a lookup arm so the viewer's
    ``npc_model_lookup`` resolves Colosseum def ids. The lookup arm only fires for
    Colosseum def ids, so this never disturbs the Zulrah or Inferno paths.
    """
    if not npc_models_path.is_file():
        raise SystemExit(f"export_colosseum_npcs: shared header missing: {npc_models_path}")
    text = npc_models_path.read_text(encoding="utf-8")

    include_line = '#include "npc_models_colosseum.h"'
    if include_line not in text:
        anchor = "static const NpcModelMapping* npc_model_lookup(uint16_t npc_id) {"
        if anchor not in text:
            raise SystemExit("export_colosseum_npcs: npc_model_lookup anchor not found")
        block = (
            "/* ================================================================ */\n"
            "/* fortis colosseum NPC model/animation mappings (generated) */\n"
            f"{include_line}\n\n"
        )
        text = text.replace(anchor, block + anchor, 1)

    arm = "NPC_MODEL_MAP_COLOSSEUM_GEN"
    if arm not in text.split("npc_model_lookup", 1)[1]:
        inferno_arm = (
            "    for (int i = 0; i < (int)(sizeof(NPC_MODEL_MAP_INFERNO_GEN) / "
            "sizeof(NPC_MODEL_MAP_INFERNO_GEN[0])); i++) {\n"
            "        if (NPC_MODEL_MAP_INFERNO_GEN[i].npc_id == npc_id) return "
            "&NPC_MODEL_MAP_INFERNO_GEN[i];\n"
            "    }\n"
        )
        if inferno_arm not in text:
            raise SystemExit("export_colosseum_npcs: inferno lookup arm not found for splice")
        colosseum_arm = (
            "    for (int i = 0; i < (int)(sizeof(NPC_MODEL_MAP_COLOSSEUM_GEN) / "
            "sizeof(NPC_MODEL_MAP_COLOSSEUM_GEN[0])); i++) {\n"
            "        if (NPC_MODEL_MAP_COLOSSEUM_GEN[i].npc_id == npc_id) return "
            "&NPC_MODEL_MAP_COLOSSEUM_GEN[i];\n"
            "    }\n"
        )
        text = text.replace(inferno_arm, inferno_arm + colosseum_arm, 1)

    npc_models_path.write_text(text, encoding="utf-8")


def main() -> None:
    """Export Colosseum NPC models, animations, and the model-map header."""
    parser = argparse.ArgumentParser(
        description="export Fortis Colosseum NPC models + animations from the modern cache"
    )
    parser.add_argument("--modern-cache", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, default=Path("ocean/osrs/data"))
    args = parser.parse_args()

    reader = ModernCacheReader(args.modern_cache)
    args.output_dir.mkdir(parents=True, exist_ok=True)

    npc_files = reader.read_group(2, MODERN_NPC_CONFIG_GROUP)
    print(f"read {len(npc_files)} NPC defs; building {len(COLOSSEUM_NPC_IDS)} Colosseum NPCs")

    models, mapping, sequence_models = build_npc_models(reader, npc_files)
    models_path = args.output_dir / "colosseum_npcs.models"
    # MDL4 (textured) output: passing an atlas makes write_models_binary emit the
    # per-face alpha block (face_alphas + face_alpha_labels) the MDL2 path drops.
    # Colosseum NPC component models carry real animation-alpha labels (e.g. the
    # Shockwave Colossus body), so this is what lets their type-5 alpha shimmer
    # round-trip into colosseum_npcs.models for the composite render path.
    store = RcCacheStore(args.modern_cache)
    tex_colors = load_texture_average_colors(store)
    atlas = build_atlas(load_texture_sprites(store))
    write_models_binary(models_path, models, tex_colors=tex_colors, atlas=atlas)
    print(f"wrote {len(models)} models ({models_path.stat().st_size:,} bytes) to {models_path}")

    anim_ids = collect_anim_ids(mapping)
    anims_path = args.output_dir / "colosseum_npcs.anims"
    export_animations(reader, anims_path, anim_ids, sequence_models, mapping)
    print(f"wrote {len(anim_ids)} sequences ({anims_path.stat().st_size:,} bytes) to {anims_path}")

    header_path = args.output_dir / "npc_models_colosseum.h"
    write_colosseum_header(header_path, mapping)
    print(f"wrote model-map header to {header_path}")

    patch_npc_models_header(args.output_dir / "npc_models.h")
    print(f"patched {args.output_dir / 'npc_models.h'} with colosseum lookup arm")


if __name__ == "__main__":
    main()
