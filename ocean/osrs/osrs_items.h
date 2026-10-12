#ifndef OSRS_ITEMS_H
#define OSRS_ITEMS_H

#include <assert.h>
#include <stdint.h>
#include <stddef.h>

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

typedef enum {
    OSRS_ITEM_EFFECT_NONE = 0,
    OSRS_ITEM_EFFECT_TWISTED_BOW = 1u << 0,
    OSRS_ITEM_EFFECT_VIRTUS_PIECE = 1u << 1,
    OSRS_ITEM_EFFECT_CONFLICTION = 1u << 2,
    OSRS_ITEM_EFFECT_SANG_HEAL = 1u << 3,
    OSRS_ITEM_EFFECT_RECOIL_RING = 1u << 4,
    OSRS_ITEM_EFFECT_LIGHTBEARER = 1u << 5,
    OSRS_ITEM_EFFECT_DHAROK_PIECE = 1u << 6,
    OSRS_ITEM_EFFECT_ELYSIAN = 1u << 7,
    OSRS_ITEM_EFFECT_CRYSTAL_ARMOUR = 1u << 8,
    OSRS_ITEM_EFFECT_DRAGON_HUNTER_WAND = 1u << 9,
    OSRS_ITEM_EFFECT_ECHO_BOOTS = 1u << 10,
    OSRS_ITEM_EFFECT_BLOOD_FURY = 1u << 11,
    OSRS_ITEM_EFFECT_VENOM_IMMUNE = 1u << 12,
    OSRS_ITEM_EFFECT_FANG = 1u << 13,
    OSRS_ITEM_EFFECT_TUMEKENS_SHADOW = 1u << 14,
    OSRS_ITEM_EFFECT_VENATOR_BOUNCE = 1u << 15,
} OsrsItemEffectMask;

typedef struct {
    uint16_t item_id;
    char name[32];
    uint8_t slot;
    uint8_t two_handed;
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
    uint32_t effect_mask;
} Item;

#include "osrs_items_generated.h"

/* Equivalent combat gear variants. Locked cape stats: OSRS Wiki,
 * https://oldschool.runescape.wiki/w/Infernal_cape#Combat_stats .
 * Raw 24224 is the locked infernal cape, not granite maul 24225. */
#define OSRS_ITEM_RAW_GEAR_ALIAS_ROWS(X) \
    X(1704, ITEM_GLORY) \
    X(4880, ITEM_DHAROKS_HELM) \
    X(4886, ITEM_DHAROKS_GREATAXE) \
    X(4887, ITEM_DHAROKS_GREATAXE) \
    X(4888, ITEM_DHAROKS_GREATAXE) \
    X(4889, ITEM_DHAROKS_GREATAXE) \
    X(4892, ITEM_DHAROKS_PLATEBODY) \
    X(4898, ITEM_DHAROKS_PLATELEGS) \
    X(4994, ITEM_VERACS_PLATESKIRT) \
    X(24133, ITEM_INFERNAL_CAPE) \
    X(24224, ITEM_INFERNAL_CAPE) \
    X(28055, ITEM_DRAGON_BOOTS) \
    X(28069, ITEM_FIGHTER_TORSO)

static inline int osrs_item_index_from_raw_id(int raw) {
#define OSRS_RAW_GEAR_ALIAS_CASE(RAW, INDEX) case RAW: return INDEX;
    switch (raw) {
        OSRS_ITEM_RAW_GEAR_ALIAS_ROWS(OSRS_RAW_GEAR_ALIAS_CASE)
    }
#undef OSRS_RAW_GEAR_ALIAS_CASE
    for (int i = 0; i < NUM_ITEMS; i++)
        if (ITEM_DATABASE[i].item_id == raw) return i;
    return -1;
}

