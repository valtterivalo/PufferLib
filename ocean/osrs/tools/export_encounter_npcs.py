"""Regenerate inferno.models and zulrah.models as MDL4 (atlas-textured).

Reproduces the model-building logic of the original encounter NPC exporter
(last canonical revision at commit 1e0be358c) exactly, so the regenerated model
set is byte-for-byte the same membership, then writes MDL4 by passing tex_colors
+ atlas to write_models_binary (which also writes the sibling .atlas). The
.anims and npc_models_{group}.h outputs are intentionally NOT regenerated: they
already exist and stay valid because synthetic model IDs (0xC0000+npc_id,
0xD0000|gfx_id) and model membership are unchanged.

Imports the vendored cache pipeline at ocean/osrs/tools/cache_pipeline. The
gameval definitions and the modern cache are reference inputs that live under
refs/ or .refs/.

Usage:
    uv run python ocean/osrs/tools/export_encounter_npcs.py \
        --group inferno --modern-cache .refs/osrs-cache-modern \
        --manifest ocean/osrs/tools/monsters_manifest.json \
        --output-dir ocean/osrs/data [--verify-only]
"""

import argparse
import io
import json
import os
import struct
import sys
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
    searched = "\n  ".join(str(c) for c in candidates)
    raise SystemExit(f"export_encounter_npcs: cache pipeline not found, searched:\n  {searched}")


def _find_gameval_dir() -> Path:
    """Locate the RuneLite gameval definitions reference input under refs/.refs."""
    repo_root = Path(__file__).resolve().parents[3]
    suffix = Path(
        "osrs-client-deob/runelite-api/src/main/java/net/runelite/api/gameval",
    )
    for base in ("refs", ".refs"):
        d = repo_root / base / suffix
        if d.is_dir():
            return d
    raise SystemExit("export_encounter_npcs: gameval dir not found under refs/.refs")


CACHE_PIPELINE = _find_cache_pipeline()
sys.path.insert(0, str(CACHE_PIPELINE))

from modern_cache_reader import (
    ModernCacheReader,
    read_i32,
    read_string,
    read_u8,
    read_u16,
    read_u24,
    read_u32,
)
from export_models import (
    MDL4_MAGIC,
    ModelData,
    _merge_models,
    decode_model,
    load_model_modern,
    write_models_binary,
)
from rc_cache import (
    RcCacheStore,
    load_texture_average_colors,
    load_texture_sprites,
)
from export_textures import build_atlas

import re

_CONST_PATTERN = re.compile(
    r"public\s+static\s+final\s+int\s+(\w+)\s*=\s*(\d+)\s*;"
)


def _parse_gameval_file(path: Path) -> dict[str, int]:
    text = Path(path).read_text()
    return {name: int(value) for name, value in _CONST_PATTERN.findall(text)}


def load_gameval(gameval_dir: Path):
    return _parse_gameval_file(gameval_dir / "SpotanimID.java")


def resolve_names(names, lookup, context=""):
    result = []
    for name in names:
        if name not in lookup:
            raise KeyError(f"[{context}] gameval constant not found: {name!r}")
        result.append(lookup[name])
    return result


MODERN_NPC_CONFIG_GROUP = 9
MODERN_SPOTANIM_CONFIG_GROUP = 13


@dataclass
class NpcDef:
    npc_id: int = 0
    name: str = ""
    model_ids: list = field(default_factory=list)
    chathead_model_ids: list = field(default_factory=list)
    size: int = 1
    idle_anim: int = -1
    walk_anim: int = -1
    run_anim: int = -1
    turn_180_anim: int = -1
    turn_cw_anim: int = -1
    turn_ccw_anim: int = -1
    attack_anim: int = -1
    death_anim: int = -1
    combat_level: int = 0
    width_scale: int = 128
    height_scale: int = 128
    recolor_src: list = field(default_factory=list)
    recolor_dst: list = field(default_factory=list)
    retexture_src: list = field(default_factory=list)
    retexture_dst: list = field(default_factory=list)


@dataclass
class SpotAnimDef:
    id: int = 0
    model_id: int = -1
    seq_id: int = -1
    recolor_src: list = field(default_factory=list)
    recolor_dst: list = field(default_factory=list)
    width_scale: int = 128
    height_scale: int = 128
    rotation: int = 0
    ambient: int = 0
    contrast: int = 0


