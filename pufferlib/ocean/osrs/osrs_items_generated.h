/**
 * @file osrs_items_generated.h
 * @brief AUTO-GENERATED item database from equipment.json
 *
 * DO NOT EDIT — regenerate with:
 *   python pufferlib/ocean/osrs/tools/generate_items.py
 */

#ifndef OSRS_ITEMS_GENERATED_H
#define OSRS_ITEMS_GENERATED_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    GEN_SLOT_HEAD = 0,
    GEN_SLOT_CAPE = 1,
    GEN_SLOT_NECK = 2,
    GEN_SLOT_WEAPON = 3,
    GEN_SLOT_BODY = 4,
    GEN_SLOT_SHIELD = 5,
    GEN_SLOT_LEGS = 6,
    GEN_SLOT_HANDS = 7,
    GEN_SLOT_FEET = 8,
    GEN_SLOT_RING = 9,
    GEN_SLOT_AMMO = 10,
    GEN_NUM_EQUIPMENT_SLOTS = 11
} GenEquipmentSlot;

typedef enum {
    GEN_ITEM_HELM_NEITIZNOT = 0,  /* Helm of Neitiznot */
    GEN_ITEM_GOD_CAPE = 1,  /* Imbued god cape */
    GEN_ITEM_GLORY = 2,  /* Amulet of glory */
    GEN_ITEM_BLACK_DHIDE_BODY = 3,  /* Black d'hide body */
    GEN_ITEM_MYSTIC_TOP = 4,  /* Mystic robe top */
    GEN_ITEM_RUNE_PLATELEGS = 5,  /* Rune platelegs */
    GEN_ITEM_MYSTIC_BOTTOM = 6,  /* Mystic robe bottom */
    GEN_ITEM_WHIP = 7,  /* Abyssal whip */
    GEN_ITEM_RUNE_CROSSBOW = 8,  /* Rune crossbow */
    GEN_ITEM_AHRIM_STAFF = 9,  /* Ahrim's staff */
    GEN_ITEM_DRAGON_DAGGER = 10,  /* Dragon dagger */
    GEN_ITEM_DRAGON_DEFENDER = 11,  /* Dragon defender */
    GEN_ITEM_SPIRIT_SHIELD = 12,  /* Spirit shield */
    GEN_ITEM_BARROWS_GLOVES = 13,  /* Barrows gloves */
    GEN_ITEM_CLIMBING_BOOTS = 14,  /* Climbing boots */
    GEN_ITEM_BERSERKER_RING = 15,  /* Berserker ring */
    GEN_ITEM_DIAMOND_BOLTS_E = 16,  /* Diamond bolts (e) */
    GEN_ITEM_GHRAZI_RAPIER = 17,  /* Ghrazi rapier */
    GEN_ITEM_INQUISITORS_MACE = 18,  /* Inquisitor's mace */
    GEN_ITEM_STAFF_OF_DEAD = 19,  /* Staff of the dead */
    GEN_ITEM_KODAI_WAND = 20,  /* Kodai wand */
    GEN_ITEM_VOLATILE_STAFF = 21,  /* Volatile nightmare staff */
    GEN_ITEM_ZURIELS_STAFF = 22,  /* Zuriel's staff (LMS-only, not in wiki equipment.json) */
    GEN_ITEM_ARMADYL_CROSSBOW = 23,  /* Armadyl crossbow */
    GEN_ITEM_ZARYTE_CROSSBOW = 24,  /* Zaryte crossbow */
    GEN_ITEM_DRAGON_CLAWS = 25,  /* Dragon claws */
    GEN_ITEM_AGS = 26,  /* Armadyl godsword */
    GEN_ITEM_ANCIENT_GS = 27,  /* Ancient godsword */
    GEN_ITEM_GRANITE_MAUL = 28,  /* Granite maul */
    GEN_ITEM_ELDER_MAUL = 29,  /* Elder maul */
    GEN_ITEM_DARK_BOW = 30,  /* Dark bow */
    GEN_ITEM_HEAVY_BALLISTA = 31,  /* Heavy ballista */
    GEN_ITEM_VESTAS = 32,  /* Vesta's longsword */
    GEN_ITEM_VOIDWAKER = 33,  /* Voidwaker */
    GEN_ITEM_STATIUS_WARHAMMER = 34,  /* Statius's warhammer */
    GEN_ITEM_MORRIGANS_JAVELIN = 35,  /* Morrigan's javelin */
    GEN_ITEM_ANCESTRAL_HAT = 36,  /* Ancestral hat */
    GEN_ITEM_ANCESTRAL_TOP = 37,  /* Ancestral robe top */
    GEN_ITEM_ANCESTRAL_BOTTOM = 38,  /* Ancestral robe bottom */
    GEN_ITEM_AHRIMS_ROBETOP = 39,  /* Ahrim's robetop */
    GEN_ITEM_AHRIMS_ROBESKIRT = 40,  /* Ahrim's robeskirt */
    GEN_ITEM_KARILS_TOP = 41,  /* Karil's leathertop */
    GEN_ITEM_BANDOS_TASSETS = 42,  /* Bandos tassets */
    GEN_ITEM_BLESSED_SPIRIT_SHIELD = 43,  /* Blessed spirit shield */
    GEN_ITEM_FURY = 44,  /* Amulet of fury */
    GEN_ITEM_OCCULT_NECKLACE = 45,  /* Occult necklace */
    GEN_ITEM_INFERNAL_CAPE = 46,  /* Infernal cape */
    GEN_ITEM_ETERNAL_BOOTS = 47,  /* Eternal boots */
    GEN_ITEM_SEERS_RING_I = 48,  /* Seers ring (i) */
    GEN_ITEM_LIGHTBEARER = 49,  /* Lightbearer */
    GEN_ITEM_MAGES_BOOK = 50,  /* Mage's book */
    GEN_ITEM_DRAGON_ARROWS = 51,  /* Dragon arrows */
    GEN_ITEM_TORAGS_PLATELEGS = 52,  /* Torag's platelegs */
    GEN_ITEM_DHAROKS_PLATELEGS = 53,  /* Dharok's platelegs */
    GEN_ITEM_VERACS_PLATESKIRT = 54,  /* Verac's plateskirt */
    GEN_ITEM_TORAGS_HELM = 55,  /* Torag's helm */
    GEN_ITEM_DHAROKS_HELM = 56,  /* Dharok's helm */
    GEN_ITEM_VERACS_HELM = 57,  /* Verac's helm */
    GEN_ITEM_GUTHANS_HELM = 58,  /* Guthan's helm */
    GEN_ITEM_OPAL_DRAGON_BOLTS = 59,  /* Opal dragon bolts (e) */
    GEN_ITEM_IMBUED_SARA_CAPE = 60,  /* Imbued saradomin cape */
    GEN_ITEM_EYE_OF_AYAK = 61,  /* Eye of ayak */
    GEN_ITEM_ELIDINIS_WARD_F = 62,  /* Elidinis' ward (f) */
    GEN_ITEM_CONFLICTION_GAUNTLETS = 63,  /* Confliction gauntlets */
    GEN_ITEM_AVERNIC_TREADS = 64,  /* Avernic treads (max) */
    GEN_ITEM_RING_OF_SUFFERING_RI = 65,  /* Ring of suffering (ri) */
    GEN_ITEM_TWISTED_BOW = 66,  /* Twisted bow */
    GEN_ITEM_MASORI_MASK_F = 67,  /* Masori mask (f) */
    GEN_ITEM_MASORI_BODY_F = 68,  /* Masori body (f) */
    GEN_ITEM_MASORI_CHAPS_F = 69,  /* Masori chaps (f) */
    GEN_ITEM_NECKLACE_OF_ANGUISH = 70,  /* Necklace of anguish */
    GEN_ITEM_DIZANAS_QUIVER = 71,  /* Dizana's quiver */
    GEN_ITEM_ZARYTE_VAMBRACES = 72,  /* Zaryte vambraces */
    GEN_ITEM_TOXIC_BLOWPIPE = 73,  /* Toxic blowpipe */
    GEN_ITEM_AHRIMS_HOOD = 74,  /* Ahrim's hood */
    GEN_ITEM_TORMENTED_BRACELET = 75,  /* Tormented bracelet */
    GEN_ITEM_SANGUINESTI_STAFF = 76,  /* Sanguinesti staff */
    GEN_ITEM_INFINITY_BOOTS = 77,  /* Infinity boots */
    GEN_ITEM_GOD_BLESSING = 78,  /* Holy blessing */
    GEN_ITEM_RING_OF_RECOIL = 79,  /* Ring of recoil */
    GEN_ITEM_CRYSTAL_HELM = 80,  /* Crystal helm */
    GEN_ITEM_AVAS_ASSEMBLER = 81,  /* Ava's assembler */
    GEN_ITEM_CRYSTAL_BODY = 82,  /* Crystal body */
    GEN_ITEM_CRYSTAL_LEGS = 83,  /* Crystal legs */
    GEN_ITEM_BOW_OF_FAERDHINEN = 84,  /* Bow of faerdhinen (c) */
    GEN_ITEM_BLESSED_DHIDE_BOOTS = 85,  /* Blessed d'hide boots */
    GEN_ITEM_MYSTIC_HAT = 86,  /* Mystic hat */
    GEN_ITEM_TRIDENT_OF_SWAMP = 87,  /* Trident of the swamp */
    GEN_ITEM_BOOK_OF_DARKNESS = 88,  /* Book of darkness */
    GEN_ITEM_AMETHYST_ARROW = 89,  /* Amethyst arrow */
    GEN_ITEM_MYSTIC_BOOTS = 90,  /* Mystic boots */
    GEN_ITEM_BLESSED_COIF = 91,  /* Blessed coif */
    GEN_ITEM_BLACK_DHIDE_CHAPS = 92,  /* Black d'hide chaps */
    GEN_ITEM_MAGIC_SHORTBOW_I = 93,  /* Magic shortbow (i) */
    GEN_ITEM_AVAS_ACCUMULATOR = 94,  /* Ava's accumulator */
    GEN_ITEM_CRYSTAL_SHIELD = 95,  /* Crystal shield */
    GEN_ITEM_PEGASIAN_BOOTS = 96,  /* Pegasian boots */
    GEN_ITEM_JUSTICIAR_FACEGUARD = 97,  /* Justiciar faceguard */
    GEN_ITEM_JUSTICIAR_CHESTGUARD = 98,  /* Justiciar chestguard */
    GEN_ITEM_JUSTICIAR_LEGGUARDS = 99,  /* Justiciar legguards */
    GEN_ITEM_DRAGON_DART = 100,  /* Dragon dart */
    GEN_NUM_ITEMS = 101,
    GEN_ITEM_NONE = 255
} GenItemIndex;

