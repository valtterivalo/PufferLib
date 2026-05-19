"""Export sprites from modern OSRS cache (OpenRS2 flat format) to PNG files.

Reads sprite archives from cache index 8 and decodes them using the
SpriteLoader format from the deobfuscated client (trailer-based format
with palette, per-frame offsets, and optional alpha channel).

Exports specific sprite IDs needed for the debug viewer GUI:
  - equipment slot backgrounds (156-165, 170)
  - prayer icons enabled/disabled (115-154, 502-509, 945-951, 1420-1427)
  - tab icons (168, 776, 779, 780, 900, 901)
  - spell icons (325-348, 375-398, 557, 561, 564, 607, 611, 614)
  - combat interface sprites (657)

Usage:
  uv run python scripts/export_sprites_modern.py \
    --cache ../../../.refs/osrs-cache-modern \
    --output data/sprites/gui
"""

import argparse
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

# add parent for modern_cache_reader import
sys.path.insert(0, str(Path(__file__).parent))
from modern_cache_reader import ModernCacheReader
from rc_cache import decode_sprite_group

DEFAULT_MODERN_CACHE = Path(__file__).resolve().parents[3] / ".refs" / "osrs-cache-modern"


@dataclass
class SpriteFrame:
    """Single sprite frame decoded from cache archive."""

    group_id: int = 0
    frame: int = 0
    offset_x: int = 0
    offset_y: int = 0
    width: int = 0
    height: int = 0
    max_width: int = 0
    max_height: int = 0
    pixels: list[int] = field(default_factory=list)  # ARGB int array


def decode_sprites(group_id: int, data: bytes) -> list[SpriteFrame]:
    """Decode a sprite group through the shared RuneC cache pipeline."""
    frames = []
    for frame_idx, sprite in enumerate(decode_sprite_group(data)):
        pixels = []
        raw = sprite.pixels
        for offset in range(0, len(raw), 4):
            r = raw[offset]
            g = raw[offset + 1]
            b = raw[offset + 2]
            a = raw[offset + 3]
            pixels.append((a << 24) | (r << 16) | (g << 8) | b)
        frames.append(
            SpriteFrame(
                group_id=group_id,
                frame=frame_idx,
                width=sprite.width,
                height=sprite.height,
                max_width=sprite.width,
                max_height=sprite.height,
                pixels=pixels,
            )
        )
    return frames


# sprite IDs whose RGB should be whitened in opaque pixels at save time. these
# are alpha-mask sprites used as tinted overlays in the renderer — the source
# cache ships them with near-black RGB which would multiply tinting to black,
# defeating the overlay's purpose. normalizing to white RGB lets the tint flow
# through correctly.
ALPHA_MASK_NORMALIZE_IDS: set[int] = {1183, 1184, 1178, 1179}


def save_sprite_png(sprite: SpriteFrame, path: Path, sprite_id: int = -1) -> None:
    """Save a sprite frame as RGBA PNG using pure Python (no PIL dependency)."""
    try:
        from PIL import Image
    except ImportError:
        print(f"  pillow not available, skipping {path}", file=sys.stderr)
        return

    if sprite.width <= 0 or sprite.height <= 0:
        print(f"  skipping {path.name}: zero-size frame ({sprite.width}x{sprite.height})",
              file=sys.stderr)
        return
    expected = sprite.width * sprite.height
    if len(sprite.pixels) != expected:
        print(f"  skipping {path.name}: pixel count {len(sprite.pixels)} != "
              f"{sprite.width}x{sprite.height} = {expected}", file=sys.stderr)
        return

    normalize_rgb = sprite_id in ALPHA_MASK_NORMALIZE_IDS
    canvas_w = sprite.max_width if sprite.max_width > 0 else sprite.width
    canvas_h = sprite.max_height if sprite.max_height > 0 else sprite.height
    if sprite.offset_x < 0 or sprite.offset_y < 0:
        raise ValueError(f"{path.name}: negative sprite offset")
    if sprite.offset_x + sprite.width > canvas_w or sprite.offset_y + sprite.height > canvas_h:
        raise ValueError(f"{path.name}: sprite frame exceeds max canvas")

    frame_img = Image.new("RGBA", (sprite.width, sprite.height))
    rgba_data = []
    for argb in sprite.pixels:
        a = (argb >> 24) & 0xFF
        if normalize_rgb and a > 0:
            r, g, b = 255, 255, 255
        else:
            r = (argb >> 16) & 0xFF
            g = (argb >> 8) & 0xFF
            b = argb & 0xFF
        rgba_data.append((r, g, b, a))
    frame_img.putdata(rgba_data)

    img = Image.new("RGBA", (canvas_w, canvas_h), (0, 0, 0, 0))
    img.alpha_composite(frame_img, (sprite.offset_x, sprite.offset_y))
    img.save(str(path))


