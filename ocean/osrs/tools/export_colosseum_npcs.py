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
import os
import sys
from pathlib import Path


def _find_cache_pipeline() -> Path:
    """Locate the RuneC cache pipeline, which is gitignored and may live only in
    the primary checkout rather than the current worktree."""
    env_override = os.environ.get("OSRS_CACHE_PIPELINE")
    candidates = []
    if env_override:
        candidates.append(Path(env_override))
    repo_root = Path(__file__).resolve().parents[3]
    candidates.append(repo_root / "refs" / "RuneC" / "tools" / "cache_pipeline")
    common = Path.home() / "Projects" / "pufferlib-metal"
    candidates.append(common / "refs" / "RuneC" / "tools" / "cache_pipeline")
    for candidate in candidates:
        if candidate.is_dir():
            return candidate
    searched = "\n  ".join(str(c) for c in candidates)
    raise SystemExit(f"export_colosseum_npcs: cache pipeline not found, searched:\n  {searched}")


CACHE_PIPELINE = _find_cache_pipeline()
sys.path.insert(0, str(CACHE_PIPELINE))

from modern_cache_reader import ModernCacheReader
from modern_cache_reader import parse_sequence as parse_modern_sequence
from rc_cache.definitions import decode_npc_definition
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
from export_inferno_npcs import apply_recolors, apply_scale

MODERN_NPC_CONFIG_GROUP = 9
MODERN_SEQ_CONFIG_GROUP = 12
MODERN_FRAME_INDEX = 0

SYNTHETIC_MODEL_BASE = 0xC0000

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
}


def build_npc_models(
    reader: ModernCacheReader,
    npc_files: dict[int, bytes],
) -> tuple[list[ModelData], dict[int, dict[str, int]]]:
    """Decode, merge, recolor, and scale each Colosseum NPC mesh.

    Returns the merged models keyed by synthetic id plus a mapping from npc id
    to {synthetic_model_id, idle_anim, walk_anim} for the C header.
    """
    models: list[ModelData] = []
    mapping: dict[int, dict[str, int]] = {}

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
            parts.append(decoded)

        merged = parts[0] if len(parts) == 1 else _merge_models(parts)
        if npc.recolor_from:
            apply_recolors(merged, npc.recolor_from, npc.recolor_to)
        apply_scale(merged, npc.width_scale, npc.height_scale)
        merged.model_id = SYNTHETIC_MODEL_BASE + npc_id
        models.append(merged)

        idle_anim = npc.stand_anim if npc.stand_anim >= 0 else 0xFFFF
        walk_anim = npc.walk_anim if npc.walk_anim >= 0 else 0xFFFF
        mapping[npc_id] = {
            "synthetic_model_id": merged.model_id,
            "idle_anim": idle_anim,
            "walk_anim": walk_anim,
        }
        print(
            f"  npc {npc_id} ({npc.name}): {merged.vertex_count}v {merged.face_count}f "
            f"idle={idle_anim} walk={walk_anim}"
        )

    return models, mapping


def collect_anim_ids(mapping: dict[int, dict[str, int]]) -> set[int]:
    """Gather every non-sentinel idle and walk sequence id."""
    anim_ids: set[int] = set()
    for entry in mapping.values():
        for key in ("idle_anim", "walk_anim"):
            value = entry[key]
            if value != 0xFFFF:
                anim_ids.add(value)
    return anim_ids


def export_animations(
    reader: ModernCacheReader,
    output_path: Path,
    anim_ids: set[int],
) -> None:
    """Resolve sequences and frames for the requested ids and write the binary."""
    seq_files = reader.read_group(2, MODERN_SEQ_CONFIG_GROUP)

    sequences: dict[int, SequenceDef] = {}
    for seq_id in sorted(anim_ids):
        if seq_id not in seq_files:
            raise SystemExit(f"export_colosseum_npcs: sequence {seq_id} missing from cache")
        modern_seq = parse_modern_sequence(seq_id, seq_files[seq_id])
        sequences[seq_id] = SequenceDef(
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

    needed_groups: set[int] = set()
    for seq in sequences.values():
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

    write_animations_binary(
        output_path, framebases, all_frames, sequences, set(sequences.keys())
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
            f"{entry['idle_anim']}, 0xFFFF, {entry['walk_anim']}}},  /* {label} */"
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

    models, mapping = build_npc_models(reader, npc_files)
    models_path = args.output_dir / "colosseum_npcs.models"
    write_models_binary(models_path, models)
    print(f"wrote {len(models)} models ({models_path.stat().st_size:,} bytes) to {models_path}")

    anim_ids = collect_anim_ids(mapping)
    anims_path = args.output_dir / "colosseum_npcs.anims"
    export_animations(reader, anims_path, anim_ids)
    print(f"wrote {len(anim_ids)} sequences ({anims_path.stat().st_size:,} bytes) to {anims_path}")

    header_path = args.output_dir / "npc_models_colosseum.h"
    write_colosseum_header(header_path, mapping)
    print(f"wrote model-map header to {header_path}")

    patch_npc_models_header(args.output_dir / "npc_models.h")
    print(f"patched {args.output_dir / 'npc_models.h'} with colosseum lookup arm")


if __name__ == "__main__":
    main()