static inline uint16_t osrs_item_canonical_raw_id(uint16_t raw) {
#define OSRS_CANONICAL_GEAR_CASE(RAW, INDEX) \
    case RAW: return ITEM_DATABASE[INDEX].item_id;
    switch (raw) {
        OSRS_ITEM_RAW_GEAR_ALIAS_ROWS(OSRS_CANONICAL_GEAR_CASE)
        default: return raw;
    }
#undef OSRS_CANONICAL_GEAR_CASE
}

static inline const Item* get_item(uint8_t item_index) {
    if (item_index >= NUM_ITEMS) return NULL;
    return &ITEM_DATABASE[item_index];
}

static inline int item_supports_ancient_autocast(uint8_t item_index) {
    return item_index == ITEM_KODAI_WAND || item_index == ITEM_DRAGON_HUNTER_WAND;
}

static inline int get_item_attack_style(uint8_t item_index) {
    switch (item_index) {
        case ITEM_WHIP:
        case ITEM_DRAGON_SCIMITAR:
        case ITEM_DRAGON_DAGGER:
        case ITEM_GHRAZI_RAPIER:
        case ITEM_INQUISITORS_MACE:
        case ITEM_DRAGON_CLAWS:
        case ITEM_AGS:
        case ITEM_ANCIENT_GS:
        case ITEM_GRANITE_MAUL:
        case ITEM_GRANITE_MAUL_ORNATE:
        case ITEM_DHAROKS_GREATAXE:
        case ITEM_ELDER_MAUL:
        case ITEM_SGS:
        case ITEM_SCYTHE_OF_VITUR:
        case ITEM_OSMUMTENS_FANG:
        case ITEM_ABYSSAL_TENTACLE:
        case ITEM_VESTAS:
        case ITEM_VESTAS_BLIGHTED:
        case ITEM_ABYSSAL_DAGGER_BH:
        case ITEM_ABYSSAL_DAGGER_BH_P_PLUS_PLUS:
        case ITEM_DRAGON_MACE_BH:
        case ITEM_VOIDWAKER:
        case ITEM_STATIUS_WARHAMMER:
            return 1;
        case ITEM_RUNE_CROSSBOW:
        case ITEM_ARMADYL_CROSSBOW:
        case ITEM_ZARYTE_CROSSBOW:
        case ITEM_DARK_BOW:
        case ITEM_HEAVY_BALLISTA:
        case ITEM_MORRIGANS_JAVELIN:
        case ITEM_MAGIC_SHORTBOW_I:
        case ITEM_BOW_OF_FAERDHINEN:
        case ITEM_TWISTED_BOW:
        case ITEM_TOXIC_BLOWPIPE:
        case ITEM_VENATOR_BOW:
            return 2;
        case ITEM_AHRIM_STAFF:
        case ITEM_STAFF_OF_DEAD:
        case ITEM_KODAI_WAND:
        case ITEM_DRAGON_HUNTER_WAND:
        case ITEM_VOLATILE_STAFF:
        case ITEM_ZURIELS_STAFF:
        case ITEM_TRIDENT_OF_SWAMP:
        case ITEM_SANGUINESTI_STAFF:
        case ITEM_EYE_OF_AYAK:
        case ITEM_TUMEKENS_SHADOW:
            return 3;
        default:
            return 0;
    }
}

static inline int item_is_two_handed(uint8_t item_index) {
    if (item_index >= NUM_ITEMS) return 0;
    return ITEM_DATABASE[item_index].slot == SLOT_WEAPON &&
           ITEM_DATABASE[item_index].two_handed;
}

static inline uint8_t osrs_suppress_shield_for_two_handed_weapon(
    uint8_t weapon_item,
    uint8_t shield_item
) {
    return item_is_two_handed(weapon_item) ? ITEM_NONE : shield_item;
}

#define STAT_NORM_ATTACK 150.0f
#define STAT_NORM_DEFENCE 100.0f
#define STAT_NORM_STRENGTH 150.0f
#define STAT_NORM_MAGIC_DMG 30.0f
#define STAT_NORM_PRAYER 10.0f
#define STAT_NORM_SPEED 10.0f
#define STAT_NORM_RANGE 15.0f

#endif