def parse_modern_npc_def(npc_id: int, data: bytes) -> NpcDef:
    d = NpcDef(npc_id=npc_id)
    buf = io.BytesIO(data)
    while True:
        opcode_byte = buf.read(1)
        if not opcode_byte:
            break
        opcode = opcode_byte[0]
        if opcode == 0:
            break
        elif opcode == 1:
            count = read_u8(buf)
            d.model_ids = [read_u16(buf) for _ in range(count)]
        elif opcode == 2:
            d.name = read_string(buf)
        elif opcode == 3:
            read_string(buf)
        elif opcode == 5:
            count = read_u8(buf)
            for _ in range(count):
                read_u16(buf)
        elif opcode == 12:
            d.size = read_u8(buf)
        elif opcode == 13:
            d.idle_anim = read_u16(buf)
        elif opcode == 14:
            d.walk_anim = read_u16(buf)
        elif opcode == 15:
            d.turn_180_anim = read_u16(buf)
        elif opcode == 16:
            d.turn_cw_anim = read_u16(buf)
        elif opcode == 17:
            d.walk_anim = read_u16(buf)
            d.turn_180_anim = read_u16(buf)
            d.turn_cw_anim = read_u16(buf)
            d.turn_ccw_anim = read_u16(buf)
        elif opcode == 18:
            read_u16(buf)
        elif 30 <= opcode <= 34:
            read_string(buf)
        elif opcode == 40:
            count = read_u8(buf)
            for _ in range(count):
                d.recolor_src.append(read_u16(buf))
                d.recolor_dst.append(read_u16(buf))
        elif opcode == 41:
            count = read_u8(buf)
            for _ in range(count):
                d.retexture_src.append(read_u16(buf))
                d.retexture_dst.append(read_u16(buf))
        elif opcode == 60:
            count = read_u8(buf)
            d.chathead_model_ids = [read_u16(buf) for _ in range(count)]
        elif opcode == 61:
            count = read_u8(buf)
            d.model_ids = [read_u32(buf) for _ in range(count)]
        elif 74 <= opcode <= 79:
            read_u16(buf)
        elif opcode == 93:
            pass
        elif opcode == 95:
            d.combat_level = read_u16(buf)
        elif opcode == 97:
            d.width_scale = read_u16(buf)
        elif opcode == 98:
            d.height_scale = read_u16(buf)
        elif opcode == 99:
            pass
        elif opcode == 100:
            read_u8(buf)
        elif opcode == 101:
            read_u8(buf)
        elif opcode == 102:
            bitfield = read_u8(buf)
            bit_count = 0
            tmp = bitfield
            while tmp != 0:
                bit_count += 1
                tmp >>= 1
            for i in range(bit_count):
                if bitfield & (1 << i):
                    pos = buf.tell()
                    peek = buf.read(1)
                    if peek and peek[0] < 128:
                        buf.seek(pos)
                        read_u16(buf)
                    else:
                        buf.seek(pos)
                        read_i32(buf)
                    pos2 = buf.tell()
                    peek2 = buf.read(1)
                    if peek2 and peek2[0] < 128:
                        buf.seek(pos2)
                        read_u16(buf)
                    else:
                        buf.seek(pos2)
                        read_i32(buf)
        elif opcode == 103:
            read_u16(buf)
        elif opcode == 106:
            read_u16(buf)
            read_u16(buf)
            length = read_u8(buf)
            for _ in range(length + 1):
                read_u16(buf)
        elif opcode == 107:
            pass
        elif opcode == 108:
            pass
        elif opcode == 109:
            pass
        elif opcode == 111:
            pass
        elif opcode == 114:
            read_u16(buf)
        elif opcode == 115:
            read_u16(buf)
            read_u16(buf)
            read_u16(buf)
            read_u16(buf)
        elif opcode == 116:
            read_u16(buf)
        elif opcode == 117:
            read_u16(buf)
            read_u16(buf)
            read_u16(buf)
        elif opcode == 118:
            read_u16(buf)
            read_u16(buf)
            read_u16(buf)
            length = read_u8(buf)
            for _ in range(length + 1):
                read_u16(buf)
        elif opcode == 122:
            pass
        elif opcode == 123:
            pass
        elif opcode == 124:
            read_u16(buf)
        elif opcode == 125:
            read_u8(buf)
        elif opcode == 126:
            read_u16(buf)
        elif opcode == 128:
            read_u8(buf)
        elif opcode == 129:
            pass
        elif opcode == 130:
            pass
        elif opcode == 145:
            pass
        elif opcode == 146:
            read_u16(buf)
        elif opcode == 147:
            pass
        elif opcode == 249:
            count_val = read_u8(buf)
            for _ in range(count_val):
                is_string = read_u8(buf)
                read_u24(buf)
                if is_string:
                    read_string(buf)
                else:
                    read_u32(buf)
        else:
            print(f"  warning: unknown npc opcode {opcode} at npc {npc_id}, pos {buf.tell()}", file=sys.stderr)
            break
    return d


