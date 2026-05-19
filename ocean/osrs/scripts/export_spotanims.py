"""Parse spotanim data from modern OpenRS2 flat file cache and export spotanim metadata.

SpotAnimations (GFX effects) are visual effects like spell impacts, projectiles,
and special attack graphics. Each has a model ID, animation sequence ID, scale,
and optional recolors.

Usage:
    uv run python scripts/export_spotanims.py \
        --modern-cache ../reference/osrs-cache-modern

    # export specific GFX IDs only
    uv run python scripts/export_spotanims.py \
        --modern-cache ../reference/osrs-cache-modern \
        --ids 27,368,369,377
"""

import argparse
import io
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from modern_cache_reader import ModernCacheReader

MODERN_SPOTANIM_CONFIG_GROUP = 13
SPOTANIM_BIN_MAGIC = 0x544F5053
SPOTANIM_BIN_VERSION = 1


@dataclass
class SpotAnimDef:
    """SpotAnimation definition from spotanim.dat."""

    gfx_id: int
    model_id: int = 0
    animation_id: int = -1
    resize_xy: int = 128  # 128 = 1.0x scale
    resize_z: int = 128
    rotation: int = 0
    brightness: int = 0
    shadow: int = 0
    recolor_src: list[int] = field(default_factory=list)
    recolor_dst: list[int] = field(default_factory=list)


def _parse_modern_spotanim_entry(gfx_id: int, data: bytes) -> SpotAnimDef:
    """Parse a single spotanim from modern cache opcode stream."""
    sa = SpotAnimDef(gfx_id=gfx_id)
    entry_buf = io.BytesIO(data)

    while True:
        opcode_raw = entry_buf.read(1)
        if len(opcode_raw) == 0:
            break
        opcode = opcode_raw[0]

        if opcode == 0:
            break
        elif opcode == 1:
            sa.model_id = struct.unpack(">H", entry_buf.read(2))[0]
        elif opcode == 2:
            anim_id = struct.unpack(">H", entry_buf.read(2))[0]
            sa.animation_id = anim_id if anim_id != 65535 else -1
        elif opcode == 3:
            sa.model_id = struct.unpack(">i", entry_buf.read(4))[0]
        elif opcode == 4:
            sa.resize_xy = struct.unpack(">H", entry_buf.read(2))[0]
        elif opcode == 5:
            sa.resize_z = struct.unpack(">H", entry_buf.read(2))[0]
        elif opcode == 6:
            sa.rotation = struct.unpack(">H", entry_buf.read(2))[0]
        elif opcode == 7:
            sa.brightness = entry_buf.read(1)[0]
        elif opcode == 8:
            sa.shadow = entry_buf.read(1)[0]
        elif opcode == 9:
            while True:
                b = entry_buf.read(1)
                if not b or b == b"\0":
                    break
        elif opcode == 40:
            length = entry_buf.read(1)[0]
            for _ in range(length):
                src = struct.unpack(">H", entry_buf.read(2))[0]
                dst = struct.unpack(">H", entry_buf.read(2))[0]
                sa.recolor_src.append(src)
                sa.recolor_dst.append(dst)
        elif opcode == 41:
            length = entry_buf.read(1)[0]
            for _ in range(length):
                entry_buf.read(4)  # skip retexture pairs
        else:
            print(f"  warning: unknown modern spotanim opcode {opcode} for GFX {gfx_id}")
            break

    return sa


def decode_spotanims_modern(reader: ModernCacheReader) -> dict[int, SpotAnimDef]:
    """Parse spotanim entries from modern cache (config index 2, group 13)."""
    files = reader.read_group(2, MODERN_SPOTANIM_CONFIG_GROUP)
    spotanims: dict[int, SpotAnimDef] = {}

    for gfx_id, entry_data in files.items():
        sa = _parse_modern_spotanim_entry(gfx_id, entry_data)
        spotanims[gfx_id] = sa

    return spotanims


def write_spotanims_binary(path: Path, spotanims: dict[int, SpotAnimDef]) -> None:
    """Write RuneC-compatible SPOT binary metadata."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as f:
        f.write(struct.pack("<III",
            SPOTANIM_BIN_MAGIC, SPOTANIM_BIN_VERSION, len(spotanims)))
        for gfx_id, sa in sorted(spotanims.items()):
            f.write(struct.pack("<IiiIIIii",
                gfx_id,
                sa.model_id,
                sa.animation_id,
                sa.resize_xy,
                sa.resize_z,
                sa.rotation,
                sa.brightness,
                sa.shadow))


# GFX IDs we need for the PvP viewer
TARGET_GFX_IDS = {
    27,    # crossbow bolt projectile
    368,   # ice barrage projectile (orb)
    369,   # ice barrage impact (freeze splash)
    377,   # blood barrage impact
    1468,  # dragon bolt projectile
}


def main() -> None:
    """Parse spotanim data and print metadata for target GFX IDs."""
    parser = argparse.ArgumentParser(description="parse spotanim data from OSRS cache")
    parser.add_argument(
        "--modern-cache",
        type=Path,
        required=True,
        help="path to modern OpenRS2 cache directory",
    )
    parser.add_argument(
        "--ids",
        type=str,
        default=None,
        help="comma-separated GFX IDs to show (default: all PvP-relevant)",
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="print all spotanims (not just targets)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="write RuneC-compatible spotanims.bin metadata",
    )
    args = parser.parse_args()

    cache_path = args.modern_cache
    print(f"reading modern cache from {cache_path}")

    modern_reader = ModernCacheReader(cache_path)
    spotanims = decode_spotanims_modern(modern_reader)
    print(f"parsed {len(spotanims)} spotanims total\n")

    if args.output:
        write_spotanims_binary(args.output, spotanims)
        print(f"wrote {len(spotanims)} spotanims to {args.output}\n")

    if args.ids:
        target_ids = {int(x) for x in args.ids.split(",")}
    elif args.all:
        target_ids = set(spotanims.keys())
    else:
        target_ids = TARGET_GFX_IDS

    print(f"{'GFX':>5}  {'model':>6}  {'anim':>5}  {'scaleXY':>7}  {'scaleZ':>6}  {'rot':>4}  recolors")
    print("-" * 70)

    for gfx_id in sorted(target_ids):
        sa = spotanims.get(gfx_id)
        if sa is None:
            print(f"{gfx_id:>5}  (not found)")
            continue

        recolors = ""
        if sa.recolor_src:
            recolors = ", ".join(
                f"{s}->{d}" for s, d in zip(sa.recolor_src, sa.recolor_dst)
            )

        print(
            f"{gfx_id:>5}  {sa.model_id:>6}  {sa.animation_id:>5}  "
            f"{sa.resize_xy:>7}  {sa.resize_z:>6}  {sa.rotation:>4}  {recolors}"
        )

    # print summary for integration
    model_ids = set()
    anim_ids = set()
    for gfx_id in target_ids:
        sa = spotanims.get(gfx_id)
        if sa:
            if sa.model_id > 0:
                model_ids.add(sa.model_id)
            if sa.animation_id >= 0:
                anim_ids.add(sa.animation_id)

    print(f"\nmodel IDs to add to export_models.py: {sorted(model_ids)}")
    print(f"anim seq IDs to add to export_animations.py: {sorted(anim_ids)}")


if __name__ == "__main__":
    main()