# sprite IDs to export, organized by category
SPRITE_IDS: dict[str, list[int]] = {
    # equipment slot background icons
    "equip": [156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 170],
    # prayer icons (enabled)
    "prayer": [
        115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126,
        127, 128, 129, 130, 131, 132, 133, 134,  # base prayers
        502, 503, 504, 505,  # hawk eye, mystic lore, eagle eye, mystic might
        945, 946, 947,  # chivalry, piety, preserve
        1420, 1421,  # rigour, augury
    ],
    # prayer icons (disabled/greyed)
    "prayer_off": [
        135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146,
        147, 148, 149, 150, 151, 152, 153, 154,  # base disabled
        506, 507, 508, 509,  # hawk/mystic disabled
        949, 950, 951,  # chivalry/piety/preserve disabled
        1424, 1425,  # rigour/augury disabled
    ],
    # tab icons (combat, stats, quests, inventory, equipment, prayer, magic)
    "tab": [168, 776, 779, 780, 898, 899, 900, 901, 1583],
    # ancient spell icons (enabled + disabled)
    "spell_ancient": [
        325, 326, 327, 328,  # ice rush/burst/blitz/barrage
        329, 330, 331, 332,  # smoke rush/burst/blitz/barrage
        333, 334, 335, 336,  # blood rush/burst/blitz/barrage
        337, 338, 339, 340,  # shadow rush/burst/blitz/barrage
        341, 342, 343, 344,  # Paddewwa/Senntisten/Kharyrll/Lassar
        345, 346, 347, 348,  # Dareeyak/Carrallanger/Annakarl/Ghorrock
        375, 376, 377, 378,  # ice disabled
        379, 380, 381, 382,  # smoke disabled
        383, 384, 385, 386,  # blood disabled
        387, 388, 389, 390,  # shadow disabled
        391, 392, 393, 394,  # ancient teleports disabled
        395, 396, 397, 398,  # ancient teleports disabled
    ],
    # lunar spell icons
    "spell_lunar": [557, 561, 564, 607, 611, 614],
    # combat interface
    "combat": [657],
    # interface chrome: side panel background, tab stones, equipment slot chrome
    "chrome": [
        1031,  # FIXED_MODE_SIDE_PANEL_BACKGROUND
        1032,  # FIXED_MODE_TABS_ROW_BOTTOM
        1036,  # FIXED_MODE_TABS_ROW_TOP
        1026, 1027, 1028, 1029, 1030,  # TAB_STONE_*_SELECTED corners + middle
        179,   # EQUIPMENT_SLOT_SELECTED
        952, 953,  # SLANTED_TAB, SLANTED_TAB_HOVERED
        1071, 1072,  # MINIMAP_ORB_FRAME, _HOVERED
        1017,  # CHATBOX background
        1018,  # CHATBOX_BUTTONS_BACKGROUND_STONES
        166,   # EQUIPMENT_SLOT_AMMUNITION (we had 156-165, was missing 166)
        171, 172, 173,  # iron rivets (square, vertical, horizontal)
    ],
    # minimap chrome: compass, orb fills, orb icons, frame mask, entity dots.
    # all sprite IDs taken from runelite-api SpriteID.java.
    # includes both fixed-mode and resizable-mode (OSRS-stretch / "Old School
    # Box") variants so the renderer can switch layouts at runtime.
    "minimap": [
        169,   # COMPASS_TEXTURE (rotated with camera yaw)
        1037,  # FIXED_MODE_MINIMAP_LEFT_EDGE (chrome surrounding the circle)
        1038,  # FIXED_MODE_MINIMAP_RIGHT_EDGE
        1182,  # FIXED_MODE_MINIMAP_AND_COMPASS_FRAME (full bezel)
        1183,  # FIXED_MODE_MINIMAP_ALPHA_MASK (circle cutout)
        1184,  # FIXED_MODE_COMPASS_ALPHA_MASK
        # resizable-mode (OSRS-stretch) chrome
        897,   # RESIZEABLE_MODE_SIDE_PANEL_BACKGROUND
        1173,  # RESIZEABLE_MODE_TABS_TOP_ROW
        1174,  # RESIZEABLE_MODE_TABS_BOTTOM_ROW
        1175,  # RESIZEABLE_MODE_SIDE_PANEL_EDGE_LEFT
        1176,  # RESIZEABLE_MODE_SIDE_PANEL_EDGE_RIGHT
        1177,  # RESIZEABLE_MODE_MINIMAP_AND_COMPASS_FRAME
        1178,  # RESIZEABLE_MODE_MINIMAP_ALPHA_MASK
        1179,  # RESIZEABLE_MODE_COMPASS_ALPHA_MASK
        1180,  # RESIZEABLE_MODE_TAB_STONE_MIDDLE
        1181,  # RESIZEABLE_MODE_TAB_STONE_MIDDLE_SELECTED
        1059,  # MINIMAP_ORB_EMPTY (greyed orb backing)
        1060,  # MINIMAP_ORB_HITPOINTS (green-fill chrome)
        1063,  # MINIMAP_ORB_PRAYER
        1064,  # MINIMAP_ORB_RUN
        1065,  # MINIMAP_ORB_RUN_ACTIVATED
        1066,  # MINIMAP_ORB_PRAYER_ACTIVATED
        1067,  # MINIMAP_ORB_HITPOINTS_ICON (small heart inside HP orb)
        1068,  # MINIMAP_ORB_PRAYER_ICON
        1069,  # MINIMAP_ORB_WALK_ICON
        1070,  # MINIMAP_ORB_RUN_ICON
        1058,  # MINIMAP_ORB_PRAYER_ICON_ACTIVATED
        # entity dot sprites: drawn over the minimap circle for items, NPCs, players
        510,   # MINIMAP_MARKER_RED_ITEM
        511,   # MINIMAP_MARKER_YELLOW_NPC
        512,   # MINIMAP_MARKER_WHITE_PLAYER
        513,   # MINIMAP_MARKER_GREEN_PLAYER_FRIEND
        422,   # MINIMAP_DESTINATION_FLAG
        441,   # MINIMAP_GUIDE_ARROW_YELLOW
    ],
    # click cross animations (4 yellow move + 4 red attack, 16x16 each)
    "click_cross": [515, 516, 517, 518, 519, 520, 521, 522],
    # overhead prayer headicons (multi-frame: 0=melee, 1=ranged, 2=magic, 3=retribution, 4=smite, 5=redemption)
    "headicons_prayer": [440],
    # hitsplat sprites (each is a single-frame sprite group)
    "hitmarks": [1358, 1359, 1360, 1361, 1362],
}

