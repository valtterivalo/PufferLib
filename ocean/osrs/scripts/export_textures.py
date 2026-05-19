"""Texture atlas helpers for OSRS raylib rendering."""

import struct
from dataclasses import dataclass, field
from pathlib import Path

TEXTURE_SIZE = 128  # atlas cell size (vanilla sprites are 64x64 or 128x128)
ATLAS_COLS = 16  # textures per row in atlas
MODERN_SPRITE_INDEX = 8
MODERN_TEXTURE_INDEX = 9
TEXTURE_ANIM_MAGIC = 0x4D4E4154
TEXTURE_ANIM_VERSION = 1
FLAG_VERTICAL = 0x01
FLAG_ALPHA = 0x02


@dataclass
class SpriteData:
    """Decoded sprite pixel data."""

    width: int = 0
    height: int = 0
    pixels: bytes = b""  # RGBA, row-major, width*height*4 bytes


@dataclass
class TextureDef:
    """Decoded texture definition."""

    texture_id: int
    file_ids: list[int] = field(default_factory=list)
    average_color: int = 0
    opaque: bool = True
    animation_direction: int = 0
    animation_speed: int = 0


@dataclass
class TextureAtlas:
    """Built texture atlas with UV mapping info."""

    width: int = 0
    height: int = 0
    pixels: bytes = b""  # RGBA, width*height*4
    uv_map: dict[int, tuple[float, float, float, float]] = field(default_factory=dict)
    anim_map: dict[int, tuple[int, int, int, int, int]] = field(default_factory=dict)
    repeat_v_margin: float = 0.0
    white_u: float = 0.0
    white_v: float = 0.0


def decode_texture_definition(texture_id: int, data: bytes) -> TextureDef:
    """Decode one modern OSRS texture definition."""
    if len(data) == 7:
        return TextureDef(
            texture_id=texture_id,
            file_ids=[(data[0] << 8) | data[1]],
            average_color=(data[2] << 8) | data[3],
            opaque=data[4] != 0,
            animation_direction=data[5],
            animation_speed=data[6],
        )

    if len(data) < 4:
        raise ValueError(f"texture definition too small: {texture_id}")

    pos = 0
    average_color = (data[pos] << 8) | data[pos + 1]
    pos += 2
    opaque = data[pos] != 0
    pos += 1
    count = data[pos]
    pos += 1
    if count <= 0:
        return TextureDef(texture_id=texture_id, average_color=average_color, opaque=opaque)
    if pos + count * 2 > len(data):
        raise ValueError(f"truncated texture file id list: {texture_id}")

    file_ids = []
    for _ in range(count):
        file_ids.append((data[pos] << 8) | data[pos + 1])
        pos += 2

    if count > 1:
        pos += count - 1
        pos += count - 1
    pos += count * 4

    return TextureDef(
        texture_id=texture_id,
        file_ids=file_ids,
        average_color=average_color,
        opaque=opaque,
        animation_direction=data[pos] if pos < len(data) else 0,
        animation_speed=data[pos + 1] if pos + 1 < len(data) else 0,
    )


def load_texture_definitions(reader) -> dict[int, TextureDef]:
    """Load modern cache texture definitions."""
    files = reader.read_group(MODERN_TEXTURE_INDEX, 0)
    defs: dict[int, TextureDef] = {}
    for texture_id, data in files.items():
        if data:
            defs[texture_id] = decode_texture_definition(texture_id, data)
    return defs


def load_texture_average_colors(reader) -> dict[int, int]:
    """Load texture average colors keyed by texture id."""
    return {
        texture_id: texture.average_color
        for texture_id, texture in load_texture_definitions(reader).items()
    }


