"""Export inferno NPC models, animations, and spotanim GFX from modern OSRS cache.

Reads NPC definitions for all inferno monsters (nibblers through Zuk), extracts
their model IDs and animation sequence IDs, exports 3D meshes to .models binary,
exports animations to .anims binary, and updates npc_models.h with mappings.

Also reads SpotAnim (GFX) configs for inferno projectiles.

The cache pipeline library (modern_cache_reader, rc_cache, export_models,
export_animations, export_textures) is vendored at ocean/osrs/tools/cache_pipeline,
located the same way export_colosseum_npcs.py does.

Usage:
    uv run python ocean/osrs/tools/export_inferno_npcs.py \
        --modern-cache .refs/osrs-cache-modern \
        --output-dir ocean/osrs/data
"""

import argparse
import os
import sys
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
    raise SystemExit(f"export_inferno_npcs: cache pipeline not found, searched:\n  {searched}")


CACHE_PIPELINE = _find_cache_pipeline()
sys.path.insert(0, str(CACHE_PIPELINE))

from modern_cache_reader import ModernCacheReader
from rc_cache import (
    RcCacheStore,
    load_texture_average_colors,
    load_texture_sprites,
)
from rc_cache.definitions import (
    NpcDef,
    SpotanimDef,
    decode_npc_definition,
    decode_spotanim_definition,
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
    SequenceDef,
    _parse_normal_frame,
    load_modern_framebases,
    write_animations_binary,
)
from modern_cache_reader import parse_sequence as parse_modern_sequence

# modern cache layout
MODERN_NPC_CONFIG_GROUP = 9      # config index 2, group 9 = NPC definitions
MODERN_SPOTANIM_CONFIG_GROUP = 13  # config index 2, group 13 = SpotAnim/GFX
MODERN_SEQ_CONFIG_GROUP = 12     # config index 2, group 12 = sequences
MODERN_FRAME_INDEX = 0           # frame archives
MODERN_FRAMEBASE_INDEX = 1       # frame bases

# inferno NPC IDs from the OSRS wiki
INFERNO_NPC_IDS = {
    7691: "Jal-Nib (nibbler)",
    7692: "Jal-MejRah (bat)",
    7693: "Jal-Ak (blob)",
    7694: "Jal-Ak-Rek-Ket (blob melee split)",
    7695: "Jal-Ak-Rek-Xil (blob range split)",
    7696: "Jal-Ak-Rek-Mej (blob mage split)",
    7697: "Jal-ImKot (meleer)",
    7698: "Jal-Xil (ranger)",
    7699: "Jal-Zek (mager)",
    7700: "JalTok-Jad",
    7701: "Yt-HurKot (jad healer)",
    7706: "TzKal-Zuk",
    7707: "Zuk shield",
    7708: "Jal-MejJak (zuk healer)",
}

# inferno projectile/effect GFX ids (wiki + runelite inferno plugin)
INFERNO_SPOTANIM_IDS = {
    # jad attacks
    447: "Jad ranged projectile (fireball)",
    448: "Jad magic projectile",
    451: "Jad ranged hit",
    157: "Jad magic hit",
    # mager
    1379: "Mager magic projectile",
    1380: "Mager magic hit",
    # ranger
    1377: "Ranger ranged projectile",
    1378: "Ranger ranged hit",
    # zuk
    1375: "Zuk magic projectile",
    1376: "Zuk ranged projectile",
    1381: "Zuk typeless hit (falling rocks?)",
    # bat
    1374: "Bat ranged projectile",
    # blob
    1382: "Blob melee",
    1383: "Blob ranged",
    1384: "Blob magic",
    # healer
    1385: "Healer magic attack",
    # player projectiles (needed for tbow in inferno)
    942: "Dragon arrow projectile (twisted bow)",
}


def apply_recolors(md: ModelData, src: list[int], dst: list[int]) -> None:
    """Apply recolor pairs to model face colors in-place."""
    for i, color in enumerate(md.face_colors):
        for s, d in zip(src, dst):
            if color == s:
                md.face_colors[i] = d
                break


def apply_scale(md: ModelData, width_scale: int, height_scale: int) -> None:
    """Apply NPC width/height scale to vertex positions in-place."""
    if width_scale == 128 and height_scale == 128:
        return
    ws = width_scale / 128.0
    hs = height_scale / 128.0
    for i in range(md.vertex_count):
        md.vertices_x[i] = int(md.vertices_x[i] * ws)
        md.vertices_y[i] = int(md.vertices_y[i] * hs)
        md.vertices_z[i] = int(md.vertices_z[i] * ws)