# human-readable names for specific sprite IDs
SPRITE_NAMES: dict[int, str] = {
    156: "slot_head", 157: "slot_cape", 158: "slot_neck", 159: "slot_weapon",
    160: "slot_ring", 161: "slot_body", 162: "slot_shield", 163: "slot_legs",
    164: "slot_hands", 165: "slot_feet", 170: "slot_tile",
    168: "tab_combat", 776: "tab_quests", 779: "tab_prayer",
    780: "tab_magic", 898: "tab_stats", 899: "tab_quests2",
    900: "tab_inventory", 901: "tab_equipment",
    # prayer/spell icons are loaded BY NUMERIC ID in osrs_gui.h, so keep them
    # as numeric filenames (no names here → fallback to str(sprite_id)).
    657: "special_attack",
    # interface chrome
    1031: "side_panel_bg",
    1032: "tabs_row_bottom",
    1036: "tabs_row_top",
    1026: "tab_stone_tl_sel",
    1027: "tab_stone_tr_sel",
    1028: "tab_stone_bl_sel",
    1029: "tab_stone_br_sel",
    1030: "tab_stone_mid_sel",
    179: "slot_selected",
    952: "slanted_tab",
    953: "slanted_tab_hover",
    1071: "orb_frame",
    1072: "orb_frame_hover",
    # minimap chrome (canonical sprite IDs from runelite-api SpriteID.java)
    169: "compass",
    1037: "minimap_edge_left",
    1038: "minimap_edge_right",
    1182: "minimap_and_compass_frame",
    1183: "minimap_alpha_mask",
    1184: "compass_alpha_mask",
    # resizable-mode (OSRS-stretch / Old School Box layout)
    897: "rm_side_panel_bg",
    1173: "rm_tabs_top_row",
    1174: "rm_tabs_bottom_row",
    1175: "rm_side_panel_edge_left",
    1176: "rm_side_panel_edge_right",
    1177: "rm_minimap_and_compass_frame",
    1178: "rm_minimap_alpha_mask",
    1179: "rm_compass_alpha_mask",
    1180: "rm_tab_stone_middle",
    1181: "rm_tab_stone_middle_selected",
    1059: "orb_empty",
    1060: "orb_hp",
    1063: "orb_prayer",
    1064: "orb_run",
    1065: "orb_run_active",
    1066: "orb_prayer_active",
    1067: "orb_icon_hp",
    1068: "orb_icon_prayer",
    1069: "orb_icon_walk",
    1070: "orb_icon_run",
    1058: "orb_icon_prayer_active",
    510: "minimap_dot_item",
    511: "minimap_dot_npc",
    512: "minimap_dot_player",
    513: "minimap_dot_friend",
    422: "minimap_dest_flag",
    441: "minimap_guide_arrow",
    1017: "chatbox_bg",
    1018: "chatbox_stones",
    166: "slot_ammo",
    171: "rivets_square",
    172: "rivets_vertical",
    173: "rivets_horizontal",
    515: "cross_yellow_1", 516: "cross_yellow_2",
    517: "cross_yellow_3", 518: "cross_yellow_4",
    519: "cross_red_1", 520: "cross_red_2",
    521: "cross_red_3", 522: "cross_red_4",
    # overhead prayer headicons (group 440, 6 frames)
    440: "headicons_prayer",
    # hitsplats: 0=blue miss, 1=red damage, 2=green poison, 3=disease, 4=venom
    1358: "hitmarks_0", 1359: "hitmarks_1",
    1360: "hitmarks_2", 1361: "hitmarks_3", 1362: "hitmarks_4",
}

