/**
 * @file osrs_pvp_items.h
 * @brief Item database with OSRS item IDs and equipment stats
 *
 * Provides a static database of LMS items with real OSRS item IDs.
 * Each item has complete equipment stats sourced from OSRS wiki/game data.
 * Indices 0-16 are basic LMS loadout; 17-51 are loot progression items;
 * 52-59 are barrows armor and opal dragon bolts.
 */

#ifndef OSRS_PVP_ITEMS_H
#define OSRS_PVP_ITEMS_H

#include <stdint.h>
#include <stddef.h>

// ============================================================================
// EQUIPMENT SLOTS
// ============================================================================

typedef enum {
    SLOT_HEAD = 0,
    SLOT_CAPE = 1,
    SLOT_NECK = 2,
    SLOT_WEAPON = 3,
    SLOT_BODY = 4,
    SLOT_SHIELD = 5,
    SLOT_LEGS = 6,
    SLOT_HANDS = 7,
    SLOT_FEET = 8,
    SLOT_RING = 9,
    SLOT_AMMO = 10,
    NUM_EQUIPMENT_SLOTS = 11
} EquipmentSlot;

// ============================================================================
// ITEM DATABASE INDICES
// ============================================================================

// Database indices (0-51), NOT OSRS item IDs.
// Use ITEM_DATABASE[index].item_id to get real OSRS ID.
typedef enum {
    // === Basic LMS loadout (indices 0-16) ===
    ITEM_HELM_NEITIZNOT = 0,
    ITEM_GOD_CAPE = 1,
    ITEM_GLORY = 2,
    ITEM_BLACK_DHIDE_BODY = 3,
    ITEM_MYSTIC_TOP = 4,
    ITEM_RUNE_PLATELEGS = 5,
    ITEM_MYSTIC_BOTTOM = 6,
    ITEM_WHIP = 7,
    ITEM_RUNE_CROSSBOW = 8,
    ITEM_AHRIM_STAFF = 9,
    ITEM_DRAGON_DAGGER = 10,
    ITEM_DRAGON_DEFENDER = 11,
    ITEM_SPIRIT_SHIELD = 12,
    ITEM_BARROWS_GLOVES = 13,
    ITEM_CLIMBING_BOOTS = 14,
    ITEM_BERSERKER_RING = 15,
    ITEM_DIAMOND_BOLTS_E = 16,

    // === Weapons (indices 17-35) ===
    ITEM_GHRAZI_RAPIER = 17,
    ITEM_INQUISITORS_MACE = 18,
    ITEM_STAFF_OF_DEAD = 19,
    ITEM_KODAI_WAND = 20,
    ITEM_VOLATILE_STAFF = 21,
    ITEM_ZURIELS_STAFF = 22,
    ITEM_ARMADYL_CROSSBOW = 23,
    ITEM_ZARYTE_CROSSBOW = 24,
    ITEM_DRAGON_CLAWS = 25,
    ITEM_AGS = 26,
    ITEM_ANCIENT_GS = 27,
    ITEM_GRANITE_MAUL = 28,
    ITEM_ELDER_MAUL = 29,
    ITEM_DARK_BOW = 30,
    ITEM_HEAVY_BALLISTA = 31,
    ITEM_VESTAS = 32,
    ITEM_VOIDWAKER = 33,
    ITEM_STATIUS_WARHAMMER = 34,
    ITEM_MORRIGANS_JAVELIN = 35,

    // === Armor and accessories (indices 36-51) ===
    ITEM_ANCESTRAL_HAT = 36,
    ITEM_ANCESTRAL_TOP = 37,
    ITEM_ANCESTRAL_BOTTOM = 38,
    ITEM_AHRIMS_ROBETOP = 39,
    ITEM_AHRIMS_ROBESKIRT = 40,
    ITEM_KARILS_TOP = 41,
    ITEM_BANDOS_TASSETS = 42,
    ITEM_BLESSED_SPIRIT_SHIELD = 43,
    ITEM_FURY = 44,
    ITEM_OCCULT_NECKLACE = 45,
    ITEM_INFERNAL_CAPE = 46,
    ITEM_ETERNAL_BOOTS = 47,
    ITEM_SEERS_RING_I = 48,
    ITEM_LIGHTBEARER = 49,
    ITEM_MAGES_BOOK = 50,
    ITEM_DRAGON_ARROWS = 51,

    // === Barrows armor + opal bolts (indices 52-59) ===
    ITEM_TORAGS_PLATELEGS = 52,
    ITEM_DHAROKS_PLATELEGS = 53,
    ITEM_VERACS_PLATESKIRT = 54,
    ITEM_TORAGS_HELM = 55,
    ITEM_DHAROKS_HELM = 56,
    ITEM_VERACS_HELM = 57,
    ITEM_GUTHANS_HELM = 58,
    ITEM_OPAL_DRAGON_BOLTS = 59,

    // === Zulrah encounter items (indices 60-94) ===
    // tier 2 (BIS) mage
    ITEM_IMBUED_SARA_CAPE = 60,
    ITEM_EYE_OF_AYAK = 61,
    ITEM_ELIDINIS_WARD_F = 62,
    ITEM_CONFLICTION_GAUNTLETS = 63,
    ITEM_AVERNIC_TREADS = 64,
    ITEM_RING_OF_SUFFERING_RI = 65,
    // tier 2 (BIS) range switches
    ITEM_TWISTED_BOW = 66,
    ITEM_MASORI_MASK_F = 67,
    ITEM_MASORI_BODY_F = 68,
    ITEM_MASORI_CHAPS_F = 69,
    ITEM_NECKLACE_OF_ANGUISH = 70,
    ITEM_DIZANAS_QUIVER = 71,
    ITEM_ZARYTE_VAMBRACES = 72,
    // tier 2 spec
    ITEM_TOXIC_BLOWPIPE = 73,
    // tier 1 (mid) mage
    ITEM_AHRIMS_HOOD = 74,
    ITEM_TORMENTED_BRACELET = 75,
    ITEM_SANGUINESTI_STAFF = 76,
    ITEM_INFINITY_BOOTS = 77,
    ITEM_GOD_BLESSING = 78,
    ITEM_RING_OF_RECOIL = 79,
    // tier 1 range switches
    ITEM_CRYSTAL_HELM = 80,
    ITEM_AVAS_ASSEMBLER = 81,
    ITEM_CRYSTAL_BODY = 82,
    ITEM_CRYSTAL_LEGS = 83,
    ITEM_BOW_OF_FAERDHINEN = 84,
    ITEM_BLESSED_DHIDE_BOOTS = 85,
    // tier 0 (budget) mage
    ITEM_MYSTIC_HAT = 86,
    ITEM_TRIDENT_OF_SWAMP = 87,
    ITEM_BOOK_OF_DARKNESS = 88,
    ITEM_AMETHYST_ARROW = 89,
    ITEM_MYSTIC_BOOTS = 90,
    // tier 0 range switches
    ITEM_BLESSED_COIF = 91,
    ITEM_BLACK_DHIDE_CHAPS = 92,
    ITEM_MAGIC_SHORTBOW_I = 93,
    ITEM_AVAS_ACCUMULATOR = 94,

    NUM_ITEMS = 95,
    ITEM_NONE = 255
} ItemIndex;