def main() -> None:
    """Export inferno NPC data from modern OSRS cache."""
    parser = argparse.ArgumentParser(description="export inferno NPC models + animations from modern cache")
    parser.add_argument(
        "--modern-cache", type=Path, required=True,
        help="path to modern OpenRS2 flat-file cache",
    )
    parser.add_argument(
        "--output-dir", type=Path, default=Path("data"),
        help="output directory for generated files",
    )
    args = parser.parse_args()

    reader = ModernCacheReader(args.modern_cache)
    output_dir = args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    print("reading NPC definitions from modern cache (index 2, group 9)...")
    npc_files = reader.read_group(2, MODERN_NPC_CONFIG_GROUP)
    print(f"  {len(npc_files)} total NPC entries in group 9")

    npc_defs: dict[int, NpcDef] = {}
    all_model_ids: set[int] = set()
    all_anim_ids: set[int] = set()

    for npc_id, label in sorted(INFERNO_NPC_IDS.items()):
        if npc_id not in npc_files:
            print(f"  NPC {npc_id} ({label}): NOT FOUND in cache")
            continue

        npc = decode_npc_definition(npc_id, npc_files[npc_id])
        if not npc.complete:
            raise SystemExit(
                f"export_inferno_npcs: npc {npc_id} ({label}) hit unknown opcode "
                f"{npc.unknown_opcode}"
            )
        if not npc.models:
            raise SystemExit(f"export_inferno_npcs: npc {npc_id} ({label}) has no models")
        npc_defs[npc_id] = npc

        print(f"\n  NPC {npc_id} ({label}):")
        print(f"    name: {npc.name}")
        print(f"    models: {npc.models}")
        print(f"    size: {npc.size}")
        print(f"    idle_anim: {npc.stand_anim}")
        print(f"    walk_anim: {npc.walk_anim}")
        print(f"    scale: {npc.width_scale}x{npc.height_scale}")
        if npc.recolor_from:
            print(f"    recolors: {list(zip(npc.recolor_from, npc.recolor_to))}")
        if npc.retexture_from:
            print(f"    retextures: {list(zip(npc.retexture_from, npc.retexture_to))}")

        all_model_ids.update(npc.models)
        for anim_id in [
            npc.stand_anim, npc.walk_anim, npc.rotate_180_anim,
            npc.idle_rotate_left_anim, npc.idle_rotate_right_anim,
        ]:
            if anim_id >= 0:
                all_anim_ids.add(anim_id)

    print("\n\nreading SpotAnim/GFX definitions (index 2, group 13)...")
    spotanim_files = reader.read_group(2, MODERN_SPOTANIM_CONFIG_GROUP)
    print(f"  {len(spotanim_files)} total spotanim entries")

    spotanim_defs: dict[int, SpotanimDef] = {}
    for gfx_id, label in sorted(INFERNO_SPOTANIM_IDS.items()):
        if gfx_id not in spotanim_files:
            print(f"  GFX {gfx_id} ({label}): NOT FOUND in cache")
            continue

        sa = decode_spotanim_definition(gfx_id, spotanim_files[gfx_id])
        if not sa.complete:
            raise SystemExit(
                f"export_inferno_npcs: gfx {gfx_id} ({label}) hit unknown opcode "
                f"{sa.unknown_opcode}"
            )
        spotanim_defs[gfx_id] = sa

        print(f"  GFX {gfx_id} ({label}): model={sa.model_id}, seq={sa.animation_id}, "
              f"scale={sa.resize_xy}x{sa.resize_z}")

        if sa.model_id >= 0:
            all_model_ids.add(sa.model_id)
        if sa.animation_id >= 0:
            all_anim_ids.add(sa.animation_id)

    print(f"\ntotal unique model IDs to export: {len(all_model_ids)}")
    print(f"  {sorted(all_model_ids)}")
    print(f"total unique animation IDs to export: {len(all_anim_ids)}")
    print(f"  {sorted(all_anim_ids)}")

    print("\n\nexporting NPC + GFX models...")
    all_models: list[ModelData] = []

    for npc_id, npc in sorted(npc_defs.items()):
        sub_models: list[ModelData] = []
        for mid in npc.models:
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

        if len(sub_models) == 1:
            merged = sub_models[0]
        else:
            merged = _merge_models(sub_models)

        if npc.recolor_from:
            apply_recolors(merged, npc.recolor_from, npc.recolor_to)

        apply_scale(merged, npc.width_scale, npc.height_scale)

        # use NPC ID as model ID for lookup (synthetic: 0xC0000 + npc_id)
        merged.model_id = 0xC0000 + npc_id
        all_models.append(merged)
        print(f"  NPC {npc_id} ({npc.name}): {merged.vertex_count} verts, {merged.face_count} faces")

    # also export raw model IDs for GFX projectiles (no recolor for now, keep raw)
    gfx_model_ids = set()
    for gfx_id, sa in spotanim_defs.items():
        if sa.model_id >= 0:
            gfx_model_ids.add(sa.model_id)

    for mid in sorted(gfx_model_ids):
        raw = load_model_modern(reader, mid)
        if raw is None:
            print(f"  warning: GFX model {mid} not found")
            continue
        md = decode_model(mid, raw)
        if md is None:
            print(f"  warning: failed to decode GFX model {mid}")
            continue
        all_models.append(md)
        print(f"  GFX model {mid}: {md.vertex_count} verts, {md.face_count} faces")

    # MDL4 (textured) output: passing an atlas makes write_models_binary emit the
    # per-face alpha block (face_alphas + face_alpha_labels) the MDL2 path drops,
    # and enables atlas-sampled textures on faces that were vertex-color-only.
    models_path = output_dir / "inferno_npcs.models"
    store = RcCacheStore(args.modern_cache)
    tex_colors = load_texture_average_colors(store)
    atlas = build_atlas(load_texture_sprites(store))
    write_models_binary(models_path, all_models, tex_colors=tex_colors, atlas=atlas)
    file_size = models_path.stat().st_size
    print(f"\nwrote {len(all_models)} models ({file_size:,} bytes) to {models_path}")

    print("\n\nexporting animations...")
    seq_files = reader.read_group(2, MODERN_SEQ_CONFIG_GROUP)

    sequences: dict[int, SequenceDef] = {}
    for seq_id in sorted(all_anim_ids):
        if seq_id not in seq_files:
            print(f"  warning: sequence {seq_id} not found in cache")
            continue
        modern_seq = parse_modern_sequence(seq_id, seq_files[seq_id])
        seq = SequenceDef(
            seq_id=modern_seq.seq_id,
            frame_count=modern_seq.frame_count,
            frame_delays=modern_seq.frame_delays,
            primary_frame_ids=modern_seq.primary_frame_ids,
            frame_step=modern_seq.frame_step,
            interleave_order=modern_seq.interleave_order,
            priority=modern_seq.forced_priority,
            loop_count=modern_seq.max_loops,
            walk_flag=modern_seq.priority,
            run_flag=modern_seq.precedence_animating,
        )
        sequences[seq_id] = seq
        print(f"  seq {seq_id}: {seq.frame_count} frames, delays={seq.frame_delays[:5]}{'...' if len(seq.frame_delays) > 5 else ''}")

    needed_groups: set[int] = set()
    for seq_id in all_anim_ids & set(sequences.keys()):
        seq = sequences[seq_id]
        for fid in seq.primary_frame_ids:
            if fid != -1:
                needed_groups.add(fid >> 16)

    print(f"  loading {len(needed_groups)} frame archives...")

    # first pass: discover framebase IDs from frame data headers
    needed_base_ids: set[int] = set()
    raw_frame_data: dict[int, dict[int, bytes]] = {}
    for group_id in sorted(needed_groups):
        try:
            files = reader.read_group(MODERN_FRAME_INDEX, group_id)
        except (KeyError, FileNotFoundError):
            print(f"  warning: frame archive {group_id} not found")
            continue
        raw_frame_data[group_id] = files
        for file_data in files.values():
            if len(file_data) >= 2:
                fb_id = (file_data[0] << 8) | file_data[1]
                needed_base_ids.add(fb_id)

    print(f"  loading {len(needed_base_ids)} framebases...")
    framebases = load_modern_framebases(reader, needed_base_ids)
    print(f"  loaded {len(framebases)} framebases")

    # second pass: parse frames
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

    total_frames = sum(len(v) for v in all_frames.values())
    print(f"  {len(all_frames)} frame archives, {total_frames} total frames")

    anims_path = output_dir / "inferno_npcs.anims"
    available_seqs = all_anim_ids & set(sequences.keys())
    write_animations_binary(anims_path, framebases, all_frames, sequences, available_seqs)

    print("\n\nupdating npc_models.h...")
    header_path = output_dir / "npc_models.h"

    npc_entries = []
    for npc_id, npc in sorted(npc_defs.items()):
        # use the synthetic model ID we assigned (0xC0000 + npc_id)
        synth_model = 0xC0000 + npc_id
        idle = npc.stand_anim if npc.stand_anim >= 0 else 0xFFFF
        # we don't have attack anims from the def directly; set to 0xFFFF (unknown)
        attack = 0xFFFF
        label = INFERNO_NPC_IDS.get(npc_id, npc.name)
        npc_entries.append((npc_id, synth_model, idle, attack, label))

    spotanim_entries = []
    for gfx_id, sa in sorted(spotanim_defs.items()):
        if sa.model_id >= 0:
            label = INFERNO_SPOTANIM_IDS.get(gfx_id, "unknown")
            spotanim_entries.append((gfx_id, sa.model_id, sa.animation_id, label))

    with open(header_path, "w") as f:
        f.write("/**\n")
        f.write(" * @fileoverview NPC model/animation mappings for encounter rendering.\n")
        f.write(" *\n")
        f.write(" * Maps NPC definition IDs to cache model IDs and animation sequence IDs.\n")
        f.write(" * Generated by ocean/osrs/tools/export_inferno_npcs.py -- do not edit.\n")
        f.write(" */\n\n")
        f.write("#ifndef NPC_MODELS_H\n")
        f.write("#define NPC_MODELS_H\n\n")
        f.write("#include <stdint.h>\n\n")

        f.write("typedef struct {\n")
        f.write("    uint16_t npc_id;\n")
        f.write("    uint32_t model_id;\n")
        f.write("    uint32_t idle_anim;\n")
        f.write("    uint32_t attack_anim;\n")
        f.write("    uint32_t walk_anim;\n")
        f.write("    uint32_t run_anim;\n")
        f.write("} NpcModelMapping;\n\n")

        # zulrah entries (keep existing)
        f.write("/* zulrah forms + snakeling */\n")
        f.write("static const NpcModelMapping NPC_MODEL_MAP_ZULRAH[] = {\n")
        f.write("    {2042, 14408, 5069, 5068},  /* green zulrah (ranged) */\n")
        f.write("    {2043, 14409, 5069, 5068},  /* red zulrah (melee) */\n")
        f.write("    {2044, 14407, 5069, 5068},  /* blue zulrah (magic) */\n")
        f.write("};\n\n")

        # snakeling defines (keep existing)
        f.write("/* snakeling model + animations (NPC 2045 melee, 2046 magic — same model) */\n")
        f.write("#define SNAKELING_MODEL_ID 10415\n")
        f.write("#define SNAKELING_ANIM_IDLE    1721\n")
        f.write("#define SNAKELING_ANIM_MELEE   140   /* NPC 2045 melee attack */\n")
        f.write("#define SNAKELING_ANIM_MAGIC   185   /* NPC 2046 magic attack */\n")
        f.write("#define SNAKELING_ANIM_DEATH   138   /* NPC 2045 death */\n")
        f.write("#define SNAKELING_ANIM_WALK    2405  /* walk cycle */\n\n")

        # zulrah spotanim defines (keep existing)
        f.write("/* zulrah spotanim (projectile/cloud) model IDs */\n")
        f.write("#define GFX_RANGED_PROJ_MODEL  20390  /* GFX 1044 ranged projectile */\n")
        f.write("#define GFX_CLOUD_PROJ_MODEL   11221  /* GFX 1045 cloud projectile */\n")
        f.write("#define GFX_MAGIC_PROJ_MODEL   26593  /* GFX 1046 magic projectile */\n")
        f.write("#define GFX_TOXIC_CLOUD_MODEL   4086  /* object 11700 */\n")
        f.write("#define GFX_SNAKELING_SPAWN_MODEL 20390  /* GFX 1047 spawn orb */\n\n")

        # zulrah animation defines (keep existing)
        f.write("/* zulrah animation sequence IDs */\n")
        f.write("#define ZULRAH_ANIM_ATTACK   5068\n")
        f.write("#define ZULRAH_ANIM_IDLE     5069\n")
        f.write("#define ZULRAH_ANIM_DIVE     5072\n")
        f.write("#define ZULRAH_ANIM_SURFACE  5071\n")
        f.write("#define ZULRAH_ANIM_RISE     5073\n")
        f.write("#define ZULRAH_ANIM_5070     5070\n")
        f.write("#define ZULRAH_ANIM_5806     5806\n")
        f.write("#define ZULRAH_ANIM_5807     5807\n")
        f.write("#define GFX_SNAKELING_SPAWN_ANIM 5358\n\n")

        # inferno NPC model mappings
        f.write("/* ================================================================ */\n")
        f.write("/* inferno NPC model/animation mappings                              */\n")
        f.write("/* ================================================================ */\n\n")

        f.write("static const NpcModelMapping NPC_MODEL_MAP_INFERNO[] = {\n")
        for npc_id, synth_model, idle, attack, label in npc_entries:
            f.write(f"    {{{npc_id}, 0x{synth_model:X}, {idle}, {attack}}},  /* {label} */\n")
        f.write("};\n\n")

        # inferno NPC defines for walk anims and other useful data
        f.write("/* inferno NPC walk animation IDs */\n")
        for npc_id, npc in sorted(npc_defs.items()):
            safe_name = INFERNO_NPC_IDS[npc_id].split("(")[1].rstrip(")") if "(" in INFERNO_NPC_IDS[npc_id] else INFERNO_NPC_IDS[npc_id]
            safe_name = safe_name.replace(" ", "_").replace("-", "_").upper()
            if npc.walk_anim >= 0:
                f.write(f"#define INF_WALK_ANIM_{safe_name}  {npc.walk_anim}\n")
        f.write("\n")

        # inferno spotanim/GFX defines
        f.write("/* inferno spotanim (projectile/effect) model + animation IDs */\n")
        for gfx_id, model_id, seq_id, label in spotanim_entries:
            f.write(f"#define INF_GFX_{gfx_id}_MODEL  {model_id}  /* {label} */\n")
            if seq_id >= 0:
                f.write(f"#define INF_GFX_{gfx_id}_ANIM   {seq_id}\n")
        f.write("\n")

        # combined lookup function that searches both zulrah and inferno tables
        f.write("static const NpcModelMapping* npc_model_lookup(uint16_t npc_id) {\n")
        f.write("    for (int i = 0; i < (int)(sizeof(NPC_MODEL_MAP_ZULRAH) / sizeof(NPC_MODEL_MAP_ZULRAH[0])); i++) {\n")
        f.write("        if (NPC_MODEL_MAP_ZULRAH[i].npc_id == npc_id) return &NPC_MODEL_MAP_ZULRAH[i];\n")
        f.write("    }\n")
        f.write("    for (int i = 0; i < (int)(sizeof(NPC_MODEL_MAP_INFERNO) / sizeof(NPC_MODEL_MAP_INFERNO[0])); i++) {\n")
        f.write("        if (NPC_MODEL_MAP_INFERNO[i].npc_id == npc_id) return &NPC_MODEL_MAP_INFERNO[i];\n")
        f.write("    }\n")
        f.write("    return NULL;\n")
        f.write("}\n\n")

        f.write("#endif /* NPC_MODELS_H */\n")

    print(f"wrote {header_path}")

    print("\n\n========================================")
    print("INF_NPC_DEF_IDS mapping table for encounter_inferno.h:")
    print("========================================")
    print("static const int INF_NPC_DEF_IDS[INF_NUM_NPC_TYPES] = {")

    inf_type_to_npc = {
        "INF_NPC_NIBBLER": 7691,
        "INF_NPC_BAT": 7692,
        "INF_NPC_BLOB": 7693,
        "INF_NPC_BLOB_MELEE": 7694,
        "INF_NPC_BLOB_RANGE": 7695,
        "INF_NPC_BLOB_MAGE": 7696,
        "INF_NPC_MELEER": 7697,
        "INF_NPC_RANGER": 7698,
        "INF_NPC_MAGER": 7699,
        "INF_NPC_JAD": 7700,
        "INF_NPC_ZUK": 7706,
        "INF_NPC_HEALER_JAD": 7701,
        "INF_NPC_HEALER_ZUK": 7708,
        "INF_NPC_ZUK_SHIELD": 7707,
    }
    for enum_name, npc_id in inf_type_to_npc.items():
        npc = npc_defs.get(npc_id)
        name = npc.name if npc else "UNKNOWN"
        print(f"    [{enum_name}] = {npc_id},  /* {name} */")
    print("};")

    print("\ndone.")


if __name__ == "__main__":
    main()