def parse_modern_spotanim(spotanim_id: int, data: bytes) -> SpotAnimDef:
    d = SpotAnimDef(id=spotanim_id)
    buf = io.BytesIO(data)
    while True:
        opcode_byte = buf.read(1)
        if not opcode_byte:
            break
        opcode = opcode_byte[0]
        if opcode == 0:
            break
        elif opcode == 1:
            d.model_id = read_u16(buf)
        elif opcode == 3:
            d.model_id = read_i32(buf)
        elif opcode == 2:
            d.seq_id = read_u16(buf)
        elif opcode == 4:
            d.width_scale = read_u16(buf)
        elif opcode == 5:
            d.height_scale = read_u16(buf)
        elif opcode == 6:
            d.rotation = read_u16(buf)
        elif opcode == 7:
            d.ambient = read_u8(buf)
        elif opcode == 8:
            d.contrast = read_u8(buf)
        elif opcode == 40:
            count = read_u8(buf)
            for _ in range(count):
                d.recolor_src.append(read_u16(buf))
                d.recolor_dst.append(read_u16(buf))
        elif opcode == 41:
            count = read_u8(buf)
            for _ in range(count):
                read_u16(buf)
                read_u16(buf)
        else:
            print(f"  warning: unknown spotanim opcode {opcode} at gfx {spotanim_id}", file=sys.stderr)
            break
    return d


def apply_recolors(md: ModelData, src, dst) -> None:
    for i, color in enumerate(md.face_colors):
        for s, dd in zip(src, dst):
            if color == s:
                md.face_colors[i] = dd
                break


def apply_scale(md: ModelData, width_scale: int, height_scale: int) -> None:
    if width_scale == 128 and height_scale == 128:
        return
    ws = width_scale / 128.0
    hs = height_scale / 128.0
    for i in range(md.vertex_count):
        md.vertices_x[i] = int(md.vertices_x[i] * ws)
        md.vertices_y[i] = int(md.vertices_y[i] * hs)
        md.vertices_z[i] = int(md.vertices_z[i] * ws)


def build_group_models(reader, group, manifest, spotanim_ids):
    """Replicate export_encounter_npcs.py model build for a group. Returns model list."""
    entries = [e for e in manifest if e.get("visual", {}).get("group") == group]
    if not entries:
        raise SystemExit(f"no manifest entries with visual.group={group!r}")

    npc_files = reader.read_group(2, MODERN_NPC_CONFIG_GROUP)
    npc_defs = {}
    all_spotanim_names = set()

    for entry in entries:
        npc_id = entry["npc_id"]
        vis = entry["visual"]
        if npc_id not in npc_files:
            raise SystemExit(f"NPC {npc_id} NOT FOUND in cache")
        npc = parse_modern_npc_def(npc_id, npc_files[npc_id])
        npc_defs[npc_id] = npc
        all_spotanim_names.update(vis.get("spotanims", []))

    all_models = []
    for npc_id, npc in sorted(npc_defs.items()):
        sub_models = []
        for mid in npc.model_ids:
            raw = load_model_modern(reader, mid)
            if raw is None:
                print(f"  warning: model {mid} not found for NPC {npc_id}")
                continue
            md = decode_model(mid, raw)
            if md is None:
                print(f"  warning: failed to decode model {mid} for NPC {npc_id}")
                continue
            sub_models.append(md)
        if not sub_models:
            print(f"  NPC {npc_id}: no models decoded")
            continue
        merged = sub_models[0] if len(sub_models) == 1 else _merge_models(sub_models)
        if npc.recolor_src:
            apply_recolors(merged, npc.recolor_src, npc.recolor_dst)
        apply_scale(merged, npc.width_scale, npc.height_scale)
        merged.model_id = 0xC0000 + npc_id
        all_models.append(merged)

    spotanim_defs = {}
    spotanim_name_for_id = {}
    if all_spotanim_names:
        spotanim_files = reader.read_group(2, MODERN_SPOTANIM_CONFIG_GROUP)
        for name in sorted(all_spotanim_names):
            gfx_id = resolve_names([name], spotanim_ids, context="spotanims")[0]
            spotanim_name_for_id[gfx_id] = name
            if gfx_id not in spotanim_files:
                raise SystemExit(f"GFX {gfx_id} ({name}) NOT FOUND in cache")
            sa = parse_modern_spotanim(gfx_id, spotanim_files[gfx_id])
            spotanim_defs[gfx_id] = sa

        exported_gfx_models = set()
        for gfx_id, sa in sorted(spotanim_defs.items()):
            if sa.model_id < 0:
                continue
            raw = load_model_modern(reader, sa.model_id)
            if raw is None:
                print(f"  warning: GFX {gfx_id} model {sa.model_id} not found")
                continue
            md = decode_model(sa.model_id, raw)
            if md is None:
                print(f"  warning: failed to decode GFX {gfx_id} model {sa.model_id}")
                continue
            if sa.recolor_src:
                apply_recolors(md, sa.recolor_src, sa.recolor_dst)
                md.model_id = 0xD0000 | gfx_id
            else:
                if sa.model_id in exported_gfx_models:
                    continue
            exported_gfx_models.add(md.model_id)
            all_models.append(md)

    return all_models


