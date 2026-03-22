"""Export sprite images from a 317-format OSRS cache.

Reads the media archive (cache index 0, file 4) and decodes indexed-color
sprites using the same format as tarnish's Sprite(StreamLoader, name, index)
constructor.

Sprite format (317 era):
  - <name>.dat: per-sprite pixel data (palette indices) + offset table
  - index.dat: shared index with palette, dimensions, and frame layout
  - Palette index 0 = transparent, remaining entries = 24-bit RGB

Usage:
    uv run python scripts/export_sprites.py \
        --cache ../reference/tarnish/game-server/data/cache \
        --sprite headicons_prayer \
        --output data/sprites/
"""

import argparse
import io
import struct
import sys
from pathlib import Path

# reuse cache infrastructure from export_collision_map.py
sys.path.insert(0, str(Path(__file__).parent))
from export_collision_map import CacheReader, decode_archive, hash_archive_name


def decode_sprite(
    dat: bytes, index_dat: bytes, frame_index: int
) -> tuple[bytes, int, int] | None:
    """Decode a single sprite frame from .dat + index.dat.

    Returns (rgba_bytes, width, height) or None on failure.

    Faithful to tarnish Sprite.java constructor (lines 376-417):
    1. Read 2-byte offset from .dat → seek index.dat to that offset
    2. From index.dat: resizeWidth(2), resizeHeight(2), palette_size(1)
    3. Read palette_size-1 RGB entries (3 bytes each), palette[0] = 0 (transparent)
    4. Skip through frame_index prior frames
    5. Read offsetX(1), offsetY(1), width(2), height(2), flags(1)
    6. Read width*height palette indices from .dat
    7. flags==0: row-major, flags==1: column-major
    """
    dat_buf = io.BytesIO(dat)
    idx_buf = io.BytesIO(index_dat)

    # read offset from .dat (2 bytes at position frame_index * 2... no, it's at the start)
    # actually, the offset is the FIRST 2 bytes of the .dat file
    dat_buf.seek(0)
    idx_offset = struct.unpack(">H", dat_buf.read(2))[0]

    # seek index.dat to that offset
    idx_buf.seek(idx_offset)

    # read resize dimensions and palette
    resize_w = struct.unpack(">H", idx_buf.read(2))[0]
    resize_h = struct.unpack(">H", idx_buf.read(2))[0]
    palette_size = idx_buf.read(1)[0] & 0xFF

    # palette: index 0 = transparent (0x000000), rest are 24-bit RGB
    palette = [0] * palette_size
    for i in range(1, palette_size):
        r, g, b = idx_buf.read(3)
        palette[i] = (r << 16) | (g << 8) | b

    # skip through prior frames
    for _ in range(frame_index):
        _ox = idx_buf.read(1)[0]  # noqa: F841
        _oy = idx_buf.read(1)[0]  # noqa: F841
        fw = struct.unpack(">H", idx_buf.read(2))[0]
        fh = struct.unpack(">H", idx_buf.read(2))[0]
        _flags = idx_buf.read(1)[0]  # noqa: F841
        # skip pixel data in .dat
        dat_buf.seek(dat_buf.tell() + fw * fh)

    # read this frame's header from index.dat
    offset_x = idx_buf.read(1)[0]  # noqa: F841
    offset_y = idx_buf.read(1)[0]  # noqa: F841
    width = struct.unpack(">H", idx_buf.read(2))[0]
    height = struct.unpack(">H", idx_buf.read(2))[0]
    flags = idx_buf.read(1)[0]

    if width == 0 or height == 0:
        return None

    # read palette indices from .dat
    pixel_count = width * height
    indices = list(dat_buf.read(pixel_count))

    # convert to RGBA
    rgba = bytearray(width * height * 4)

    if flags == 0:
        # row-major
        for i in range(pixel_count):
            rgb = palette[indices[i]]
            pos = i * 4
            rgba[pos] = (rgb >> 16) & 0xFF
            rgba[pos + 1] = (rgb >> 8) & 0xFF
            rgba[pos + 2] = rgb & 0xFF
            rgba[pos + 3] = 0 if indices[i] == 0 else 255
    elif flags == 1:
        # column-major: stored column by column
        for x in range(width):
            for y in range(height):
                idx = indices[x * height + y]
                pos = (y * width + x) * 4
                rgb = palette[idx]
                rgba[pos] = (rgb >> 16) & 0xFF
                rgba[pos + 1] = (rgb >> 8) & 0xFF
                rgba[pos + 2] = rgb & 0xFF
                rgba[pos + 3] = 0 if idx == 0 else 255

    return bytes(rgba), width, height


def save_png(rgba: bytes, width: int, height: int, path: Path) -> None:
    """Save RGBA pixel data as a PNG file."""
    try:
        from PIL import Image

        img = Image.frombytes("RGBA", (width, height), rgba)
        img.save(str(path))
    except ImportError:
        # fallback: write raw RGBA + dimensions as a simple binary
        with open(path.with_suffix(".rgba"), "wb") as f:
            f.write(struct.pack("<II", width, height))
            f.write(rgba)
        print(f"  (PIL not available, wrote raw RGBA to {path.with_suffix('.rgba')})")


def main() -> None:
    parser = argparse.ArgumentParser(description="Export sprites from 317 OSRS cache")
    parser.add_argument("--cache", required=True, help="Path to cache directory")
    parser.add_argument(
        "--sprite",
        default="headicons_prayer",
        help="Sprite archive name (default: headicons_prayer)",
    )
    parser.add_argument(
        "--count", type=int, default=8, help="Number of frames to export (default: 8)"
    )
    parser.add_argument(
        "--output", default="data/sprites/", help="Output directory for PNGs"
    )
    args = parser.parse_args()

    cache = CacheReader(Path(args.cache))

    # media archive is cache index 0, file 4
    media_raw = cache.get(0, 4)
    if not media_raw:
        print("ERROR: could not read media archive (cache 0, file 4)")
        sys.exit(1)

    media_files = decode_archive(media_raw)
    print(f"media archive: {len(media_files)} files")

    # look up the .dat and index.dat
    # hash_archive_name returns signed int (Java semantics), but decode_archive
    # reads hashes as unsigned (struct ">I"). convert to unsigned for lookup.
    dat_hash = hash_archive_name(f"{args.sprite}.dat") & 0xFFFFFFFF
    idx_hash = hash_archive_name("index.dat") & 0xFFFFFFFF

    dat_data = media_files.get(dat_hash)
    idx_data = media_files.get(idx_hash)

    if not dat_data:
        print(f"ERROR: '{args.sprite}.dat' not found in media archive (hash={dat_hash})")
        # list available hashes for debugging
        print(f"  available hashes: {sorted(media_files.keys())[:20]}...")
        sys.exit(1)

    if not idx_data:
        print(f"ERROR: 'index.dat' not found in media archive (hash={idx_hash})")
        sys.exit(1)

    print(f"found {args.sprite}.dat ({len(dat_data)} bytes) + index.dat ({len(idx_data)} bytes)")

    # export each frame
    out_dir = Path(args.output)
    out_dir.mkdir(parents=True, exist_ok=True)

    for i in range(args.count):
        result = decode_sprite(dat_data, idx_data, i)
        if result is None:
            print(f"  frame {i}: empty/invalid, skipping")
            continue
        rgba, w, h = result
        out_path = out_dir / f"{args.sprite}_{i}.png"
        save_png(rgba, w, h, out_path)
        print(f"  frame {i}: {w}x{h} -> {out_path}")

    print("done!")


if __name__ == "__main__":
    main()