RUNEC_UI_ALIASES: dict[int, list[str]] = {
    897: ["tradebacking_dark"],
    1026: ["side_stone_highlights_0"],
    1027: ["side_stone_highlights_1"],
    1028: ["side_stone_highlights_2"],
    1029: ["side_stone_highlights_3"],
    1030: ["side_stone_highlights_4"],
    1173: ["osrs_stretch_side_topbottom_0"],
    1174: ["osrs_stretch_side_topbottom_1"],
    1175: ["osrs_stretch_side_columns_0"],
    1176: ["osrs_stretch_side_columns_1"],
    1177: ["osrs_stretch_mapsurround"],
    1178: ["resize_map_mask"],
    1179: ["resize_compass_mask"],
    168: ["side_icons_0", "side_icon_combat"],
    898: ["side_icons_1", "side_icon_stats"],
    899: ["side_icons_2", "side_icon_quests"],
    900: ["side_icons_3", "side_icon_inventory"],
    901: ["side_icons_4", "side_icon_equipment"],
    902: ["side_icons_5", "side_icon_prayer"],
    903: ["side_icons_6", "side_icon_magic"],
    1583: ["side_icon_magic_ancient"],
    904: ["side_icons_7", "side_icon_clan"],
    905: ["side_icons_8", "side_icon_friends"],
    907: ["side_icons_10", "side_icon_logout"],
    908: ["side_icons_11", "side_icon_options"],
    909: ["side_icons_12", "side_icon_emotes"],
    910: ["side_icons_13", "side_icon_music"],
    1709: ["side_icons_22", "side_icon_grouping"],
    3560: ["side_icons_39", "side_icon_logout_modern"],
    293: ["combatboxes_0"],
    294: ["combatboxes_1"],
    295: ["combatboxes_2"],
    296: ["combatboxes_3"],
    653: ["combatboxes_large_0"],
    654: ["combatboxes_large_1"],
    655: ["combatboxes_very_large_0"],
    656: ["combatboxes_very_large_1"],
    657: ["combatboxes_special_attack"],
    760: ["combat_shield"],
    675: ["options_icons_16"],
    912: ["options_icons_18"],
    1090: ["options_icons_28"],
    1343: ["whistle"],
    1052: ["sideicons_interface_14"],
    1053: ["sideicons_interface_15"],
    1299: ["sideicons_interface_16"],
    1071: ["orb_frame_0"],
    1072: ["orb_frame_1"],
    2140: ["orb_frame_2"],
    1059: ["orb_filler_0"],
    1060: ["orb_filler_1"],
    1061: ["orb_filler_2"],
    1062: ["orb_filler_3"],
    1063: ["orb_filler_4"],
    1064: ["orb_filler_5"],
    1065: ["orb_filler_6"],
    1066: ["orb_filler_7"],
    1102: ["orb_filler_8"],
    1607: ["orb_filler_9"],
    1608: ["orb_filler_10"],
    1609: ["orb_filler_11"],
    1636: ["orb_filler_12"],
    1637: ["orb_filler_13"],
    2208: ["orb_filler_14"],
    1067: ["orb_icon_0"],
    1068: ["orb_icon_1"],
    1069: ["orb_icon_2"],
    1070: ["orb_icon_3"],
    1058: ["orb_icon_4"],
    1092: ["orb_icon_5"],
    1610: ["orb_icon_6"],
    1668: ["orb_icon_7"],
    3015: ["orb_icon_8"],
    3016: ["orb_icon_9"],
    3017: ["orb_icon_10"],
    3018: ["orb_icon_11"],
    3019: ["orb_icon_12"],
    3020: ["orb_icon_13"],
    3021: ["orb_icon_14"],
    3022: ["orb_icon_15"],
    2138: ["ring_34_0"],
    5794: ["tli_button01_orb01_34x34_0"],
    1196: ["orb_xp_0"],
    1438: ["ring_30"],
    1439: ["worldmap_icon_0"],
    2420: ["wiki_icon_0"],
    189: ["stats_total_left"],
    190: ["stats_total_right"],
    191: ["stats_total_middle"],
    215: ["staticons2_0", "skill_icon_18"],
    216: ["staticons2_1", "skill_icon_19"],
    217: ["staticons2_2", "skill_icon_20"],
    220: ["staticons2_5", "skill_icon_21"],
    221: ["staticons2_6", "skill_icon_22"],
    222: ["staticons2_7", "skill_icon_23"],
    944: ["prayeron_24"],
    948: ["prayeroff_24"],
}