def existing_model_ids(path: Path):
    with open(path, "rb") as f:
        magic, count = struct.unpack("<II", f.read(8))
        offs = struct.unpack(f"<{count}I", f.read(4 * count))
        ids = []
        for o in offs:
            f.seek(o)
            (mid,) = struct.unpack("<I", f.read(4))
            ids.append(mid)
    return magic, sorted(ids)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--group", required=True)
    ap.add_argument("--modern-cache", type=Path, required=True)
    ap.add_argument("--manifest", type=Path, required=True)
    ap.add_argument("--output-dir", type=Path, required=True)
    ap.add_argument("--verify-only", action="store_true")
    ap.add_argument(
        "--drop", default="",
        help="comma-separated hex synthetic model IDs to drop from the manifest build "
             "(reconcile drift vs the baked file / runtime header)",
    )
    ap.add_argument(
        "--append-raw", default="",
        help="comma-separated raw cache model IDs (decimal) to decode and append, for "
             "GFX models the runtime header references but the manifest build omits",
    )
    args = ap.parse_args()

    gameval_dir = _find_gameval_dir()
    spotanim_ids = load_gameval(gameval_dir)
    manifest = json.load(open(args.manifest))

    reader = ModernCacheReader(args.modern_cache)
    models = build_group_models(reader, args.group, manifest, spotanim_ids)

    drop_ids = {int(x, 16) for x in args.drop.split(",") if x.strip()}
    if drop_ids:
        models = [m for m in models if m.model_id not in drop_ids]
        print(f"dropped {len(drop_ids)} models: {[hex(x) for x in sorted(drop_ids)]}")

    append_raw = [int(x) for x in args.append_raw.split(",") if x.strip()]
    for mid in append_raw:
        raw = load_model_modern(reader, mid)
        if raw is None:
            raise SystemExit(f"append-raw model {mid} not found in cache")
        md = decode_model(mid, raw)
        if md is None:
            raise SystemExit(f"append-raw model {mid} failed to decode")
        models.append(md)
    if append_raw:
        print(f"appended {len(append_raw)} raw GFX models: {[hex(x) for x in append_raw]}")

    fresh_ids = sorted(m.model_id for m in models)

    out_path = args.output_dir / f"{args.group}.models"
    old_magic, old_ids = existing_model_ids(out_path)
    print(f"existing {out_path.name}: magic={old_magic:08x} ids={len(old_ids)}")
    print(f"fresh build:           ids={len(fresh_ids)}")

    if fresh_ids != old_ids:
        only_old = set(old_ids) - set(fresh_ids)
        only_new = set(fresh_ids) - set(old_ids)
        print(f"  MODEL-ID MISMATCH. only_in_existing={[hex(x) for x in sorted(only_old)]}")
        print(f"                     only_in_fresh={[hex(x) for x in sorted(only_new)]}")
        raise SystemExit("model membership differs from existing file; aborting to avoid render drift")
    print("  model membership matches existing file exactly")

    if args.verify_only:
        print("verify-only: not writing")
        return

    store = RcCacheStore(args.modern_cache)
    tex_colors = load_texture_average_colors(store)
    atlas = build_atlas(load_texture_sprites(store))
    write_models_binary(out_path, models, tex_colors=tex_colors, atlas=atlas)
    new_magic, _ = existing_model_ids(out_path)
    print(f"wrote MDL4 {out_path} ({out_path.stat().st_size:,} bytes) magic={new_magic:08x}")
    assert new_magic == MDL4_MAGIC, "post-write magic is not MDL4"


if __name__ == "__main__":
    main()
