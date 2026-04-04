"""Texture atlas data structures and utilities for OSRS raylib rendering.

Provides SpriteData / TextureAtlas dataclasses and atlas-building helpers
used by other exporters (e.g. export_objects.py). Texture pixel data is
loaded externally (OpenRS2 cache) and passed in as SpriteData dicts.

Usage:
    from export_textures import TextureAtlas, build_atlas
"""

import struct
from dataclasses import dataclass, field
from pathlib import Path

TEXTURE_SIZE = 128  # atlas cell size (vanilla sprites are 64x64 or 128x128)
ATLAS_COLS = 16  # textures per row in atlas


@dataclass
class SpriteData:
    """Decoded sprite pixel data."""

    width: int = 0
    height: int = 0
    pixels: bytes = b""  # RGBA, row-major, width*height*4 bytes


@dataclass
class TextureAtlas:
    """Built texture atlas with UV mapping info."""

    width: int = 0
    height: int = 0
    pixels: bytes = b""  # RGBA, width*height*4
    # mapping: texture_id -> (u_offset, v_offset, u_size, v_size) in [0,1] range
    uv_map: dict[int, tuple[float, float, float, float]] = field(default_factory=dict)
    # white pixel UV for non-textured faces
    white_u: float = 0.0
    white_v: float = 0.0


def build_atlas(
    sprites: dict[int, SpriteData],
    cell_size: int = TEXTURE_SIZE,
) -> TextureAtlas:
    """Build a texture atlas from decoded sprites.

    Layout: grid of cell_size x cell_size cells.
    Slot 0: solid white (for non-textured faces -- vertex colors show through).
    Slots 1..N: actual texture sprites, resized to cell_size if needed.

    Returns TextureAtlas with RGBA pixel data and UV mapping.
    """
    # sort texture IDs for deterministic layout
    tex_ids = sorted(sprites.keys())
    total_slots = 1 + len(tex_ids)  # slot 0 = white

    cols = ATLAS_COLS
    rows = (total_slots + cols - 1) // cols

    atlas_w = cols * cell_size
    atlas_h = rows * cell_size
    atlas_pixels = bytearray(atlas_w * atlas_h * 4)

    # slot 0: solid white
    for y in range(cell_size):
        for x in range(cell_size):
            idx = (y * atlas_w + x) * 4
            atlas_pixels[idx] = 255
            atlas_pixels[idx + 1] = 255
            atlas_pixels[idx + 2] = 255
            atlas_pixels[idx + 3] = 255

    uv_map: dict[int, tuple[float, float, float, float]] = {}

    for slot_idx, tex_id in enumerate(tex_ids, start=1):
        sprite = sprites[tex_id]
        col = slot_idx % cols
        row = slot_idx // cols
        ax = col * cell_size
        ay = row * cell_size

        # copy sprite pixels into atlas cell, resizing if needed
        _blit_sprite_to_atlas(
            atlas_pixels, atlas_w, ax, ay, cell_size, sprite,
        )

        # UV mapping: normalized coordinates
        u_off = ax / atlas_w
        v_off = ay / atlas_h
        u_size = cell_size / atlas_w
        v_size = cell_size / atlas_h
        uv_map[tex_id] = (u_off, v_off, u_size, v_size)

    # white pixel UV (center of slot 0)
    white_u = 0.5 * cell_size / atlas_w
    white_v = 0.5 * cell_size / atlas_h

    return TextureAtlas(
        width=atlas_w,
        height=atlas_h,
        pixels=bytes(atlas_pixels),
        uv_map=uv_map,
        white_u=white_u,
        white_v=white_v,
    )


def _blit_sprite_to_atlas(
    atlas: bytearray,
    atlas_w: int,
    ax: int,
    ay: int,
    cell_size: int,
    sprite: SpriteData,
) -> None:
    """Copy sprite pixels into an atlas cell, nearest-neighbor resize if needed."""
    sw = sprite.width
    sh = sprite.height
    sp = sprite.pixels

    for dy in range(cell_size):
        for dx in range(cell_size):
            # source pixel (nearest neighbor)
            sx = dx * sw // cell_size
            sy = dy * sh // cell_size
            si = (sy * sw + sx) * 4

            # destination in atlas
            di = ((ay + dy) * atlas_w + (ax + dx)) * 4

            if si + 3 < len(sp):
                atlas[di] = sp[si]
                atlas[di + 1] = sp[si + 1]
                atlas[di + 2] = sp[si + 2]
                atlas[di + 3] = sp[si + 3]


def write_atlas_binary(path: Path, atlas: TextureAtlas) -> None:
    """Write texture atlas as raw RGBA binary with header.

    Format:
      uint32 magic = 0x41544C53 ("ATLS")
      uint32 width
      uint32 height
      uint8  pixels[width * height * 4]  (RGBA)
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as f:
        f.write(struct.pack("<I", 0x41544C53))  # "ATLS"
        f.write(struct.pack("<I", atlas.width))
        f.write(struct.pack("<I", atlas.height))
        f.write(atlas.pixels)