for frame, sprite_id in enumerate(range(233, 253)):
    RUNEC_UI_ALIASES.setdefault(sprite_id, []).append(f"combaticons_{frame}")
for frame, sprite_id in enumerate(range(253, 273)):
    RUNEC_UI_ALIASES.setdefault(sprite_id, []).append(f"combaticons2_{frame}")
for frame, sprite_id in enumerate(range(273, 293)):
    RUNEC_UI_ALIASES.setdefault(sprite_id, []).append(f"combaticons3_{frame}")
for frame, sprite_id in enumerate(range(774, 788)):
    RUNEC_UI_ALIASES.setdefault(sprite_id, []).append(f"sideicons_interface_{frame}")
for frame, sprite_id in enumerate(range(197, 215)):
    RUNEC_UI_ALIASES.setdefault(sprite_id, []).extend([f"staticons_{frame}", f"skill_icon_{frame}"])
for frame, sprite_id in enumerate(range(3051, 3057)):
    RUNEC_UI_ALIASES.setdefault(sprite_id, []).append(f"chat_tab_button_{frame}")
for frame, sprite_id in enumerate(range(156, 168)):
    RUNEC_UI_ALIASES.setdefault(sprite_id, []).append(f"wornicons_{frame}")
for frame, sprite_id in enumerate(range(170, 185)):
    RUNEC_UI_ALIASES.setdefault(sprite_id, []).append(f"miscgraphics_{frame}")