typedef struct {
    uint16_t item_id;
    char name[32];
    uint8_t slot;
    uint8_t attack_speed;
    uint8_t attack_range;
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
    int16_t magic_damage;
    int16_t prayer;
} GenItem;

static const GenItem GEN_ITEM_DATABASE[GEN_NUM_ITEMS] = {
    [GEN_ITEM_HELM_NEITIZNOT] = { /* Helm of Neitiznot */
        .item_id = 10828, .name = "Helm of neitiznot", .slot = GEN_SLOT_HEAD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 31, .defence_slash = 29, .defence_crush = 34,
        .defence_magic = 3, .defence_ranged = 30,
        .melee_strength = 3, .ranged_strength = 0, .magic_damage = 0, .prayer = 3
    },
    [GEN_ITEM_GOD_CAPE] = { /* Imbued god cape */
        .item_id = 21795, .name = "Imbued zamorak cape", .slot = GEN_SLOT_CAPE,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 15, .attack_ranged = 0,
        .defence_stab = 3, .defence_slash = 3, .defence_crush = 3,
        .defence_magic = 15, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 2, .prayer = 0
    },
    [GEN_ITEM_GLORY] = { /* Amulet of glory */
        .item_id = 1712, .name = "Amulet of glory", .slot = GEN_SLOT_NECK,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 10, .attack_slash = 10, .attack_crush = 10,
        .attack_magic = 10, .attack_ranged = 10,
        .defence_stab = 3, .defence_slash = 3, .defence_crush = 3,
        .defence_magic = 3, .defence_ranged = 3,
        .melee_strength = 6, .ranged_strength = 0, .magic_damage = 0, .prayer = 3
    },
    [GEN_ITEM_BLACK_DHIDE_BODY] = { /* Black d'hide body */
        .item_id = 2503, .name = "Black d'hide body", .slot = GEN_SLOT_BODY,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -15, .attack_ranged = 30,
        .defence_stab = 30, .defence_slash = 38, .defence_crush = 45,
        .defence_magic = 45, .defence_ranged = 50,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_MYSTIC_TOP] = { /* Mystic robe top */
        .item_id = 4091, .name = "Mystic robe top", .slot = GEN_SLOT_BODY,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 20, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 20, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_RUNE_PLATELEGS] = { /* Rune platelegs */
        .item_id = 1079, .name = "Rune platelegs", .slot = GEN_SLOT_LEGS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -21, .attack_ranged = -11,
        .defence_stab = 51, .defence_slash = 49, .defence_crush = 47,
        .defence_magic = -4, .defence_ranged = 49,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_MYSTIC_BOTTOM] = { /* Mystic robe bottom */
        .item_id = 4093, .name = "Mystic robe bottom", .slot = GEN_SLOT_LEGS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 15, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 15, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_WHIP] = { /* Abyssal whip */
        .item_id = 4151, .name = "Abyssal whip", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 4, .attack_range = 1,
        .attack_stab = 0, .attack_slash = 82, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 82, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_RUNE_CROSSBOW] = { /* Rune crossbow */
        .item_id = 9185, .name = "Rune crossbow", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 6, .attack_range = 7,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 90,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_AHRIM_STAFF] = { /* Ahrim's staff */
        .item_id = 4710, .name = "Ahrim's staff", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 6, .attack_range = 10,
        .attack_stab = 12, .attack_slash = -1, .attack_crush = 65,
        .attack_magic = 15, .attack_ranged = 0,
        .defence_stab = 3, .defence_slash = 5, .defence_crush = 2,
        .defence_magic = 15, .defence_ranged = 0,
        .melee_strength = 68, .ranged_strength = 0, .magic_damage = 5, .prayer = 0
    },
    [GEN_ITEM_DRAGON_DAGGER] = { /* Dragon dagger */
        .item_id = 5698, .name = "Dragon dagger", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 4, .attack_range = 1,
        .attack_stab = 40, .attack_slash = 25, .attack_crush = -4,
        .attack_magic = 1, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 1, .defence_ranged = 0,
        .melee_strength = 40, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_DRAGON_DEFENDER] = { /* Dragon defender */
        .item_id = 12954, .name = "Dragon defender", .slot = GEN_SLOT_SHIELD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 25, .attack_slash = 24, .attack_crush = 23,
        .attack_magic = -3, .attack_ranged = -2,
        .defence_stab = 25, .defence_slash = 24, .defence_crush = 23,
        .defence_magic = -3, .defence_ranged = -2,
        .melee_strength = 6, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_SPIRIT_SHIELD] = { /* Spirit shield */
        .item_id = 12829, .name = "Spirit shield", .slot = GEN_SLOT_SHIELD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 39, .defence_slash = 41, .defence_crush = 50,
        .defence_magic = 1, .defence_ranged = 45,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 1
    },
    [GEN_ITEM_BARROWS_GLOVES] = { /* Barrows gloves */
        .item_id = 7462, .name = "Barrows gloves", .slot = GEN_SLOT_HANDS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 12, .attack_slash = 12, .attack_crush = 12,
        .attack_magic = 6, .attack_ranged = 12,
        .defence_stab = 12, .defence_slash = 12, .defence_crush = 12,
        .defence_magic = 6, .defence_ranged = 12,
        .melee_strength = 12, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_CLIMBING_BOOTS] = { /* Climbing boots */
        .item_id = 3105, .name = "Climbing boots", .slot = GEN_SLOT_FEET,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 2, .defence_crush = 2,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 2, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_BERSERKER_RING] = { /* Berserker ring */
        .item_id = 6737, .name = "Berserker ring", .slot = GEN_SLOT_RING,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 4,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 4, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_DIAMOND_BOLTS_E] = { /* Diamond bolts (e) */
        .item_id = 9243, .name = "Diamond bolts (e)", .slot = GEN_SLOT_AMMO,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 105, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_GHRAZI_RAPIER] = { /* Ghrazi rapier */
        .item_id = 22324, .name = "Ghrazi rapier", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 4, .attack_range = 1,
        .attack_stab = 94, .attack_slash = 55, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 89, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_INQUISITORS_MACE] = { /* Inquisitor's mace */
        .item_id = 24417, .name = "Inquisitor's mace", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 4, .attack_range = 1,
        .attack_stab = 52, .attack_slash = -4, .attack_crush = 95,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 89, .ranged_strength = 0, .magic_damage = 0, .prayer = 2
    },
    [GEN_ITEM_STAFF_OF_DEAD] = { /* Staff of the dead */
        .item_id = 11791, .name = "Staff of the dead", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 4, .attack_range = 10,
        .attack_stab = 55, .attack_slash = 70, .attack_crush = 0,
        .attack_magic = 17, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 3, .defence_crush = 3,
        .defence_magic = 17, .defence_ranged = 0,
        .melee_strength = 72, .ranged_strength = 0, .magic_damage = 15, .prayer = 0
    },
    [GEN_ITEM_KODAI_WAND] = { /* Kodai wand */
        .item_id = 21006, .name = "Kodai wand", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 4, .attack_range = 10,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 28, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 3, .defence_crush = 3,
        .defence_magic = 20, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 15, .prayer = 0
    },
    [GEN_ITEM_VOLATILE_STAFF] = { /* Volatile nightmare staff */
        .item_id = 24424, .name = "Volatile nightmare staff", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 5, .attack_range = 10,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 16, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 14, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 15, .prayer = 0
    },
    [GEN_ITEM_ZURIELS_STAFF] = { /* Zuriel's staff (LMS-only, not in wiki equipment.json) (manual) */
        .item_id = 13867, .name = "Zuriel's staff", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 5, .attack_range = 10,
        .attack_stab = 13, .attack_slash = -1, .attack_crush = 65,
        .attack_magic = 18, .attack_ranged = 0,
        .defence_stab = 5, .defence_slash = 7, .defence_crush = 4,
        .defence_magic = 18, .defence_ranged = 0,
        .melee_strength = 72, .ranged_strength = 0, .magic_damage = 10, .prayer = 0
    },
    [GEN_ITEM_ARMADYL_CROSSBOW] = { /* Armadyl crossbow */
        .item_id = 11785, .name = "Armadyl crossbow", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 6, .attack_range = 7,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 100,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 1
    },
    [GEN_ITEM_ZARYTE_CROSSBOW] = { /* Zaryte crossbow */
        .item_id = 26374, .name = "Zaryte crossbow", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 6, .attack_range = 7,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 110,
        .defence_stab = 14, .defence_slash = 14, .defence_crush = 12,
        .defence_magic = 15, .defence_ranged = 16,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 1
    },
    [GEN_ITEM_DRAGON_CLAWS] = { /* Dragon claws */
        .item_id = 13652, .name = "Dragon claws", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 4, .attack_range = 1,
        .attack_stab = 41, .attack_slash = 57, .attack_crush = -4,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 13, .defence_slash = 26, .defence_crush = 7,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 56, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_AGS] = { /* Armadyl godsword */
        .item_id = 11802, .name = "Armadyl godsword", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 6, .attack_range = 1,
        .attack_stab = 0, .attack_slash = 132, .attack_crush = 80,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 132, .ranged_strength = 0, .magic_damage = 0, .prayer = 8
    },
    [GEN_ITEM_ANCIENT_GS] = { /* Ancient godsword */
        .item_id = 26233, .name = "Ancient godsword", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 6, .attack_range = 1,
        .attack_stab = 0, .attack_slash = 132, .attack_crush = 80,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 132, .ranged_strength = 0, .magic_damage = 0, .prayer = 8
    },
    [GEN_ITEM_GRANITE_MAUL] = { /* Granite maul */
        .item_id = 4153, .name = "Granite maul", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 7, .attack_range = 1,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 81,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 79, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_ELDER_MAUL] = { /* Elder maul */
        .item_id = 21003, .name = "Elder maul", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 6, .attack_range = 1,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 135,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 147, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_DARK_BOW] = { /* Dark bow */
        .item_id = 11235, .name = "Dark bow", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 9, .attack_range = 10,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 95,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_HEAVY_BALLISTA] = { /* Heavy ballista */
        .item_id = 19481, .name = "Heavy ballista", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 7, .attack_range = 10,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 125,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 15, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_VESTAS] = { /* Vesta's longsword */
        .item_id = 22613, .name = "Vesta's longsword (Deadman Mode", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 5, .attack_range = 1,
        .attack_stab = 106, .attack_slash = 121, .attack_crush = -2,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 1, .defence_slash = 4, .defence_crush = 3,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 118, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_VOIDWAKER] = { /* Voidwaker */
        .item_id = 27690, .name = "Voidwaker", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 4, .attack_range = 1,
        .attack_stab = 70, .attack_slash = 80, .attack_crush = -2,
        .attack_magic = 5, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 1, .defence_crush = 0,
        .defence_magic = 2, .defence_ranged = 0,
        .melee_strength = 80, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_STATIUS_WARHAMMER] = { /* Statius's warhammer */
        .item_id = 22622, .name = "Statius's warhammer (Deadman Mo", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 5, .attack_range = 1,
        .attack_stab = -4, .attack_slash = -4, .attack_crush = 123,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 114, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_MORRIGANS_JAVELIN] = { /* Morrigan's javelin */
        .item_id = 22636, .name = "Morrigan's javelin (Deadman Mod", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 6, .attack_range = 5,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 105,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 145, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_ANCESTRAL_HAT] = { /* Ancestral hat */
        .item_id = 21018, .name = "Ancestral hat", .slot = GEN_SLOT_HEAD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 8, .attack_ranged = -2,
        .defence_stab = 12, .defence_slash = 11, .defence_crush = 13,
        .defence_magic = 5, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 3, .prayer = 0
    },
    [GEN_ITEM_ANCESTRAL_TOP] = { /* Ancestral robe top */
        .item_id = 21021, .name = "Ancestral robe top", .slot = GEN_SLOT_BODY,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 35, .attack_ranged = -8,
        .defence_stab = 42, .defence_slash = 31, .defence_crush = 51,
        .defence_magic = 28, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 3, .prayer = 0
    },
    [GEN_ITEM_ANCESTRAL_BOTTOM] = { /* Ancestral robe bottom */
        .item_id = 21024, .name = "Ancestral robe bottom", .slot = GEN_SLOT_LEGS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 26, .attack_ranged = -7,
        .defence_stab = 27, .defence_slash = 24, .defence_crush = 30,
        .defence_magic = 20, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 3, .prayer = 0
    },
    [GEN_ITEM_AHRIMS_ROBETOP] = { /* Ahrim's robetop */
        .item_id = 4712, .name = "Ahrim's robetop", .slot = GEN_SLOT_BODY,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 30, .attack_ranged = -10,
        .defence_stab = 52, .defence_slash = 37, .defence_crush = 63,
        .defence_magic = 30, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 1, .prayer = 0
    },
    [GEN_ITEM_AHRIMS_ROBESKIRT] = { /* Ahrim's robeskirt */
        .item_id = 4714, .name = "Ahrim's robeskirt", .slot = GEN_SLOT_LEGS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 22, .attack_ranged = -7,
        .defence_stab = 33, .defence_slash = 30, .defence_crush = 36,
        .defence_magic = 22, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 1, .prayer = 0
    },
    [GEN_ITEM_KARILS_TOP] = { /* Karil's leathertop */
        .item_id = 4736, .name = "Karil's leathertop", .slot = GEN_SLOT_BODY,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -15, .attack_ranged = 30,
        .defence_stab = 47, .defence_slash = 42, .defence_crush = 50,
        .defence_magic = 65, .defence_ranged = 57,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_BANDOS_TASSETS] = { /* Bandos tassets */
        .item_id = 11834, .name = "Bandos tassets", .slot = GEN_SLOT_LEGS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -21, .attack_ranged = -7,
        .defence_stab = 71, .defence_slash = 63, .defence_crush = 66,
        .defence_magic = -4, .defence_ranged = 93,
        .melee_strength = 2, .ranged_strength = 0, .magic_damage = 0, .prayer = 1
    },
    [GEN_ITEM_BLESSED_SPIRIT_SHIELD] = { /* Blessed spirit shield */
        .item_id = 12831, .name = "Blessed spirit shield", .slot = GEN_SLOT_SHIELD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 53, .defence_slash = 55, .defence_crush = 73,
        .defence_magic = 2, .defence_ranged = 52,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 3
    },
    [GEN_ITEM_FURY] = { /* Amulet of fury */
        .item_id = 6585, .name = "Amulet of fury", .slot = GEN_SLOT_NECK,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 10, .attack_slash = 10, .attack_crush = 10,
        .attack_magic = 10, .attack_ranged = 10,
        .defence_stab = 15, .defence_slash = 15, .defence_crush = 15,
        .defence_magic = 15, .defence_ranged = 15,
        .melee_strength = 8, .ranged_strength = 0, .magic_damage = 0, .prayer = 5
    },
    [GEN_ITEM_OCCULT_NECKLACE] = { /* Occult necklace */
        .item_id = 12002, .name = "Occult necklace", .slot = GEN_SLOT_NECK,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 12, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 5, .prayer = 2
    },
    [GEN_ITEM_INFERNAL_CAPE] = { /* Infernal cape */
        .item_id = 21295, .name = "Infernal cape", .slot = GEN_SLOT_CAPE,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 4, .attack_slash = 4, .attack_crush = 4,
        .attack_magic = 1, .attack_ranged = 1,
        .defence_stab = 12, .defence_slash = 12, .defence_crush = 12,
        .defence_magic = 12, .defence_ranged = 12,
        .melee_strength = 8, .ranged_strength = 0, .magic_damage = 0, .prayer = 2
    },
    [GEN_ITEM_ETERNAL_BOOTS] = { /* Eternal boots */
        .item_id = 13235, .name = "Eternal boots", .slot = GEN_SLOT_FEET,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 8, .attack_ranged = 0,
        .defence_stab = 5, .defence_slash = 5, .defence_crush = 5,
        .defence_magic = 8, .defence_ranged = 5,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 1, .prayer = 0
    },
    [GEN_ITEM_SEERS_RING_I] = { /* Seers ring (i) */
        .item_id = 11770, .name = "Seers ring (i)", .slot = GEN_SLOT_RING,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 12, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 12, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 1, .prayer = 0
    },
    [GEN_ITEM_LIGHTBEARER] = { /* Lightbearer */
        .item_id = 25975, .name = "Lightbearer", .slot = GEN_SLOT_RING,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_MAGES_BOOK] = { /* Mage's book */
        .item_id = 6889, .name = "Mage's book", .slot = GEN_SLOT_SHIELD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 15, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 15, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 2, .prayer = 0
    },
    [GEN_ITEM_DRAGON_ARROWS] = { /* Dragon arrows */
        .item_id = 11212, .name = "Dragon arrow", .slot = GEN_SLOT_AMMO,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 60, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_TORAGS_PLATELEGS] = { /* Torag's platelegs */
        .item_id = 4751, .name = "Torag's platelegs", .slot = GEN_SLOT_LEGS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -21, .attack_ranged = -11,
        .defence_stab = 85, .defence_slash = 82, .defence_crush = 83,
        .defence_magic = -4, .defence_ranged = 92,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_DHAROKS_PLATELEGS] = { /* Dharok's platelegs */
        .item_id = 4722, .name = "Dharok's platelegs", .slot = GEN_SLOT_LEGS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -21, .attack_ranged = -11,
        .defence_stab = 85, .defence_slash = 82, .defence_crush = 83,
        .defence_magic = -4, .defence_ranged = 92,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_VERACS_PLATESKIRT] = { /* Verac's plateskirt */
        .item_id = 4759, .name = "Verac's plateskirt", .slot = GEN_SLOT_LEGS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -21, .attack_ranged = -11,
        .defence_stab = 85, .defence_slash = 82, .defence_crush = 83,
        .defence_magic = 0, .defence_ranged = 84,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 4
    },
    [GEN_ITEM_TORAGS_HELM] = { /* Torag's helm */
        .item_id = 4745, .name = "Torag's helm", .slot = GEN_SLOT_HEAD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -6, .attack_ranged = -2,
        .defence_stab = 55, .defence_slash = 58, .defence_crush = 54,
        .defence_magic = -1, .defence_ranged = 62,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_DHAROKS_HELM] = { /* Dharok's helm */
        .item_id = 4716, .name = "Dharok's helm", .slot = GEN_SLOT_HEAD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -3, .attack_ranged = -1,
        .defence_stab = 45, .defence_slash = 48, .defence_crush = 44,
        .defence_magic = -1, .defence_ranged = 51,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_VERACS_HELM] = { /* Verac's helm */
        .item_id = 4753, .name = "Verac's helm", .slot = GEN_SLOT_HEAD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -6, .attack_ranged = -2,
        .defence_stab = 55, .defence_slash = 58, .defence_crush = 54,
        .defence_magic = 0, .defence_ranged = 56,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 3
    },
    [GEN_ITEM_GUTHANS_HELM] = { /* Guthan's helm */
        .item_id = 4724, .name = "Guthan's helm", .slot = GEN_SLOT_HEAD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -6, .attack_ranged = -2,
        .defence_stab = 55, .defence_slash = 58, .defence_crush = 54,
        .defence_magic = -1, .defence_ranged = 62,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_OPAL_DRAGON_BOLTS] = { /* Opal dragon bolts (e) */
        .item_id = 21932, .name = "Opal dragon bolts (e)", .slot = GEN_SLOT_AMMO,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 122, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_IMBUED_SARA_CAPE] = { /* Imbued saradomin cape */
        .item_id = 21791, .name = "Imbued saradomin cape", .slot = GEN_SLOT_CAPE,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 15, .attack_ranged = 0,
        .defence_stab = 3, .defence_slash = 3, .defence_crush = 3,
        .defence_magic = 15, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 2, .prayer = 0
    },
    [GEN_ITEM_EYE_OF_AYAK] = { /* Eye of ayak */
        .item_id = 31113, .name = "Eye of ayak", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 3, .attack_range = 6,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 30, .attack_ranged = 0,
        .defence_stab = 1, .defence_slash = 5, .defence_crush = 5,
        .defence_magic = 10, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 2
    },
    [GEN_ITEM_ELIDINIS_WARD_F] = { /* Elidinis' ward (f) */
        .item_id = 27251, .name = "Elidinis' ward (f)", .slot = GEN_SLOT_SHIELD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 25, .attack_ranged = 0,
        .defence_stab = 53, .defence_slash = 55, .defence_crush = 73,
        .defence_magic = 2, .defence_ranged = 52,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 5, .prayer = 4
    },
    [GEN_ITEM_CONFLICTION_GAUNTLETS] = { /* Confliction gauntlets */
        .item_id = 31106, .name = "Confliction gauntlets", .slot = GEN_SLOT_HANDS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 20, .attack_ranged = -4,
        .defence_stab = 15, .defence_slash = 18, .defence_crush = 7,
        .defence_magic = 5, .defence_ranged = 5,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 7, .prayer = 2
    },
    [GEN_ITEM_AVERNIC_TREADS] = { /* Avernic treads (max) */
        .item_id = 31097, .name = "Avernic treads (max)", .slot = GEN_SLOT_FEET,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 5, .attack_slash = 5, .attack_crush = 5,
        .attack_magic = 11, .attack_ranged = 15,
        .defence_stab = 21, .defence_slash = 25, .defence_crush = 25,
        .defence_magic = 10, .defence_ranged = 10,
        .melee_strength = 6, .ranged_strength = 3, .magic_damage = 2, .prayer = 0
    },
    [GEN_ITEM_RING_OF_SUFFERING_RI] = { /* Ring of suffering (ri) */
        .item_id = 20657, .name = "Ring of suffering (i)", .slot = GEN_SLOT_RING,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 20, .defence_slash = 20, .defence_crush = 20,
        .defence_magic = 20, .defence_ranged = 20,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 4
    },
    [GEN_ITEM_TWISTED_BOW] = { /* Twisted bow */
        .item_id = 20997, .name = "Twisted bow", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 6, .attack_range = 10,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 70,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 20, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_MASORI_MASK_F] = { /* Masori mask (f) */
        .item_id = 27235, .name = "Masori mask (f)", .slot = GEN_SLOT_HEAD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -1, .attack_ranged = 12,
        .defence_stab = 8, .defence_slash = 10, .defence_crush = 12,
        .defence_magic = 12, .defence_ranged = 9,
        .melee_strength = 0, .ranged_strength = 2, .magic_damage = 0, .prayer = 1
    },
    [GEN_ITEM_MASORI_BODY_F] = { /* Masori body (f) */
        .item_id = 27238, .name = "Masori body (f)", .slot = GEN_SLOT_BODY,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -4, .attack_ranged = 43,
        .defence_stab = 59, .defence_slash = 52, .defence_crush = 64,
        .defence_magic = 74, .defence_ranged = 60,
        .melee_strength = 0, .ranged_strength = 4, .magic_damage = 0, .prayer = 1
    },
    [GEN_ITEM_MASORI_CHAPS_F] = { /* Masori chaps (f) */
        .item_id = 27241, .name = "Masori chaps (f)", .slot = GEN_SLOT_LEGS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -2, .attack_ranged = 27,
        .defence_stab = 35, .defence_slash = 30, .defence_crush = 39,
        .defence_magic = 46, .defence_ranged = 37,
        .melee_strength = 0, .ranged_strength = 2, .magic_damage = 0, .prayer = 1
    },
    [GEN_ITEM_NECKLACE_OF_ANGUISH] = { /* Necklace of anguish */
        .item_id = 19547, .name = "Necklace of anguish", .slot = GEN_SLOT_NECK,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 15,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 5, .magic_damage = 0, .prayer = 2
    },
    [GEN_ITEM_DIZANAS_QUIVER] = { /* Dizana's quiver */
        .item_id = 28947, .name = "Dizana's quiver", .slot = GEN_SLOT_CAPE,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 18,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 3, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_ZARYTE_VAMBRACES] = { /* Zaryte vambraces */
        .item_id = 26235, .name = "Zaryte vambraces", .slot = GEN_SLOT_HANDS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = -8, .attack_slash = -8, .attack_crush = -8,
        .attack_magic = 0, .attack_ranged = 18,
        .defence_stab = 8, .defence_slash = 8, .defence_crush = 8,
        .defence_magic = 5, .defence_ranged = 8,
        .melee_strength = 0, .ranged_strength = 2, .magic_damage = 0, .prayer = 1
    },
    [GEN_ITEM_TOXIC_BLOWPIPE] = { /* Toxic blowpipe */
        .item_id = 12926, .name = "Toxic blowpipe", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 3, .attack_range = 5,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 30,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 20, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_AHRIMS_HOOD] = { /* Ahrim's hood */
        .item_id = 4708, .name = "Ahrim's hood", .slot = GEN_SLOT_HEAD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 6, .attack_ranged = -2,
        .defence_stab = 15, .defence_slash = 13, .defence_crush = 16,
        .defence_magic = 6, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 1, .prayer = 0
    },
    [GEN_ITEM_TORMENTED_BRACELET] = { /* Tormented bracelet */
        .item_id = 19544, .name = "Tormented bracelet", .slot = GEN_SLOT_HANDS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 10, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 5, .prayer = 2
    },
    [GEN_ITEM_SANGUINESTI_STAFF] = { /* Sanguinesti staff */
        .item_id = 22481, .name = "Sanguinesti staff", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 4, .attack_range = 7,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 25, .attack_ranged = -4,
        .defence_stab = 2, .defence_slash = 3, .defence_crush = 1,
        .defence_magic = 15, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_INFINITY_BOOTS] = { /* Infinity boots */
        .item_id = 6920, .name = "Infinity boots", .slot = GEN_SLOT_FEET,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 5, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 5, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_GOD_BLESSING] = { /* Holy blessing */
        .item_id = 20220, .name = "Holy blessing", .slot = GEN_SLOT_AMMO,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 1
    },
    [GEN_ITEM_RING_OF_RECOIL] = { /* Ring of recoil */
        .item_id = 2550, .name = "Ring of recoil", .slot = GEN_SLOT_RING,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_CRYSTAL_HELM] = { /* Crystal helm */
        .item_id = 23971, .name = "Crystal helm", .slot = GEN_SLOT_HEAD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -10, .attack_ranged = 9,
        .defence_stab = 12, .defence_slash = 8, .defence_crush = 14,
        .defence_magic = 10, .defence_ranged = 18,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 2
    },
    [GEN_ITEM_AVAS_ASSEMBLER] = { /* Ava's assembler */
        .item_id = 22109, .name = "Ava's assembler", .slot = GEN_SLOT_CAPE,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 8,
        .defence_stab = 1, .defence_slash = 1, .defence_crush = 1,
        .defence_magic = 8, .defence_ranged = 2,
        .melee_strength = 0, .ranged_strength = 2, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_CRYSTAL_BODY] = { /* Crystal body */
        .item_id = 23975, .name = "Crystal body", .slot = GEN_SLOT_BODY,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -18, .attack_ranged = 31,
        .defence_stab = 46, .defence_slash = 38, .defence_crush = 48,
        .defence_magic = 44, .defence_ranged = 68,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 3
    },
    [GEN_ITEM_CRYSTAL_LEGS] = { /* Crystal legs */
        .item_id = 23979, .name = "Crystal legs", .slot = GEN_SLOT_LEGS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -12, .attack_ranged = 18,
        .defence_stab = 26, .defence_slash = 21, .defence_crush = 30,
        .defence_magic = 34, .defence_ranged = 38,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 2
    },
    [GEN_ITEM_BOW_OF_FAERDHINEN] = { /* Bow of faerdhinen (c) */
        .item_id = 25865, .name = "Bow of faerdhinen", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 5, .attack_range = 10,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 128,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 106, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_BLESSED_DHIDE_BOOTS] = { /* Blessed d'hide boots */
        .item_id = 19921, .name = "Ancient d'hide boots", .slot = GEN_SLOT_FEET,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -10, .attack_ranged = 7,
        .defence_stab = 4, .defence_slash = 4, .defence_crush = 4,
        .defence_magic = 4, .defence_ranged = 4,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 1
    },
    [GEN_ITEM_MYSTIC_HAT] = { /* Mystic hat */
        .item_id = 4089, .name = "Mystic hat", .slot = GEN_SLOT_HEAD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 4, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 4, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_TRIDENT_OF_SWAMP] = { /* Trident of the swamp */
        .item_id = 12899, .name = "Trident of the swamp", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 4, .attack_range = 7,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 25, .attack_ranged = 0,
        .defence_stab = 2, .defence_slash = 3, .defence_crush = 1,
        .defence_magic = 15, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_BOOK_OF_DARKNESS] = { /* Book of darkness */
        .item_id = 12612, .name = "Book of darkness", .slot = GEN_SLOT_SHIELD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 10, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 5
    },
    [GEN_ITEM_AMETHYST_ARROW] = { /* Amethyst arrow */
        .item_id = 21326, .name = "Amethyst arrow", .slot = GEN_SLOT_AMMO,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 55, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_MYSTIC_BOOTS] = { /* Mystic boots */
        .item_id = 4097, .name = "Mystic boots", .slot = GEN_SLOT_FEET,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 3, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 3, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_BLESSED_COIF] = { /* Blessed coif */
        .item_id = 10382, .name = "Guthix coif", .slot = GEN_SLOT_HEAD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -1, .attack_ranged = 7,
        .defence_stab = 4, .defence_slash = 7, .defence_crush = 10,
        .defence_magic = 4, .defence_ranged = 8,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 1
    },
    [GEN_ITEM_BLACK_DHIDE_CHAPS] = { /* Black d'hide chaps */
        .item_id = 2497, .name = "Black d'hide chaps", .slot = GEN_SLOT_LEGS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -10, .attack_ranged = 17,
        .defence_stab = 18, .defence_slash = 20, .defence_crush = 26,
        .defence_magic = 23, .defence_ranged = 26,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_MAGIC_SHORTBOW_I] = { /* Magic shortbow (i) */
        .item_id = 12788, .name = "Magic shortbow (i)", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 4, .attack_range = 7,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 75,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_AVAS_ACCUMULATOR] = { /* Ava's accumulator */
        .item_id = 10499, .name = "Ava's accumulator", .slot = GEN_SLOT_CAPE,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 4,
        .defence_stab = 0, .defence_slash = 1, .defence_crush = 0,
        .defence_magic = 4, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_CRYSTAL_SHIELD] = { /* Crystal shield */
        .item_id = 4224, .name = "Crystal shield (historical)", .slot = GEN_SLOT_SHIELD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -10, .attack_ranged = -10,
        .defence_stab = 51, .defence_slash = 54, .defence_crush = 53,
        .defence_magic = 0, .defence_ranged = 80,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_PEGASIAN_BOOTS] = { /* Pegasian boots */
        .item_id = 13237, .name = "Pegasian boots", .slot = GEN_SLOT_FEET,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -12, .attack_ranged = 12,
        .defence_stab = 5, .defence_slash = 5, .defence_crush = 5,
        .defence_magic = 5, .defence_ranged = 5,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 0
    },
    [GEN_ITEM_JUSTICIAR_FACEGUARD] = { /* Justiciar faceguard */
        .item_id = 22326, .name = "Justiciar faceguard", .slot = GEN_SLOT_HEAD,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -6, .attack_ranged = -2,
        .defence_stab = 60, .defence_slash = 63, .defence_crush = 59,
        .defence_magic = -6, .defence_ranged = 67,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 2
    },
    [GEN_ITEM_JUSTICIAR_CHESTGUARD] = { /* Justiciar chestguard */
        .item_id = 22327, .name = "Justiciar chestguard", .slot = GEN_SLOT_BODY,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -40, .attack_ranged = -20,
        .defence_stab = 132, .defence_slash = 130, .defence_crush = 117,
        .defence_magic = -16, .defence_ranged = 142,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 4
    },
    [GEN_ITEM_JUSTICIAR_LEGGUARDS] = { /* Justiciar legguards */
        .item_id = 22328, .name = "Justiciar legguards", .slot = GEN_SLOT_LEGS,
        .attack_speed = 0, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = -31, .attack_ranged = -17,
        .defence_stab = 95, .defence_slash = 92, .defence_crush = 93,
        .defence_magic = -14, .defence_ranged = 102,
        .melee_strength = 0, .ranged_strength = 0, .magic_damage = 0, .prayer = 4
    },
    [GEN_ITEM_DRAGON_DART] = { /* Dragon dart */
        .item_id = 11230, .name = "Dragon dart", .slot = GEN_SLOT_WEAPON,
        .attack_speed = 3, .attack_range = 0,
        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,
        .attack_magic = 0, .attack_ranged = 0,
        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,
        .defence_magic = 0, .defence_ranged = 0,
        .melee_strength = 0, .ranged_strength = 35, .magic_damage = 0, .prayer = 0
    },
};

#endif /* OSRS_ITEMS_GENERATED_H */