def decode_sprite_group(data: bytes) -> list[SpriteData]:
    """Decode a modern OSRS indexed sprite group."""
    if len(data) < 9:
        raise ValueError("sprite group too small")

    sprite_count = (data[-2] << 8) | data[-1]
    if sprite_count <= 0 or sprite_count > 1000:
        raise ValueError("invalid sprite count")

    meta_pos = len(data) - 7 - sprite_count * 8
    if meta_pos < 0:
        raise ValueError("invalid sprite metadata offset")

    max_width = (data[meta_pos] << 8) | data[meta_pos + 1]
    max_height = (data[meta_pos + 2] << 8) | data[meta_pos + 3]
    palette_length = (data[meta_pos + 4] & 0xFF) + 1
    if max_width <= 0 or max_height <= 0:
        raise ValueError("invalid sprite canvas dimensions")
    if palette_length < 1 or palette_length > 256:
        raise ValueError("invalid sprite palette length")

    pos = meta_pos + 5
    x_offsets = [0] * sprite_count
    y_offsets = [0] * sprite_count
    sub_widths = [0] * sprite_count
    sub_heights = [0] * sprite_count
    for i in range(sprite_count):
        x_offsets[i] = (data[pos] << 8) | data[pos + 1]
        pos += 2
    for i in range(sprite_count):
        y_offsets[i] = (data[pos] << 8) | data[pos + 1]
        pos += 2
    for i in range(sprite_count):
        sub_widths[i] = (data[pos] << 8) | data[pos + 1]
        pos += 2
    for i in range(sprite_count):
        sub_heights[i] = (data[pos] << 8) | data[pos + 1]
        pos += 2

    palette_start = meta_pos - (palette_length - 1) * 3
    if palette_start < 0:
        raise ValueError("invalid sprite palette offset")

    palette = [0] * palette_length
    pos = palette_start
    for i in range(1, palette_length):
        rgb = (data[pos] << 16) | (data[pos + 1] << 8) | data[pos + 2]
        palette[i] = rgb if rgb != 0 else 1
        pos += 3

    pixel_pos = 0
    sprites: list[SpriteData] = []
    for i in range(sprite_count):
        width = sub_widths[i]
        height = sub_heights[i]
        dimension = width * height
        canvas_width = max(max_width, width + x_offsets[i], 1)
        canvas_height = max(max_height, height + y_offsets[i], 1)
        if dimension <= 0:
            sprites.append(SpriteData(
                width=canvas_width,
                height=canvas_height,
                pixels=bytes(canvas_width * canvas_height * 4),
            ))
            continue
        if pixel_pos >= palette_start:
            raise ValueError("sprite pixels overlap metadata")

        flags = data[pixel_pos]
        pixel_pos += 1
        indices = bytearray(dimension)
        alphas = bytearray(dimension)

        if flags & FLAG_VERTICAL:
            for x in range(width):
                for y in range(height):
                    indices[y * width + x] = data[pixel_pos]
                    pixel_pos += 1
        else:
            indices[:] = data[pixel_pos : pixel_pos + dimension]
            pixel_pos += dimension

        if flags & FLAG_ALPHA:
            if flags & FLAG_VERTICAL:
                for x in range(width):
                    for y in range(height):
                        alphas[y * width + x] = data[pixel_pos]
                        pixel_pos += 1
            else:
                alphas[:] = data[pixel_pos : pixel_pos + dimension]
                pixel_pos += dimension
        else:
            for j, idx in enumerate(indices):
                if idx:
                    alphas[j] = 0xFF

        for j, idx in enumerate(indices):
            if idx:
                alphas[j] = 0xFF

        rgba = bytearray(canvas_width * canvas_height * 4)
        for y in range(height):
            for x in range(width):
                src = y * width + x
                pal_idx = indices[src]
                alpha = alphas[src]
                rgb = palette[pal_idx] if pal_idx < len(palette) else 0xFF00FF
                dst_x = x + x_offsets[i]
                dst_y = y + y_offsets[i]
                if dst_x >= canvas_width or dst_y >= canvas_height:
                    continue
                dst = (dst_y * canvas_width + dst_x) * 4
                rgba[dst] = (rgb >> 16) & 0xFF
                rgba[dst + 1] = (rgb >> 8) & 0xFF
                rgba[dst + 2] = rgb & 0xFF
                rgba[dst + 3] = alpha

        sprites.append(SpriteData(width=canvas_width, height=canvas_height, pixels=bytes(rgba)))

    return sprites


def load_sprite(reader, sprite_id: int, frame: int = 0) -> SpriteData:
    """Load one sprite frame from the modern cache."""
    data = reader.read_container(MODERN_SPRITE_INDEX, sprite_id)
    if data is None:
        raise KeyError(f"sprite group not found: {sprite_id}")
    sprites = decode_sprite_group(data)
    if frame >= len(sprites):
        raise KeyError(f"sprite frame not found: {sprite_id}:{frame}")
    return sprites[frame]


def load_texture_sprites(reader, texture_ids: set[int] | None = None) -> dict[int, SpriteData]:
    """Load the first sprite for each requested texture definition."""
    textures = load_texture_definitions(reader)
    ids_to_load = set(textures) if texture_ids is None else set(texture_ids)
    sprites: dict[int, SpriteData] = {}
    for texture_id in sorted(ids_to_load):
        texture = textures.get(texture_id)
        if texture is None or not texture.file_ids:
            continue
        try:
            sprites[texture_id] = load_sprite(reader, texture.file_ids[0])
        except (KeyError, ValueError):
            continue
    return sprites