for frame in range(20):
    RUNEC_UI_ALIASES.setdefault(115 + frame, []).append(f"prayeron_{frame}")
    RUNEC_UI_ALIASES.setdefault(135 + frame, []).append(f"prayeroff_{frame}")
for frame, sprite_id in enumerate(range(502, 506), start=20):
    RUNEC_UI_ALIASES.setdefault(sprite_id, []).append(f"prayeron_{frame}")
for frame, sprite_id in enumerate(range(506, 510), start=20):
    RUNEC_UI_ALIASES.setdefault(sprite_id, []).append(f"prayeroff_{frame}")


def sprite_output_names(sprite_id: int) -> list[str]:
    """Return every filename stem that should be emitted for a sprite group."""
    names = [SPRITE_NAMES.get(sprite_id, str(sprite_id))]
    for alias in RUNEC_UI_ALIASES.get(sprite_id, []):
        if alias not in names:
            names.append(alias)
    return names


def main() -> None:
    """Export GUI sprites from modern OSRS cache."""
    parser = argparse.ArgumentParser(description="Export OSRS GUI sprites")
    parser.add_argument(
        "--cache", default=DEFAULT_MODERN_CACHE,
        help="Path to modern cache directory",
    )
    parser.add_argument(
        "--output", default=str(Path(__file__).resolve().parents[1] / "data/sprites/gui"),
        help="Output directory for PNGs",
    )
    parser.add_argument(
        "--list-all", action="store_true",
        help="List all sprite group IDs in index 8 and exit",
    )
    args = parser.parse_args()

    reader = ModernCacheReader(args.cache)

    if args.list_all:
        manifest = reader.read_index_manifest(8)
        print(f"index 8 has {len(manifest.group_ids)} sprite groups")
        print(f"  range: {min(manifest.group_ids)} - {max(manifest.group_ids)}")
        return

    out_dir = Path(args.output)
    out_dir.mkdir(parents=True, exist_ok=True)

    # collect all sprite IDs to export
    all_ids: set[int] = set()
    for ids in SPRITE_IDS.values():
        all_ids.update(ids)
    all_ids.update(RUNEC_UI_ALIASES.keys())

    # check which IDs exist in the cache
    manifest = reader.read_index_manifest(8)
    available = set(manifest.group_ids)

    exported = 0
    failed = 0

    for sprite_id in sorted(all_ids):
        if sprite_id not in available:
            print(f"  sprite {sprite_id}: NOT in cache index 8")
            failed += 1
            continue

        data = reader.read_container(8, sprite_id)
        if data is None:
            print(f"  sprite {sprite_id}: failed to read container")
            failed += 1
            continue

        try:
            frames = decode_sprites(sprite_id, data)
        except (IndexError, struct.error, ValueError) as e:
            print(f"  sprite {sprite_id}: decode error: {e}", file=sys.stderr)
            failed += 1
            continue

        if not frames:
            print(f"  sprite {sprite_id}: no frames decoded")
            failed += 1
            continue

        for frame in frames:
            for name in sprite_output_names(sprite_id):
                if len(frames) > 1:
                    filename = f"{name}_{frame.frame}.png"
                else:
                    filename = f"{name}.png"
                path = out_dir / filename
                save_sprite_png(frame, path, sprite_id)
                exported += 1

        if len(frames) == 1:
            f = frames[0]
            print(f"  sprite {sprite_id} ({SPRITE_NAMES.get(sprite_id, '?')}): "
                  f"{f.width}x{f.height}")
        else:
            print(f"  sprite {sprite_id} ({SPRITE_NAMES.get(sprite_id, '?')}): "
                  f"{len(frames)} frames")

    print(f"\nexported {exported} sprite frames, {failed} failed")
    print(f"output: {out_dir}/")


if __name__ == "__main__":
    main()