// ============================================================================
// ITEM STRUCT
// ============================================================================

typedef struct {
    uint16_t item_id;           // Real OSRS item ID
    char name[32];              // Human-readable name
    uint8_t slot;               // Equipment slot (EquipmentSlot enum)
    uint8_t attack_speed;       // Weapon attack speed (ticks)
    uint8_t attack_range;       // Weapon attack range (tiles)
    int16_t attack_stab;
    int16_t attack_slash;
    int16_t attack_crush;
    int16_t attack_magic;
    int16_t attack_ranged;
    int16_t defence_stab;
    int16_t defence_slash;
    int16_t defence_crush;
    int16_t defence_magic;
    int16_t defence_ranged;
    int16_t melee_strength;
    int16_t ranged_strength;
    int16_t magic_damage;       // Magic damage % bonus
    int16_t prayer;
} Item;

// ============================================================================
// STATIC ITEM DATABASE
// ============================================================================

// Stats sourced from OSRS wiki. int16_t for items with bonuses > 127.
static const Item ITEM_DATABASE[NUM_ITEMS] = {
    // === Basic LMS loadout (0-16) ===

    [ITEM_HELM_NEITIZNOT] = {
        .item_id = 10828, .name = "Helm of Neitiznot", .slot = SLOT_HEAD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 31, .defence_slash = 29, .defence_crush = 34,
        .defence_magic = 3, .defence_ranged = 30,
        .melee_strength = 3, .ranged_strength = 0, .magic_damage = 0, .prayer = 3
    },
    [ITEM_GOD_CAPE] = {
        .item_id = 21795, .name = "Imbued god cape", .slot = SLOT_CAPE,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 15, .attack_ranged = 0,
        .defence_stab = 3, .defence_slash = 3, .defence_crush = 3,
        .defence_magic = 15, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 2, .prayer = 0
    },
    [ITEM_GLORY] = {
        .item_id = 1712, .name = "Amulet of glory", .slot = SLOT_NECK,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 10, .attack_slash = 10, .attack_crush = 10,
        .attack_magic = 10, .attack_ranged = 10,
        .defence_stab = 3, .defence_slash = 3, .defence_crush = 3,
        .defence_magic = 3, .defence_ranged = 3,
        .melee_strength = 6, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_BLACK_DHIDE_BODY] = {
        .item_id = 2503, .name = "Black d'hide body", .slot = SLOT_BODY,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -15, .attack_ranged = 30,
        .defence_stab = 55, .defence_slash = 47, .defence_crush = 62,
        .defence_magic = 50, .defence_ranged = 57,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_MYSTIC_TOP] = {
        .item_id = 4091, .name = "Mystic robe top", .slot = SLOT_BODY,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 20, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 20, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_RUNE_PLATELEGS] = {
        .item_id = 1079, .name = "Rune platelegs", .slot = SLOT_LEGS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -21, .attack_ranged = -11,
        .defence_stab = 51, .defence_slash = 49, .defence_crush = 47,
        .defence_magic = -4, .defence_ranged = 49,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_MYSTIC_BOTTOM] = {
        .item_id = 4093, .name = "Mystic robe bottom", .slot = SLOT_LEGS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 15, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 15, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_WHIP] = {
        .item_id = 4151, .name = "Abyssal whip", .slot = SLOT_WEAPON,
        .attack_speed = 4, .attack_range = 1,
        .attack_stab = 0, .attack_slash = 82, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 82, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_RUNE_CROSSBOW] = {
        .item_id = 9185, .name = "Rune crossbow", .slot = SLOT_WEAPON,
        .attack_speed = 5, .attack_range = 7,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 90,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_AHRIM_STAFF] = {
        .item_id = 4710, .name = "Ahrim's staff", .slot = SLOT_WEAPON,
        .attack_speed = 5, .attack_range = 10,
        .attack_stab = 12, .attack_slash = -1, .attack_crush = 65,
        .attack_magic = 15, .attack_ranged = 0,
        .defence_stab = 3, .defence_slash = 5, .defence_crush = 2,
        .defence_magic = 15, .defence_ranged = 0,
        .melee_strength = 68, .ranged_strength = 0, .magic_damage = 5, .prayer = 0
    },
    [ITEM_DRAGON_DAGGER] = {
        .item_id = 5698, .name = "Dragon dagger", .slot = SLOT_WEAPON,
        .attack_speed = 4, .attack_range = 1,
        .attack_stab = 40, .attack_slash = 25, .attack_crush = -4,
        .attack_magic = 1, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 1, .defence_ranged = 0,
        .melee_strength = 40, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_DRAGON_DEFENDER] = {
        .item_id = 12954, .name = "Dragon defender", .slot = SLOT_SHIELD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 25, .attack_slash = 24, .attack_crush = 23,
        .attack_magic = -3, .attack_ranged = -2,
        .defence_stab = 25, .defence_slash = 24, .defence_crush = 23,
        .defence_magic = -3, .defence_ranged = 24,
        .melee_strength = 6, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_SPIRIT_SHIELD] = {
        .item_id = 12829, .name = "Spirit shield", .slot = SLOT_SHIELD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 39, .defence_slash = 41, .defence_crush = 50,
        .defence_magic = 1, .defence_ranged = 45,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 1
    },
    [ITEM_BARROWS_GLOVES] = {
        .item_id = 7462, .name = "Barrows gloves", .slot = SLOT_HANDS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 12, .attack_slash = 12, .attack_crush = 12,
        .attack_magic = 6, .attack_ranged = 12,
        .defence_stab = 12, .defence_slash = 12, .defence_crush = 12,
        .defence_magic = 6, .defence_ranged = 12,
        .melee_strength = 12, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_CLIMBING_BOOTS] = {
        .item_id = 3105, .name = "Climbing boots", .slot = SLOT_FEET,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 2, .defence_crush = 2,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 2, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_BERSERKER_RING] = {
        .item_id = 6737, .name = "Berserker ring", .slot = SLOT_RING,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 4,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 4, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_DIAMOND_BOLTS_E] = {
        .item_id = 9243, .name = "Diamond bolts (e)", .slot = SLOT_AMMO,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 105, .magic_damage = 0, .prayer = 0
    },

    // === Weapons (17-35) ===

    [ITEM_GHRAZI_RAPIER] = {
        .item_id = 22324, .name = "Ghrazi rapier", .slot = SLOT_WEAPON,
        .attack_speed = 4, .attack_range = 1,
        .attack_stab = 94, .attack_slash = 55, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 89, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_INQUISITORS_MACE] = {
        .item_id = 24417, .name = "Inquisitor's mace", .slot = SLOT_WEAPON,
        .attack_speed = 4, .attack_range = 1,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 95,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 89, .ranged_strength = 0, .magic_damage = 0, .prayer = 2
    },
    [ITEM_STAFF_OF_DEAD] = {
        .item_id = 11791, .name = "Staff of the dead", .slot = SLOT_WEAPON,
        .attack_speed = 5, .attack_range = 10,
        .attack_stab = 55, .attack_slash = -1, .attack_crush = 70,
        .attack_magic = 15, .attack_ranged = 0,
        .defence_stab = 3, .defence_slash = 1, .defence_crush = 0,
        .defence_magic = 15, .defence_ranged = 0,
        .melee_strength = 72, .ranged_strength = 0, .magic_damage = 15, .prayer = 0
    },
    [ITEM_KODAI_WAND] = {
        .item_id = 21006, .name = "Kodai wand", .slot = SLOT_WEAPON,
        .attack_speed = 5, .attack_range = 10,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 28, .attack_ranged = 0,
        .defence_stab = 3, .defence_slash = 3, .defence_crush = 3,
        .defence_magic = 20, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 15, .prayer = 0
    },
    [ITEM_VOLATILE_STAFF] = {
        .item_id = 24424, .name = "Volatile nightmare staff", .slot = SLOT_WEAPON,
        .attack_speed = 5, .attack_range = 10,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 16, .attack_ranged = 0,
        .defence_stab = 5, .defence_slash = 5, .defence_crush = 5,
        .defence_magic = 15, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 15, .prayer = 0
    },
    [ITEM_ZURIELS_STAFF] = {
        .item_id = 13867, .name = "Zuriel's staff", .slot = SLOT_WEAPON,
        .attack_speed = 5, .attack_range = 10,
        .attack_stab = 13, .attack_slash = -1, .attack_crush = 65,
        .attack_magic = 18, .attack_ranged = 0,
        .defence_stab = 5, .defence_slash = 7, .defence_crush = 4,
        .defence_magic = 18, .defence_ranged = 0,
        .melee_strength = 72, .ranged_strength = 0, .magic_damage = 10, .prayer = 0
    },
    [ITEM_ARMADYL_CROSSBOW] = {
        .item_id = 11785, .name = "Armadyl crossbow", .slot = SLOT_WEAPON,
        .attack_speed = 5, .attack_range = 7,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 100,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_ZARYTE_CROSSBOW] = {
        .item_id = 26374, .name = "Zaryte crossbow", .slot = SLOT_WEAPON,
        .attack_speed = 5, .attack_range = 7,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 110,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_DRAGON_CLAWS] = {
        .item_id = 13652, .name = "Dragon claws", .slot = SLOT_WEAPON,
        .attack_speed = 4, .attack_range = 1,
        .attack_stab = 41, .attack_slash = 57, .attack_crush = -4,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 13, .defence_slash = 26, .defence_crush = -1,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 56, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_AGS] = {
        .item_id = 11802, .name = "Armadyl godsword", .slot = SLOT_WEAPON,
        .attack_speed = 6, .attack_range = 1,
        .attack_stab = 0, .attack_slash = 132, .attack_crush = 80,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 132, .ranged_strength = 0, .magic_damage = 0, .prayer = 8
    },
    [ITEM_ANCIENT_GS] = {
        .item_id = 25730, .name = "Ancient godsword", .slot = SLOT_WEAPON,
        .attack_speed = 6, .attack_range = 1,
        .attack_stab = 0, .attack_slash = 132, .attack_crush = 80,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 132, .ranged_strength = 0, .magic_damage = 0, .prayer = 8
    },
    [ITEM_GRANITE_MAUL] = {
        .item_id = 4153, .name = "Granite maul", .slot = SLOT_WEAPON,
        .attack_speed = 7, .attack_range = 1,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 81,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 79, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_ELDER_MAUL] = {
        .item_id = 21003, .name = "Elder maul", .slot = SLOT_WEAPON,
        .attack_speed = 6, .attack_range = 1,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 135,
        .attack_magic = -4, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 147, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_DARK_BOW] = {
        .item_id = 11235, .name = "Dark bow", .slot = SLOT_WEAPON,
        .attack_speed = 9, .attack_range = 10,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 95,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_HEAVY_BALLISTA] = {
        .item_id = 19481, .name = "Heavy ballista", .slot = SLOT_WEAPON,
        .attack_speed = 7, .attack_range = 10,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 125,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_VESTAS] = {
        .item_id = 22613, .name = "Vesta's longsword", .slot = SLOT_WEAPON,
        .attack_speed = 5, .attack_range = 1,
        .attack_stab = 106, .attack_slash = 121, .attack_crush = -2,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 1, .defence_slash = 4, .defence_crush = 3,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 118, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_VOIDWAKER] = {
        .item_id = 27690, .name = "Voidwaker", .slot = SLOT_WEAPON,
        .attack_speed = 4, .attack_range = 1,
        .attack_stab = 70, .attack_slash = 80, .attack_crush = -2,
        .attack_magic = 5, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 1, .defence_crush = 0,
        .defence_magic = 2, .defence_ranged = 0,
        .melee_strength = 80, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_STATIUS_WARHAMMER] = {
        .item_id = 22622, .name = "Statius's warhammer", .slot = SLOT_WEAPON,
        .attack_speed = 5, .attack_range = 1,
        .attack_stab = -4, .attack_slash = -4, .attack_crush = 123,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 114, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_MORRIGANS_JAVELIN] = {
        .item_id = 22636, .name = "Morrigan's javelin", .slot = SLOT_WEAPON,
        .attack_speed = 5, .attack_range = 5,  // rapid style (6 on accurate/longrange)
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 105,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 145, .magic_damage = 0, .prayer = 0
    },

    // === Armor and accessories (36-51) ===

    [ITEM_ANCESTRAL_HAT] = {
        .item_id = 21018, .name = "Ancestral hat", .slot = SLOT_HEAD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 8, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 8, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 2, .prayer = 0
    },
    [ITEM_ANCESTRAL_TOP] = {
        .item_id = 21021, .name = "Ancestral robe top", .slot = SLOT_BODY,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 35, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 35, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 2, .prayer = 0
    },
    [ITEM_ANCESTRAL_BOTTOM] = {
        .item_id = 21024, .name = "Ancestral robe bottom", .slot = SLOT_LEGS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 26, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 26, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 2, .prayer = 0
    },
    [ITEM_AHRIMS_ROBETOP] = {
        .item_id = 4712, .name = "Ahrim's robetop", .slot = SLOT_BODY,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 30, .attack_ranged = 0,
        .defence_stab = 52, .defence_slash = 37, .defence_crush = 63,
        .defence_magic = 30, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_AHRIMS_ROBESKIRT] = {
        .item_id = 4714, .name = "Ahrim's robeskirt", .slot = SLOT_LEGS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 22, .attack_ranged = 0,
        .defence_stab = 33, .defence_slash = 30, .defence_crush = 36,
        .defence_magic = 22, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_KARILS_TOP] = {
        .item_id = 4736, .name = "Karil's leathertop", .slot = SLOT_BODY,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -15, .attack_ranged = 30,
        .defence_stab = 57, .defence_slash = 48, .defence_crush = 63,
        .defence_magic = 65, .defence_ranged = 57,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_BANDOS_TASSETS] = {
        .item_id = 11834, .name = "Bandos tassets", .slot = SLOT_LEGS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -21, .attack_ranged = -7,
        .defence_stab = 71, .defence_slash = 63, .defence_crush = 66,
        .defence_magic = -4, .defence_ranged = 93,
        .melee_strength = 2, .ranged_strength = 0, .magic_damage = 0, .prayer = 1
    },
    [ITEM_BLESSED_SPIRIT_SHIELD] = {
        .item_id = 12831, .name = "Blessed spirit shield", .slot = SLOT_SHIELD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 53, .defence_slash = 55, .defence_crush = 73,
        .defence_magic = 2, .defence_ranged = 52,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 3
    },
    [ITEM_FURY] = {
        .item_id = 6585, .name = "Amulet of fury", .slot = SLOT_NECK,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 10, .attack_slash = 10, .attack_crush = 10,
        .attack_magic = 10, .attack_ranged = 10,
        .defence_stab = 15, .defence_slash = 15, .defence_crush = 15,
        .defence_magic = 15, .defence_ranged = 15,
        .melee_strength = 8, .ranged_strength = 0, .magic_damage = 0, .prayer = 5
    },
    [ITEM_OCCULT_NECKLACE] = {
        .item_id = 12002, .name = "Occult necklace", .slot = SLOT_NECK,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 12, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 10, .prayer = 0
    },
    [ITEM_INFERNAL_CAPE] = {
        .item_id = 21295, .name = "Infernal cape", .slot = SLOT_CAPE,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 4, .attack_slash = 4, .attack_crush = 4,
        .attack_magic = 1, .attack_ranged = 1,
        .defence_stab = 12, .defence_slash = 12, .defence_crush = 12,
        .defence_magic = 1, .defence_ranged = 12,
        .melee_strength = 8, .ranged_strength = 0, .magic_damage = 0, .prayer = 2
    },
    [ITEM_ETERNAL_BOOTS] = {
        .item_id = 13235, .name = "Eternal boots", .slot = SLOT_FEET,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 8, .attack_ranged = 0,
        .defence_stab = 5, .defence_slash = 5, .defence_crush = 5,
        .defence_magic = 8, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_SEERS_RING_I] = {
        .item_id = 11770, .name = "Seers ring (i)", .slot = SLOT_RING,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 12, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 12, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_LIGHTBEARER] = {
        .item_id = 25975, .name = "Lightbearer", .slot = SLOT_RING,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_MAGES_BOOK] = {
        .item_id = 6889, .name = "Mage's book", .slot = SLOT_SHIELD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 15, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 15, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 2, .prayer = 0
    },
    [ITEM_DRAGON_ARROWS] = {
        .item_id = 11212, .name = "Dragon arrows", .slot = SLOT_AMMO,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 60, .magic_damage = 0, .prayer = 0
    },

    // === Barrows armor + opal bolts (52-59) ===

    [ITEM_TORAGS_PLATELEGS] = {
        .item_id = 4751, .name = "Torag's platelegs", .slot = SLOT_LEGS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -21, .attack_ranged = -11,
        .defence_stab = 85, .defence_slash = 82, .defence_crush = 83,
        .defence_magic = -4, .defence_ranged = 92,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_DHAROKS_PLATELEGS] = {
        .item_id = 4722, .name = "Dharok's platelegs", .slot = SLOT_LEGS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -21, .attack_ranged = -11,
        .defence_stab = 85, .defence_slash = 82, .defence_crush = 83,
        .defence_magic = -4, .defence_ranged = 92,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_VERACS_PLATESKIRT] = {
        .item_id = 4759, .name = "Verac's plateskirt", .slot = SLOT_LEGS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -21, .attack_ranged = -11,
        .defence_stab = 85, .defence_slash = 82, .defence_crush = 83,
        .defence_magic = 0, .defence_ranged = 84,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 4
    },
    [ITEM_TORAGS_HELM] = {
        .item_id = 4745, .name = "Torag's helm", .slot = SLOT_HEAD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -6, .attack_ranged = -2,
        .defence_stab = 55, .defence_slash = 58, .defence_crush = 54,
        .defence_magic = -1, .defence_ranged = 62,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_DHAROKS_HELM] = {
        .item_id = 4716, .name = "Dharok's helm", .slot = SLOT_HEAD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -3, .attack_ranged = -1,
        .defence_stab = 45, .defence_slash = 48, .defence_crush = 44,
        .defence_magic = -1, .defence_ranged = 51,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_VERACS_HELM] = {
        .item_id = 4753, .name = "Verac's helm", .slot = SLOT_HEAD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -6, .attack_ranged = -2,
        .defence_stab = 55, .defence_slash = 58, .defence_crush = 54,
        .defence_magic = 0, .defence_ranged = 56,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 3
    },
    [ITEM_GUTHANS_HELM] = {
        .item_id = 4724, .name = "Guthan's helm", .slot = SLOT_HEAD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -6, .attack_ranged = -2,
        .defence_stab = 55, .defence_slash = 58, .defence_crush = 54,
        .defence_magic = -1, .defence_ranged = 62,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_OPAL_DRAGON_BOLTS] = {
        .item_id = 21932, .name = "Opal dragon bolts (e)", .slot = SLOT_AMMO,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 122, .magic_damage = 0, .prayer = 0
    },

    // === Zulrah encounter items (60-94) ===

    // --- tier 2 (BIS) mage ---
    [ITEM_IMBUED_SARA_CAPE] = {
        .item_id = 21791, .name = "Imbued saradomin cape", .slot = SLOT_CAPE,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 15, .attack_ranged = 0,
        .defence_stab = 3, .defence_slash = 3, .defence_crush = 3,
        .defence_magic = 15, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 2, .prayer = 0
    },
    [ITEM_EYE_OF_AYAK] = {
        .item_id = 31113, .name = "Eye of ayak", .slot = SLOT_WEAPON,
        .attack_speed = 3, .attack_range = 6,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 30, .attack_ranged = 0,
        .defence_stab = 1, .defence_slash = 5, .defence_crush = 5,
        .defence_magic = 10, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 2
    },
    [ITEM_ELIDINIS_WARD_F] = {
        .item_id = 27251, .name = "Elidinis' ward (f)", .slot = SLOT_SHIELD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 25, .attack_ranged = 0,
        .defence_stab = 53, .defence_slash = 55, .defence_crush = 73,
        .defence_magic = 2, .defence_ranged = 52,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 5, .prayer = 4
    },
    [ITEM_CONFLICTION_GAUNTLETS] = {
        .item_id = 31106, .name = "Confliction gauntlets", .slot = SLOT_HANDS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 20, .attack_ranged = -4,
        .defence_stab = 15, .defence_slash = 18, .defence_crush = 7,
        .defence_magic = 5, .defence_ranged = 5,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 7, .prayer = 2
    },
    [ITEM_AVERNIC_TREADS] = {
        .item_id = 31097, .name = "Avernic treads (max)", .slot = SLOT_FEET,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 5, .attack_slash = 5, .attack_crush = 5,
        .attack_magic = 11, .attack_ranged = 15,
        .defence_stab = 21, .defence_slash = 25, .defence_crush = 25,
        .defence_magic = 10, .defence_ranged = 10,
        .melee_strength = 6, .ranged_strength = 3, .magic_damage = 2, .prayer = 0
    },
    [ITEM_RING_OF_SUFFERING_RI] = {
        .item_id = 20657, .name = "Ring of suffering (ri)", .slot = SLOT_RING,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 20, .defence_slash = 20, .defence_crush = 20,
        .defence_magic = 20, .defence_ranged = 20,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 4
    },

    // --- tier 2 (BIS) range switches ---
    [ITEM_TWISTED_BOW] = {
        .item_id = 20997, .name = "Twisted bow", .slot = SLOT_WEAPON,
        .attack_speed = 5, .attack_range = 10,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 70,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 20, .magic_damage = 0, .prayer = 0
    },
    [ITEM_MASORI_MASK_F] = {
        .item_id = 27235, .name = "Masori mask (f)", .slot = SLOT_HEAD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -1, .attack_ranged = 12,
        .defence_stab = 8, .defence_slash = 10, .defence_crush = 12,
        .defence_magic = 12, .defence_ranged = 9,
        .melee_strength = 0, .ranged_strength = 2, .magic_damage = 0, .prayer = 1
    },
    [ITEM_MASORI_BODY_F] = {
        .item_id = 27238, .name = "Masori body (f)", .slot = SLOT_BODY,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -4, .attack_ranged = 43,
        .defence_stab = 59, .defence_slash = 52, .defence_crush = 64,
        .defence_magic = 74, .defence_ranged = 60,
        .melee_strength = 0, .ranged_strength = 4, .magic_damage = 0, .prayer = 1
    },
    [ITEM_MASORI_CHAPS_F] = {
        .item_id = 27241, .name = "Masori chaps (f)", .slot = SLOT_LEGS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -2, .attack_ranged = 27,
        .defence_stab = 35, .defence_slash = 30, .defence_crush = 39,
        .defence_magic = 46, .defence_ranged = 37,
        .melee_strength = 0, .ranged_strength = 2, .magic_damage = 0, .prayer = 1
    },
    [ITEM_NECKLACE_OF_ANGUISH] = {
        .item_id = 19547, .name = "Necklace of anguish", .slot = SLOT_NECK,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 15,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 5, .magic_damage = 0, .prayer = 2
    },
    [ITEM_DIZANAS_QUIVER] = {
        .item_id = 28947, .name = "Dizana's quiver", .slot = SLOT_CAPE,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 18,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 3, .magic_damage = 0, .prayer = 0
    },
    [ITEM_ZARYTE_VAMBRACES] = {
        .item_id = 26235, .name = "Zaryte vambraces", .slot = SLOT_HANDS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = -8, .attack_slash = -8, .attack_crush = -8,
        .attack_magic = 0, .attack_ranged = 18,
        .defence_stab = 8, .defence_slash = 8, .defence_crush = 8,
        .defence_magic = 5, .defence_ranged = 8,
        .melee_strength = 0, .ranged_strength = 2, .magic_damage = 0, .prayer = 1
    },

    // --- tier 2 spec ---
    [ITEM_TOXIC_BLOWPIPE] = {
        .item_id = 12926, .name = "Toxic blowpipe", .slot = SLOT_WEAPON,
        .attack_speed = 3, .attack_range = 5,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 30,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 20, .magic_damage = 0, .prayer = 0
    },

    // --- tier 1 (mid) mage ---
    [ITEM_AHRIMS_HOOD] = {
        .item_id = 4708, .name = "Ahrim's hood", .slot = SLOT_HEAD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 6, .attack_ranged = -2,
        .defence_stab = 15, .defence_slash = 13, .defence_crush = 16,
        .defence_magic = 6, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 1, .prayer = 0
    },
    [ITEM_TORMENTED_BRACELET] = {
        .item_id = 19544, .name = "Tormented bracelet", .slot = SLOT_HANDS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 10, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 5, .prayer = 2
    },
    [ITEM_SANGUINESTI_STAFF] = {
        .item_id = 22481, .name = "Sanguinesti staff", .slot = SLOT_WEAPON,
        .attack_speed = 4, .attack_range = 7,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 25, .attack_ranged = -4,
        .defence_stab = 2, .defence_slash = 3, .defence_crush = 1,
        .defence_magic = 15, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_INFINITY_BOOTS] = {
        .item_id = 6920, .name = "Infinity boots", .slot = SLOT_FEET,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 5, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 5, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_GOD_BLESSING] = {
        .item_id = 20220, .name = "Holy blessing", .slot = SLOT_AMMO,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 1
    },
    [ITEM_RING_OF_RECOIL] = {
        .item_id = 2550, .name = "Ring of recoil", .slot = SLOT_RING,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },

    // --- tier 1 range switches ---
    [ITEM_CRYSTAL_HELM] = {
        .item_id = 23971, .name = "Crystal helm", .slot = SLOT_HEAD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -10, .attack_ranged = 9,
        .defence_stab = 12, .defence_slash = 8, .defence_crush = 14,
        .defence_magic = 10, .defence_ranged = 18,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 2
    },
    [ITEM_AVAS_ASSEMBLER] = {
        .item_id = 22109, .name = "Ava's assembler", .slot = SLOT_CAPE,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 8,
        .defence_stab = 1, .defence_slash = 1, .defence_crush = 1,
        .defence_magic = 8, .defence_ranged = 2,
        .melee_strength = 0, .ranged_strength = 2, .magic_damage = 0, .prayer = 0
    },
    [ITEM_CRYSTAL_BODY] = {
        .item_id = 23975, .name = "Crystal body", .slot = SLOT_BODY,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -18, .attack_ranged = 31,
        .defence_stab = 46, .defence_slash = 38, .defence_crush = 48,
        .defence_magic = 44, .defence_ranged = 68,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 3
    },
    [ITEM_CRYSTAL_LEGS] = {
        .item_id = 23979, .name = "Crystal legs", .slot = SLOT_LEGS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -12, .attack_ranged = 18,
        .defence_stab = 26, .defence_slash = 21, .defence_crush = 30,
        .defence_magic = 34, .defence_ranged = 38,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 2
    },
    [ITEM_BOW_OF_FAERDHINEN] = {
        .item_id = 25865, .name = "Bow of faerdhinen (c)", .slot = SLOT_WEAPON,
        .attack_speed = 4, .attack_range = 10,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 128,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 106, .magic_damage = 0, .prayer = 0
    },
    [ITEM_BLESSED_DHIDE_BOOTS] = {
        .item_id = 19921, .name = "Blessed d'hide boots", .slot = SLOT_FEET,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -10, .attack_ranged = 7,
        .defence_stab = 4, .defence_slash = 4, .defence_crush = 4,
        .defence_magic = 4, .defence_ranged = 4,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 1
    },

    // --- tier 0 (budget) mage ---
    [ITEM_MYSTIC_HAT] = {
        .item_id = 4089, .name = "Mystic hat", .slot = SLOT_HEAD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 4, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 4, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_TRIDENT_OF_SWAMP] = {
        .item_id = 12899, .name = "Trident of the swamp", .slot = SLOT_WEAPON,
        .attack_speed = 4, .attack_range = 7,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 25, .attack_ranged = 0,
        .defence_stab = 2, .defence_slash = 3, .defence_crush = 1,
        .defence_magic = 15, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_BOOK_OF_DARKNESS] = {
        .item_id = 12612, .name = "Book of darkness", .slot = SLOT_SHIELD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 10, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 5
    },
    [ITEM_AMETHYST_ARROW] = {
        .item_id = 21326, .name = "Amethyst arrow", .slot = SLOT_AMMO,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 55, .magic_damage = 0, .prayer = 0
    },
    [ITEM_MYSTIC_BOOTS] = {
        .item_id = 4097, .name = "Mystic boots", .slot = SLOT_FEET,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 3, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 3, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },

    // --- tier 0 range switches ---
    [ITEM_BLESSED_COIF] = {
        .item_id = 10382, .name = "Blessed coif", .slot = SLOT_HEAD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -1, .attack_ranged = 7,
        .defence_stab = 4, .defence_slash = 7, .defence_crush = 10,
        .defence_magic = 4, .defence_ranged = 8,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 1
    },
    [ITEM_BLACK_DHIDE_CHAPS] = {
        .item_id = 2497, .name = "Black d'hide chaps", .slot = SLOT_LEGS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -10, .attack_ranged = 17,
        .defence_stab = 18, .defence_slash = 20, .defence_crush = 26,
        .defence_magic = 23, .defence_ranged = 26,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_MAGIC_SHORTBOW_I] = {
        .item_id = 12788, .name = "Magic shortbow (i)", .slot = SLOT_WEAPON,
        .attack_speed = 4, .attack_range = 7,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 75,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [ITEM_AVAS_ACCUMULATOR] = {
        .item_id = 10499, .name = "Ava's accumulator", .slot = SLOT_CAPE,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 4,
        .defence_stab = 0, .defence_slash = 1, .defence_crush = 0,
        .defence_magic = 4, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
};

// ============================================================================
// LOOKUP TABLES
// ============================================================================

// Max items per slot (inventory width for dynamic gear)
#define MAX_ITEMS_PER_SLOT_DB 10

// Items available per slot (for masking and inventory)
// 255 = end marker (slot has fewer than MAX_ITEMS_PER_SLOT_DB options)
static const uint8_t ITEMS_BY_SLOT[NUM_EQUIPMENT_SLOTS][MAX_ITEMS_PER_SLOT_DB] = {
    [SLOT_HEAD]   = {ITEM_HELM_NEITIZNOT, ITEM_ANCESTRAL_HAT,
                     ITEM_TORAGS_HELM, ITEM_DHAROKS_HELM, ITEM_VERACS_HELM, ITEM_GUTHANS_HELM,
                     ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE},
    [SLOT_CAPE]   = {ITEM_GOD_CAPE, ITEM_INFERNAL_CAPE,
                     ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE},
    [SLOT_NECK]   = {ITEM_GLORY, ITEM_FURY, ITEM_OCCULT_NECKLACE,
                     ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE},
    [SLOT_WEAPON] = {ITEM_WHIP, ITEM_RUNE_CROSSBOW, ITEM_AHRIM_STAFF, ITEM_DRAGON_DAGGER,
                     ITEM_GHRAZI_RAPIER, ITEM_INQUISITORS_MACE, ITEM_STAFF_OF_DEAD, ITEM_KODAI_WAND,
                     ITEM_VOLATILE_STAFF, ITEM_ZURIELS_STAFF},
    [SLOT_BODY]   = {ITEM_BLACK_DHIDE_BODY, ITEM_MYSTIC_TOP, ITEM_ANCESTRAL_TOP, ITEM_AHRIMS_ROBETOP,
                     ITEM_KARILS_TOP,
                     ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE},
    [SLOT_SHIELD] = {ITEM_DRAGON_DEFENDER, ITEM_SPIRIT_SHIELD, ITEM_BLESSED_SPIRIT_SHIELD, ITEM_MAGES_BOOK,
                     ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE},
    [SLOT_LEGS]   = {ITEM_RUNE_PLATELEGS, ITEM_MYSTIC_BOTTOM, ITEM_ANCESTRAL_BOTTOM, ITEM_AHRIMS_ROBESKIRT,
                     ITEM_BANDOS_TASSETS, ITEM_TORAGS_PLATELEGS, ITEM_DHAROKS_PLATELEGS, ITEM_VERACS_PLATESKIRT,
                     ITEM_NONE, ITEM_NONE},
    [SLOT_HANDS]  = {ITEM_BARROWS_GLOVES,
                     ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE},
    [SLOT_FEET]   = {ITEM_CLIMBING_BOOTS, ITEM_ETERNAL_BOOTS,
                     ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE},
    [SLOT_RING]   = {ITEM_BERSERKER_RING, ITEM_SEERS_RING_I, ITEM_LIGHTBEARER,
                     ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE},
    [SLOT_AMMO]   = {ITEM_DIAMOND_BOLTS_E, ITEM_DRAGON_ARROWS, ITEM_OPAL_DRAGON_BOLTS,
                     ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE},
};

// Number of items per slot in the static DB table above
static const uint8_t NUM_ITEMS_IN_SLOT[NUM_EQUIPMENT_SLOTS] = {
    [SLOT_HEAD]   = 6,   // neitiznot, ancestral hat, torag/dharok/verac/guthan helms
    [SLOT_CAPE]   = 2,   // god cape, infernal
    [SLOT_NECK]   = 3,   // glory, fury, occult
    [SLOT_WEAPON] = 10,  // whip, rcb, ahrim, dds, rapier, inq mace, sotd, kodai, volatile, zuriel
    [SLOT_BODY]   = 5,   // dhide, mystic, ancestral, ahrim, karil
    [SLOT_SHIELD] = 4,   // defender, spirit, blessed spirit, mages book
    [SLOT_LEGS]   = 8,   // rune, mystic, ancestral, ahrim, bandos, torag/dharok/verac legs
    [SLOT_HANDS]  = 1,   // barrows gloves
    [SLOT_FEET]   = 2,   // climbing boots, eternal boots
    [SLOT_RING]   = 3,   // berserker, seers (i), lightbearer
    [SLOT_AMMO]   = 3,   // diamond bolts (e), dragon arrows, opal dragon bolts (e)
};

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/** Get item from database by index. Returns NULL if invalid. */
static inline const Item* get_item(uint8_t item_index) {
    if (item_index >= NUM_ITEMS) return NULL;
    return &ITEM_DATABASE[item_index];
}

/** Check if item is a weapon. */
static inline int item_is_weapon(uint8_t item_index) {
    if (item_index >= NUM_ITEMS) return 0;
    return ITEM_DATABASE[item_index].slot == SLOT_WEAPON;
}

/** Check if item is a shield. */
static inline int item_is_shield(uint8_t item_index) {
    if (item_index >= NUM_ITEMS) return 0;
    return ITEM_DATABASE[item_index].slot == SLOT_SHIELD;
}

/** Get attack style for a weapon item (1=melee, 2=ranged, 3=magic). */
static inline int get_item_attack_style(uint8_t item_index) {
    switch (item_index) {
        // Melee weapons
        case ITEM_WHIP:
        case ITEM_DRAGON_DAGGER:
        case ITEM_GHRAZI_RAPIER:
        case ITEM_INQUISITORS_MACE:
        case ITEM_DRAGON_CLAWS:
        case ITEM_AGS:
        case ITEM_ANCIENT_GS:
        case ITEM_GRANITE_MAUL:
        case ITEM_ELDER_MAUL:
        case ITEM_VESTAS:
        case ITEM_VOIDWAKER:
        case ITEM_STATIUS_WARHAMMER:
            return 1;  // ATTACK_STYLE_MELEE
        // Ranged weapons
        case ITEM_RUNE_CROSSBOW:
        case ITEM_ARMADYL_CROSSBOW:
        case ITEM_ZARYTE_CROSSBOW:
        case ITEM_DARK_BOW:
        case ITEM_HEAVY_BALLISTA:
        case ITEM_MORRIGANS_JAVELIN:
            return 2;  // ATTACK_STYLE_RANGED
        // Magic weapons
        case ITEM_AHRIM_STAFF:
        case ITEM_STAFF_OF_DEAD:
        case ITEM_KODAI_WAND:
        case ITEM_VOLATILE_STAFF:
        case ITEM_ZURIELS_STAFF:
            return 3;  // ATTACK_STYLE_MAGIC
        default:
            return 0;  // ATTACK_STYLE_NONE
    }
}

/** Check if weapon is two-handed. */
static inline int item_is_two_handed(uint8_t item_index) {
    switch (item_index) {
        case ITEM_AGS:
        case ITEM_ANCIENT_GS:
        case ITEM_DRAGON_CLAWS:
        case ITEM_GRANITE_MAUL:
        case ITEM_ELDER_MAUL:
        case ITEM_DARK_BOW:
        case ITEM_HEAVY_BALLISTA:
            return 1;
        default:
            return 0;
    }
}

// ============================================================================
// ITEM STATS EXTRACTION (for observations)
// ============================================================================

/** Normalization constants for item stats (max observed values in game). */
#define STAT_NORM_ATTACK 150.0f
#define STAT_NORM_DEFENCE 100.0f
#define STAT_NORM_STRENGTH 150.0f
#define STAT_NORM_MAGIC_DMG 30.0f
#define STAT_NORM_PRAYER 10.0f
#define STAT_NORM_SPEED 10.0f
#define STAT_NORM_RANGE 15.0f

/**
 * Extract normalized item stats for observations.
 *
 * Writes 18 floats to output buffer:
 *   [0-4]   attack bonuses (stab, slash, crush, magic, ranged)
 *   [5-9]   defence bonuses (stab, slash, crush, magic, ranged)
 *   [10-12] strength bonuses (melee, ranged, magic damage %)
 *   [13]    prayer bonus
 *   [14]    attack speed
 *   [15]    attack range
 *   [16]    is_weapon flag (for quick filtering)
 *   [17]    is_empty flag (1 if item_index >= NUM_ITEMS)
 *
 * @param item_index  Item database index
 * @param out         Output buffer (must have space for 18 floats)
 */
static inline void get_item_stats_normalized(uint8_t item_index, float* out) {
    if (item_index >= NUM_ITEMS) {
        // Empty slot - all zeros except is_empty flag
        for (int i = 0; i < 17; i++) out[i] = 0.0f;
        out[17] = 1.0f;  // is_empty
        return;
    }

    const Item* item = &ITEM_DATABASE[item_index];

    // Attack bonuses (normalized by max expected values)
    out[0] = (float)item->attack_stab / STAT_NORM_ATTACK;
    out[1] = (float)item->attack_slash / STAT_NORM_ATTACK;
    out[2] = (float)item->attack_crush / STAT_NORM_ATTACK;
    out[3] = (float)item->attack_magic / STAT_NORM_ATTACK;
    out[4] = (float)item->attack_ranged / STAT_NORM_ATTACK;

    // Defence bonuses
    out[5] = (float)item->defence_stab / STAT_NORM_DEFENCE;
    out[6] = (float)item->defence_slash / STAT_NORM_DEFENCE;
    out[7] = (float)item->defence_crush / STAT_NORM_DEFENCE;
    out[8] = (float)item->defence_magic / STAT_NORM_DEFENCE;
    out[9] = (float)item->defence_ranged / STAT_NORM_DEFENCE;

    // Strength bonuses
    out[10] = (float)item->melee_strength / STAT_NORM_STRENGTH;
    out[11] = (float)item->ranged_strength / STAT_NORM_STRENGTH;
    out[12] = (float)item->magic_damage / STAT_NORM_MAGIC_DMG;

    // Other bonuses
    out[13] = (float)item->prayer / STAT_NORM_PRAYER;
    out[14] = (float)item->attack_speed / STAT_NORM_SPEED;
    out[15] = (float)item->attack_range / STAT_NORM_RANGE;

    // Flags
    out[16] = (item->slot == SLOT_WEAPON) ? 1.0f : 0.0f;
    out[17] = 0.0f;  // not empty
}

/**
 * Get item index from slot inventory.
 *
 * Maps GearSlotIndex to EquipmentSlot and looks up item.
 * Returns 255 (ITEM_NONE) if slot doesn't have that item.
 */
static inline uint8_t get_item_for_slot(int gear_slot, int item_idx) {
    if (item_idx < 0 || item_idx >= MAX_ITEMS_PER_SLOT_DB) return ITEM_NONE;

    // Map GearSlotIndex to EquipmentSlot (they're slightly different)
    int eq_slot;
    switch (gear_slot) {
        case 0: eq_slot = SLOT_HEAD; break;    // GEAR_SLOT_HEAD
        case 1: eq_slot = SLOT_CAPE; break;    // GEAR_SLOT_CAPE
        case 2: eq_slot = SLOT_NECK; break;    // GEAR_SLOT_NECK
        case 3: return ITEM_NONE;              // GEAR_SLOT_AMMO (not in LMS)
        case 4: eq_slot = SLOT_WEAPON; break;  // GEAR_SLOT_WEAPON
        case 5: eq_slot = SLOT_SHIELD; break;  // GEAR_SLOT_SHIELD
        case 6: eq_slot = SLOT_BODY; break;    // GEAR_SLOT_BODY
        case 7: eq_slot = SLOT_LEGS; break;    // GEAR_SLOT_LEGS
        case 8: eq_slot = SLOT_HANDS; break;   // GEAR_SLOT_HANDS
        case 9: eq_slot = SLOT_FEET; break;    // GEAR_SLOT_FEET
        case 10: eq_slot = SLOT_RING; break;   // GEAR_SLOT_RING
        default: return ITEM_NONE;
    }

    return ITEMS_BY_SLOT[eq_slot][item_idx];
}

/**
 * Get number of available items for a gear slot.
 */
static inline int get_num_items_for_slot(int gear_slot) {
    int eq_slot;
    switch (gear_slot) {
        case 0: eq_slot = SLOT_HEAD; break;
        case 1: eq_slot = SLOT_CAPE; break;
        case 2: eq_slot = SLOT_NECK; break;
        case 3: return 0;  // AMMO
        case 4: eq_slot = SLOT_WEAPON; break;
        case 5: eq_slot = SLOT_SHIELD; break;
        case 6: eq_slot = SLOT_BODY; break;
        case 7: eq_slot = SLOT_LEGS; break;
        case 8: eq_slot = SLOT_HANDS; break;
        case 9: eq_slot = SLOT_FEET; break;
        case 10: eq_slot = SLOT_RING; break;
        default: return 0;
    }
    return NUM_ITEMS_IN_SLOT[eq_slot];
}

#endif // OSRS_PVP_ITEMS_H