def build_atlas(
    sprites: dict[int, SpriteData],
    cell_size: int = TEXTURE_SIZE,
    repeat_v_padding: int = 0,
) -> TextureAtlas:
    """Build a texture atlas from decoded sprites."""
    tex_ids = sorted(sprites.keys())
    total_slots = 1 + len(tex_ids)

    repeat_v_padding = max(0, int(repeat_v_padding))
    slot_w = cell_size
    slot_h = cell_size + repeat_v_padding * 2
    cols = ATLAS_COLS
    rows = (total_slots + cols - 1) // cols

    atlas_w = cols * slot_w
    atlas_h = rows * slot_h
    atlas_pixels = bytearray(atlas_w * atlas_h * 4)

    for y in range(slot_h):
        for x in range(slot_w):
            idx = (y * atlas_w + x) * 4
            atlas_pixels[idx] = 255
            atlas_pixels[idx + 1] = 255
            atlas_pixels[idx + 2] = 255
            atlas_pixels[idx + 3] = 255

    uv_map: dict[int, tuple[float, float, float, float]] = {}
    anim_map: dict[int, tuple[int, int, int, int, int]] = {}

    for slot_idx, tex_id in enumerate(tex_ids, start=1):
        sprite = sprites[tex_id]
        col = slot_idx % cols
        row = slot_idx // cols
        ax = col * slot_w
        ay = row * slot_h

        _blit_sprite_to_atlas(
            atlas_pixels,
            atlas_w,
            ax,
            ay,
            cell_size,
            sprite,
            repeat_v_padding=repeat_v_padding,
        )

        u_off = ax / atlas_w
        v_off = (ay + repeat_v_padding) / atlas_h
        u_size = cell_size / atlas_w
        v_size = cell_size / atlas_h
        uv_map[tex_id] = (u_off, v_off, u_size, v_size)
        anim_map[tex_id] = (ax, ay, cell_size, slot_h, repeat_v_padding)

    white_u = 0.5 * slot_w / atlas_w
    white_v = (repeat_v_padding + 0.5 * cell_size) / atlas_h

    return TextureAtlas(
        width=atlas_w,
        height=atlas_h,
        pixels=bytes(atlas_pixels),
        uv_map=uv_map,
        anim_map=anim_map,
        repeat_v_margin=(repeat_v_padding / cell_size) if cell_size > 0 else 0.0,
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
    repeat_v_padding: int = 0,
) -> None:
    """Copy sprite pixels into an atlas cell, nearest-neighbor resize if needed."""
    sw = sprite.width
    sh = sprite.height
    sp = sprite.pixels
    slot_h = cell_size + repeat_v_padding * 2

    for dy in range(slot_h):
        logical_y = dy - repeat_v_padding
        wrapped_y = logical_y % cell_size
        for dx in range(cell_size):
            sx = dx * sw // cell_size
            sy = wrapped_y * sh // cell_size
            si = (sy * sw + sx) * 4

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


def write_texture_anim_binary(path: Path, atlas: TextureAtlas, texture_defs: dict[int, TextureDef]) -> None:
    """Write atlas-cell animation metadata."""
    rows: list[tuple[int, int, int, int, int, int, int]] = []
    for tex_id, uv in sorted(atlas.uv_map.items()):
        tex = texture_defs.get(tex_id)
        direction = tex.animation_direction if tex else 0
        speed = tex.animation_speed if tex else 0
        if direction == 0 or speed == 0:
            continue
        if tex_id in atlas.anim_map:
            x, y, w, h, repeat_v_padding = atlas.anim_map[tex_id]
        else:
            u, v, uw, vh = uv
            x = round(u * atlas.width)
            y = round(v * atlas.height)
            w = round(uw * atlas.width)
            h = round(vh * atlas.height)
            repeat_v_padding = 0
        rows.append((tex_id, x, y, w, h, direction, speed, repeat_v_padding))

    with path.open("wb") as f:
        f.write(struct.pack("<III", TEXTURE_ANIM_MAGIC, TEXTURE_ANIM_VERSION, len(rows)))
        for tex_id, x, y, w, h, direction, speed, repeat_v_padding in rows:
            f.write(struct.pack(
                "<IHHHHBBH",
                tex_id,
                x,
                y,
                w,
                h,
                direction & 0xFF,
                speed & 0xFF,
                repeat_v_padding & 0xFFFF,
            ))
