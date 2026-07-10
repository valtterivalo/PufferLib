"""Append Sol Heredit's AoE stab-dust spotanim models to projectiles.models.

Sol's spear/shield AoE kicks up dust on every struck tile (VFX_COLOSSI_STAB_DUST,
gfx 2699-2706: eight self-animating grow-then-fade puffs sharing cache model
52521). The viewer spawns them via the spotanim effect system, which resolves the
model under the synthetic id 0xA2000000 + gfx, so the models must live in
projectiles.models.

This APPENDS the eight dust entries to an existing projectiles.models rather than
re-baking it: the canonical projectile build's exact --models / --spotanim-ids /
--raw-recolor-spotanim-ids list is not reconstructable from the .models file (a
raw model shared by many gfx hides which one was recolored, e.g. the venator
bolt), so a full re-export would risk silent recolor regressions. The dust model
is untextured (flat vertex-colored), so its records are atlas-independent: we
splice them in, grow the offset table, and leave projectiles.atlas untouched.

This is a stopgap. The clean fix is to add gfx 2699-2706 to the canonical
projectiles.models build so it bakes from scratch.

Usage:
    python3 ocean/osrs/tools/append_colosseum_sol_dust.py \
        --modern-cache .refs/osrs-cache-modern \
        --projectiles ocean/osrs/data/projectiles.models \
        --scratch /tmp
"""

import argparse
import copy
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "cache_pipeline"))
from export_textures import build_atlas
from rc_cache import (
    CONFIG_SPOTANIM,
    INDEX_CONFIGS,
    RcCacheStore,
    decode_spotanim_definition,
    load_model,
    load_texture_average_colors,
    load_texture_sprites,
    write_models_binary,
)
from export_projectile_models import apply_spotanim_recolors, SPOTANIM_MODEL_BASE

MDL4_MAGIC = 0x4D444C34
DUST_GFX = list(range(2699, 2707))  # VFX_COLOSSI_STAB_DUST_01..08


def parse_mdl4(data: bytes):
    magic, count = struct.unpack_from("<II", data, 0)
    if magic != MDL4_MAGIC:
        raise SystemExit(f"bad MDL4 magic {magic:#x}")
    return count, list(struct.unpack_from("<%dI" % count, data, 8))


def record_blobs(data: bytes):
    count, offsets = parse_mdl4(data)
    ends = offsets[1:] + [len(data)]
    return [data[offsets[i]:ends[i]] for i in range(count)]


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--modern-cache", type=Path, required=True)
    ap.add_argument("--projectiles", type=Path,
                    default=Path("ocean/osrs/data/projectiles.models"))
    ap.add_argument("--scratch", type=Path, default=Path("/tmp"))
    args = ap.parse_args()

    store = RcCacheStore(args.modern_cache)
    tex_colors = load_texture_average_colors(store)
    atlas = build_atlas(load_texture_sprites(store))
    spotanim_files = store.read_group(INDEX_CONFIGS, CONFIG_SPOTANIM)

    dust_models = []
    for gfx in DUST_GFX:
        spot = decode_spotanim_definition(gfx, spotanim_files[gfx])
        m = load_model(store, spot.model_id)
        if m is None:
            raise SystemExit(f"dust gfx {gfx}: model {spot.model_id} failed to load")
        m = copy.deepcopy(m)
        m.model_id = SPOTANIM_MODEL_BASE + gfx
        apply_spotanim_recolors(m, spot)
        dust_models.append(m)

    tmp = args.scratch / "sol_dust_only.models"
    write_models_binary(tmp, dust_models, tex_colors=tex_colors, atlas=atlas,
                        atlas_path=args.scratch / "sol_dust_only.atlas")
    dust_blobs = record_blobs(tmp.read_bytes())

    existing = args.projectiles.read_bytes()
    old_count, old_offsets = parse_mdl4(existing)
    old_ids = [struct.unpack_from("<I", existing, o)[0] for o in old_offsets]
    dust_ids = [SPOTANIM_MODEL_BASE + g for g in DUST_GFX]
    if set(dust_ids) & set(old_ids):
        print("dust already present in projectiles.models; nothing to do")
        return

    old_records = existing[old_offsets[0]:]
    new_count = old_count + len(dust_blobs)
    header_size = 8 + 4 * new_count
    shift = header_size - old_offsets[0]
    new_offsets = [o + shift for o in old_offsets]
    cursor = header_size + len(old_records)
    dust_bytes = b""
    for blob in dust_blobs:
        new_offsets.append(cursor)
        dust_bytes += blob
        cursor += len(blob)

    out = bytearray()
    out += struct.pack("<I", MDL4_MAGIC)
    out += struct.pack("<I", new_count)
    out += struct.pack("<%dI" % new_count, *new_offsets)
    out += old_records
    out += dust_bytes
    args.projectiles.write_bytes(out)

    # validate: every old id preserved, dust ids present, records in-bounds
    vb = bytes(out)
    vc, voff = parse_mdl4(vb)
    got = [struct.unpack_from("<I", vb, o)[0] for o in voff]
    if set(old_ids) - set(got) or not set(dust_ids) <= set(got):
        raise SystemExit("validation failed: id set mismatch after append")
    print(f"appended {len(dust_blobs)} Sol dust models to {args.projectiles} "
          f"({old_count} -> {new_count})")


if __name__ == "__main__":
    main()
