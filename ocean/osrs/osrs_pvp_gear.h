/**
 * @file osrs_pvp_gear.h
 * @brief Dynamic loadout resolution and gear management
 *
 * Priority-based loadout system: each loadout queries the player's inventory
 * for best available items. No hardcoded loadout definitions.
 */

#ifndef OSRS_PVP_GEAR_H
#define OSRS_PVP_GEAR_H

#include "osrs_types.h"
#include "osrs_items.h"
#include "osrs_inventory.h"
#include "osrs_combat.h"
#include "osrs_item_effects.h"

static const MeleeBonusType MELEE_SPEC_BONUS_TYPES[] = {
    [MELEE_SPEC_NONE] = MELEE_BONUS_SLASH,
    [MELEE_SPEC_AGS] = MELEE_BONUS_SLASH,
    [MELEE_SPEC_DRAGON_CLAWS] = MELEE_BONUS_SLASH,
    [MELEE_SPEC_GRANITE_MAUL] = MELEE_BONUS_CRUSH,
    [MELEE_SPEC_DRAGON_DAGGER] = MELEE_BONUS_STAB,
    [MELEE_SPEC_VOIDWAKER] = MELEE_BONUS_SLASH,
    [MELEE_SPEC_DWH] = MELEE_BONUS_CRUSH,
    [MELEE_SPEC_BGS] = MELEE_BONUS_SLASH,
    [MELEE_SPEC_ZGS] = MELEE_BONUS_SLASH,
    [MELEE_SPEC_SGS] = MELEE_BONUS_SLASH,
    [MELEE_SPEC_ANCIENT_GS] = MELEE_BONUS_SLASH,
    [MELEE_SPEC_VESTAS] = MELEE_BONUS_SLASH,
    [MELEE_SPEC_ABYSSAL_DAGGER] = MELEE_BONUS_STAB,
    [MELEE_SPEC_DRAGON_LONGSWORD] = MELEE_BONUS_SLASH,
    [MELEE_SPEC_DRAGON_MACE] = MELEE_BONUS_CRUSH,
    [MELEE_SPEC_ABYSSAL_BLUDGEON] = MELEE_BONUS_CRUSH,
};
// WEAPON PRIORITY TABLES (best to worst within each style)

static const uint8_t MELEE_WEAPON_PRIORITY[] = {
    ITEM_VESTAS, ITEM_GHRAZI_RAPIER, ITEM_INQUISITORS_MACE, ITEM_ELDER_MAUL,
    ITEM_VOIDWAKER, ITEM_ANCIENT_GS, ITEM_AGS, ITEM_STATIUS_WARHAMMER, ITEM_WHIP
};
#define MELEE_WEAPON_PRIORITY_LEN 9

static const uint8_t RANGE_WEAPON_PRIORITY[] = {
    ITEM_MORRIGANS_JAVELIN, ITEM_ZARYTE_CROSSBOW, ITEM_ARMADYL_CROSSBOW, ITEM_RUNE_CROSSBOW
};
#define RANGE_WEAPON_PRIORITY_LEN 4

static const uint8_t MAGE_WEAPON_PRIORITY[] = {
    ITEM_ZURIELS_STAFF, ITEM_KODAI_WAND, ITEM_VOLATILE_STAFF,
    ITEM_STAFF_OF_DEAD, ITEM_AHRIM_STAFF
};
#define MAGE_WEAPON_PRIORITY_LEN 5

static const uint8_t MELEE_SPEC_PRIORITY[] = {
    ITEM_VESTAS, ITEM_ANCIENT_GS, ITEM_AGS, ITEM_DRAGON_CLAWS,
    ITEM_VOIDWAKER, ITEM_STATIUS_WARHAMMER, ITEM_DRAGON_DAGGER
};
#define MELEE_SPEC_PRIORITY_LEN 7

static const uint8_t RANGE_SPEC_PRIORITY[] = {
    ITEM_MORRIGANS_JAVELIN, ITEM_ZARYTE_CROSSBOW, ITEM_ARMADYL_CROSSBOW,
    ITEM_DARK_BOW, ITEM_HEAVY_BALLISTA
};
#define RANGE_SPEC_PRIORITY_LEN 5

// Magic spec: only volatile nightmare staff
static const uint8_t MAGIC_SPEC_PRIORITY[] = {
    ITEM_VOLATILE_STAFF
};
#define MAGIC_SPEC_PRIORITY_LEN 1
// ARMOR PRIORITY TABLES (per style)

// Body armor
static const uint8_t TANK_BODY_PRIORITY[] = {
    ITEM_KARILS_TOP, ITEM_BLACK_DHIDE_BODY
};
#define TANK_BODY_PRIORITY_LEN 2

static const uint8_t MAGE_BODY_PRIORITY[] = {
    ITEM_ANCESTRAL_TOP, ITEM_AHRIMS_ROBETOP, ITEM_MYSTIC_TOP
};
#define MAGE_BODY_PRIORITY_LEN 3

// Legs armor
static const uint8_t TANK_LEGS_PRIORITY[] = {
    ITEM_BANDOS_TASSETS, ITEM_TORAGS_PLATELEGS, ITEM_DHAROKS_PLATELEGS,
    ITEM_VERACS_PLATESKIRT, ITEM_RUNE_PLATELEGS
};
#define TANK_LEGS_PRIORITY_LEN 5

static const uint8_t MAGE_LEGS_PRIORITY[] = {
    ITEM_ANCESTRAL_BOTTOM, ITEM_AHRIMS_ROBESKIRT, ITEM_MYSTIC_BOTTOM
};
#define MAGE_LEGS_PRIORITY_LEN 3

// Shield
static const uint8_t MELEE_SHIELD_PRIORITY[] = {
    ITEM_DRAGON_DEFENDER
};
#define MELEE_SHIELD_PRIORITY_LEN 1

static const uint8_t TANK_SHIELD_PRIORITY[] = {
    ITEM_BLESSED_SPIRIT_SHIELD, ITEM_SPIRIT_SHIELD
};
#define TANK_SHIELD_PRIORITY_LEN 2

static const uint8_t MAGE_SHIELD_PRIORITY[] = {
    ITEM_MAGES_BOOK, ITEM_BLESSED_SPIRIT_SHIELD, ITEM_SPIRIT_SHIELD
};
#define MAGE_SHIELD_PRIORITY_LEN 3

// Head
static const uint8_t TANK_HEAD_PRIORITY[] = {
    ITEM_TORAGS_HELM, ITEM_GUTHANS_HELM, ITEM_VERACS_HELM,
    ITEM_DHAROKS_HELM, ITEM_HELM_NEITIZNOT
};
#define TANK_HEAD_PRIORITY_LEN 5

static const uint8_t MAGE_HEAD_PRIORITY[] = {ITEM_ANCESTRAL_HAT, ITEM_HELM_NEITIZNOT};
#define MAGE_HEAD_PRIORITY_LEN 2

// Cape
static const uint8_t MELEE_CAPE_PRIORITY[] = {ITEM_INFERNAL_CAPE, ITEM_GOD_CAPE};
#define MELEE_CAPE_PRIORITY_LEN 2

static const uint8_t MAGE_CAPE_PRIORITY[] = {ITEM_GOD_CAPE};
#define MAGE_CAPE_PRIORITY_LEN 1

// Neck
static const uint8_t MELEE_NECK_PRIORITY[] = {ITEM_FURY, ITEM_GLORY};
#define MELEE_NECK_PRIORITY_LEN 2

static const uint8_t MAGE_NECK_PRIORITY[] = {ITEM_OCCULT_NECKLACE, ITEM_GLORY};
#define MAGE_NECK_PRIORITY_LEN 2

// Ring
static const uint8_t MELEE_RING_PRIORITY[] = {ITEM_BERSERKER_RING};
#define MELEE_RING_PRIORITY_LEN 1

static const uint8_t MAGE_RING_PRIORITY[] = {ITEM_LIGHTBEARER, ITEM_SEERS_RING_I, ITEM_BERSERKER_RING};
#define MAGE_RING_PRIORITY_LEN 3
static inline GearBonuses compute_slot_gear_bonuses(Player* p) {
    EquipmentBonuses eb;
    osrs_sum_equipment_bonuses(p->equipped, &eb);
    return osrs_gear_bonuses_from_equipment_bonuses(&eb);
}

static inline GearBonuses* get_slot_gear_bonuses(Player* p) {
    osrs_ensure_player_equipment(p);
    return &p->slot_cached_bonuses;
}

/**
 * Find best available item from a priority list in the player's inventory.
 * Returns ITEM_NONE if no item from the list is available.
 */
static inline uint8_t find_best_available(
    Player* p, int gear_slot,
    const uint8_t* priority, int priority_len
) {
    (void)gear_slot;
    for (int i = 0; i < priority_len; i++) {
        if (osrs_player_owns_item(p, priority[i])) {
            return priority[i];
        }
    }
    return ITEM_NONE;
}

/** Find best melee spec weapon available in weapon inventory. */
static inline uint8_t find_best_melee_spec(Player* p) {
    return find_best_available(p, GEAR_SLOT_WEAPON, MELEE_SPEC_PRIORITY, MELEE_SPEC_PRIORITY_LEN);
}

/** Find best ranged spec weapon available in weapon inventory. */
static inline uint8_t find_best_ranged_spec(Player* p) {
    return find_best_available(p, GEAR_SLOT_WEAPON, RANGE_SPEC_PRIORITY, RANGE_SPEC_PRIORITY_LEN);
}

/** Find best magic spec weapon available in weapon inventory. */
static inline uint8_t find_best_magic_spec(Player* p) {
    return find_best_available(p, GEAR_SLOT_WEAPON, MAGIC_SPEC_PRIORITY, MAGIC_SPEC_PRIORITY_LEN);
}

/** Check if player has granite maul in weapon inventory. */
static inline int player_has_gmaul(Player* p) {
    return osrs_player_owns_item(p, ITEM_GRANITE_MAUL);
}

/**
 * Resolve loadout for a given style from available inventory.
 *
 * Writes item indices to out[8] (one per dynamic gear slot).
 * Any slot without a matching item keeps its current equipment.
 *
 * @param p       Player (for inventory lookup)
 * @param loadout Style to resolve (MELEE/RANGE/MAGE/TANK/SPEC_*)
 * @param out     Output array of 8 item indices (NUM_DYNAMIC_GEAR_SLOTS)
 */
static inline void resolve_loadout(Player* p, int loadout, uint8_t out[NUM_DYNAMIC_GEAR_SLOTS]) {
    // Initialize all outputs to current equipment
    for (int i = 0; i < NUM_DYNAMIC_GEAR_SLOTS; i++) {
        out[i] = p->equipped[DYNAMIC_GEAR_SLOTS[i]];
    }

    // Slot order in DYNAMIC_GEAR_SLOTS: weapon(0), shield(1), body(2), legs(3),
    //                                    head(4), cape(5), neck(6), ring(7)

    switch (loadout) {
        case LOADOUT_MELEE: {
            uint8_t weapon = find_best_available(p, GEAR_SLOT_WEAPON, MELEE_WEAPON_PRIORITY, MELEE_WEAPON_PRIORITY_LEN);
            if (weapon != ITEM_NONE) out[0] = weapon;
            if (!item_is_two_handed(out[0])) {
                uint8_t shield = find_best_available(p, GEAR_SLOT_SHIELD, MELEE_SHIELD_PRIORITY, MELEE_SHIELD_PRIORITY_LEN);
                if (shield != ITEM_NONE) out[1] = shield;
            } else {
                out[1] = ITEM_NONE;
            }
            uint8_t body = find_best_available(p, GEAR_SLOT_BODY, TANK_BODY_PRIORITY, TANK_BODY_PRIORITY_LEN);
            if (body != ITEM_NONE) out[2] = body;
            uint8_t legs = find_best_available(p, GEAR_SLOT_LEGS, TANK_LEGS_PRIORITY, TANK_LEGS_PRIORITY_LEN);
            if (legs != ITEM_NONE) out[3] = legs;
            uint8_t head = find_best_available(p, GEAR_SLOT_HEAD, TANK_HEAD_PRIORITY, TANK_HEAD_PRIORITY_LEN);
            if (head != ITEM_NONE) out[4] = head;
            uint8_t cape = find_best_available(p, GEAR_SLOT_CAPE, MELEE_CAPE_PRIORITY, MELEE_CAPE_PRIORITY_LEN);
            if (cape != ITEM_NONE) out[5] = cape;
            uint8_t neck = find_best_available(p, GEAR_SLOT_NECK, MELEE_NECK_PRIORITY, MELEE_NECK_PRIORITY_LEN);
            if (neck != ITEM_NONE) out[6] = neck;
            uint8_t ring = find_best_available(p, GEAR_SLOT_RING, MELEE_RING_PRIORITY, MELEE_RING_PRIORITY_LEN);
            if (ring != ITEM_NONE) out[7] = ring;
            break;
        }
        case LOADOUT_RANGE: {
            uint8_t weapon = find_best_available(p, GEAR_SLOT_WEAPON, RANGE_WEAPON_PRIORITY, RANGE_WEAPON_PRIORITY_LEN);
            if (weapon != ITEM_NONE) out[0] = weapon;
            uint8_t shield = find_best_available(p, GEAR_SLOT_SHIELD, TANK_SHIELD_PRIORITY, TANK_SHIELD_PRIORITY_LEN);
            if (shield != ITEM_NONE) out[1] = shield;
            uint8_t body = find_best_available(p, GEAR_SLOT_BODY, TANK_BODY_PRIORITY, TANK_BODY_PRIORITY_LEN);
            if (body != ITEM_NONE) out[2] = body;
            uint8_t legs = find_best_available(p, GEAR_SLOT_LEGS, TANK_LEGS_PRIORITY, TANK_LEGS_PRIORITY_LEN);
            if (legs != ITEM_NONE) out[3] = legs;
            uint8_t head = find_best_available(p, GEAR_SLOT_HEAD, TANK_HEAD_PRIORITY, TANK_HEAD_PRIORITY_LEN);
            if (head != ITEM_NONE) out[4] = head;
            uint8_t cape = find_best_available(p, GEAR_SLOT_CAPE, MAGE_CAPE_PRIORITY, MAGE_CAPE_PRIORITY_LEN);
            if (cape != ITEM_NONE) out[5] = cape;
            uint8_t neck = find_best_available(p, GEAR_SLOT_NECK, MELEE_NECK_PRIORITY, MELEE_NECK_PRIORITY_LEN);
            if (neck != ITEM_NONE) out[6] = neck;
            uint8_t ring = find_best_available(p, GEAR_SLOT_RING, MAGE_RING_PRIORITY, MAGE_RING_PRIORITY_LEN);
            if (ring != ITEM_NONE) out[7] = ring;
            break;
        }
        case LOADOUT_MAGE:
        case LOADOUT_TANK: {
            uint8_t weapon = find_best_available(p, GEAR_SLOT_WEAPON, MAGE_WEAPON_PRIORITY, MAGE_WEAPON_PRIORITY_LEN);
            if (weapon != ITEM_NONE) out[0] = weapon;

            if (loadout == LOADOUT_MAGE) {
                uint8_t shield = find_best_available(p, GEAR_SLOT_SHIELD, MAGE_SHIELD_PRIORITY, MAGE_SHIELD_PRIORITY_LEN);
                if (shield != ITEM_NONE) out[1] = shield;
                uint8_t body = find_best_available(p, GEAR_SLOT_BODY, MAGE_BODY_PRIORITY, MAGE_BODY_PRIORITY_LEN);
                if (body != ITEM_NONE) out[2] = body;
                uint8_t legs = find_best_available(p, GEAR_SLOT_LEGS, MAGE_LEGS_PRIORITY, MAGE_LEGS_PRIORITY_LEN);
                if (legs != ITEM_NONE) out[3] = legs;
                uint8_t head = find_best_available(p, GEAR_SLOT_HEAD, MAGE_HEAD_PRIORITY, MAGE_HEAD_PRIORITY_LEN);
                if (head != ITEM_NONE) out[4] = head;
                uint8_t neck = find_best_available(p, GEAR_SLOT_NECK, MAGE_NECK_PRIORITY, MAGE_NECK_PRIORITY_LEN);
                if (neck != ITEM_NONE) out[6] = neck;
            } else {
                uint8_t shield = find_best_available(p, GEAR_SLOT_SHIELD, TANK_SHIELD_PRIORITY, TANK_SHIELD_PRIORITY_LEN);
                if (shield != ITEM_NONE) out[1] = shield;
                uint8_t body = find_best_available(p, GEAR_SLOT_BODY, TANK_BODY_PRIORITY, TANK_BODY_PRIORITY_LEN);
                if (body != ITEM_NONE) out[2] = body;
                uint8_t legs = find_best_available(p, GEAR_SLOT_LEGS, TANK_LEGS_PRIORITY, TANK_LEGS_PRIORITY_LEN);
                if (legs != ITEM_NONE) out[3] = legs;
                uint8_t head = find_best_available(p, GEAR_SLOT_HEAD, TANK_HEAD_PRIORITY, TANK_HEAD_PRIORITY_LEN);
                if (head != ITEM_NONE) out[4] = head;
                uint8_t neck = find_best_available(p, GEAR_SLOT_NECK, MELEE_NECK_PRIORITY, MELEE_NECK_PRIORITY_LEN);
                if (neck != ITEM_NONE) out[6] = neck;
            }

            uint8_t cape = find_best_available(p, GEAR_SLOT_CAPE, MAGE_CAPE_PRIORITY, MAGE_CAPE_PRIORITY_LEN);
            if (cape != ITEM_NONE) out[5] = cape;
            uint8_t ring = find_best_available(p, GEAR_SLOT_RING, MAGE_RING_PRIORITY, MAGE_RING_PRIORITY_LEN);
            if (ring != ITEM_NONE) out[7] = ring;
            break;
        }
        case LOADOUT_SPEC_MELEE: {
            uint8_t weapon = find_best_melee_spec(p);
            if (weapon != ITEM_NONE) out[0] = weapon;
            if (!item_is_two_handed(out[0])) {
                uint8_t shield = find_best_available(p, GEAR_SLOT_SHIELD, MELEE_SHIELD_PRIORITY, MELEE_SHIELD_PRIORITY_LEN);
                if (shield != ITEM_NONE) out[1] = shield;
            } else {
                out[1] = ITEM_NONE;
            }
            uint8_t body = find_best_available(p, GEAR_SLOT_BODY, TANK_BODY_PRIORITY, TANK_BODY_PRIORITY_LEN);
            if (body != ITEM_NONE) out[2] = body;
            uint8_t legs = find_best_available(p, GEAR_SLOT_LEGS, TANK_LEGS_PRIORITY, TANK_LEGS_PRIORITY_LEN);
            if (legs != ITEM_NONE) out[3] = legs;
            uint8_t head = find_best_available(p, GEAR_SLOT_HEAD, TANK_HEAD_PRIORITY, TANK_HEAD_PRIORITY_LEN);
            if (head != ITEM_NONE) out[4] = head;
            uint8_t cape = find_best_available(p, GEAR_SLOT_CAPE, MELEE_CAPE_PRIORITY, MELEE_CAPE_PRIORITY_LEN);
            if (cape != ITEM_NONE) out[5] = cape;
            uint8_t neck = find_best_available(p, GEAR_SLOT_NECK, MELEE_NECK_PRIORITY, MELEE_NECK_PRIORITY_LEN);
            if (neck != ITEM_NONE) out[6] = neck;
            uint8_t ring = find_best_available(p, GEAR_SLOT_RING, MELEE_RING_PRIORITY, MELEE_RING_PRIORITY_LEN);
            if (ring != ITEM_NONE) out[7] = ring;
            break;
        }
        case LOADOUT_SPEC_RANGE: {
            uint8_t weapon = find_best_ranged_spec(p);
            if (weapon != ITEM_NONE) out[0] = weapon;
            if (!item_is_two_handed(out[0])) {
                uint8_t shield = find_best_available(p, GEAR_SLOT_SHIELD, TANK_SHIELD_PRIORITY, TANK_SHIELD_PRIORITY_LEN);
                if (shield != ITEM_NONE) out[1] = shield;
            } else {
                out[1] = ITEM_NONE;
            }
            uint8_t body = find_best_available(p, GEAR_SLOT_BODY, TANK_BODY_PRIORITY, TANK_BODY_PRIORITY_LEN);
            if (body != ITEM_NONE) out[2] = body;
            uint8_t legs = find_best_available(p, GEAR_SLOT_LEGS, TANK_LEGS_PRIORITY, TANK_LEGS_PRIORITY_LEN);
            if (legs != ITEM_NONE) out[3] = legs;
            uint8_t head = find_best_available(p, GEAR_SLOT_HEAD, TANK_HEAD_PRIORITY, TANK_HEAD_PRIORITY_LEN);
            if (head != ITEM_NONE) out[4] = head;
            uint8_t cape = find_best_available(p, GEAR_SLOT_CAPE, MAGE_CAPE_PRIORITY, MAGE_CAPE_PRIORITY_LEN);
            if (cape != ITEM_NONE) out[5] = cape;
            uint8_t neck = find_best_available(p, GEAR_SLOT_NECK, MELEE_NECK_PRIORITY, MELEE_NECK_PRIORITY_LEN);
            if (neck != ITEM_NONE) out[6] = neck;
            uint8_t ring = find_best_available(p, GEAR_SLOT_RING, MELEE_RING_PRIORITY, MELEE_RING_PRIORITY_LEN);
            if (ring != ITEM_NONE) out[7] = ring;
            break;
        }
        case LOADOUT_SPEC_MAGIC: {
            uint8_t weapon = find_best_magic_spec(p);
            if (weapon != ITEM_NONE) out[0] = weapon;
            uint8_t shield = find_best_available(p, GEAR_SLOT_SHIELD, MAGE_SHIELD_PRIORITY, MAGE_SHIELD_PRIORITY_LEN);
            if (shield != ITEM_NONE) out[1] = shield;
            uint8_t body = find_best_available(p, GEAR_SLOT_BODY, MAGE_BODY_PRIORITY, MAGE_BODY_PRIORITY_LEN);
            if (body != ITEM_NONE) out[2] = body;
            uint8_t legs = find_best_available(p, GEAR_SLOT_LEGS, MAGE_LEGS_PRIORITY, MAGE_LEGS_PRIORITY_LEN);
            if (legs != ITEM_NONE) out[3] = legs;
            uint8_t head = find_best_available(p, GEAR_SLOT_HEAD, MAGE_HEAD_PRIORITY, MAGE_HEAD_PRIORITY_LEN);
            if (head != ITEM_NONE) out[4] = head;
            uint8_t cape = find_best_available(p, GEAR_SLOT_CAPE, MAGE_CAPE_PRIORITY, MAGE_CAPE_PRIORITY_LEN);
            if (cape != ITEM_NONE) out[5] = cape;
            uint8_t neck = find_best_available(p, GEAR_SLOT_NECK, MAGE_NECK_PRIORITY, MAGE_NECK_PRIORITY_LEN);
            if (neck != ITEM_NONE) out[6] = neck;
            uint8_t ring = find_best_available(p, GEAR_SLOT_RING, MAGE_RING_PRIORITY, MAGE_RING_PRIORITY_LEN);
            if (ring != ITEM_NONE) out[7] = ring;
            break;
        }
        case LOADOUT_GMAUL: {
            if (player_has_gmaul(p)) {
                out[0] = ITEM_GRANITE_MAUL;
                out[1] = ITEM_NONE;
            }
            break;
        }
        default:
            break;
    }
}

static inline int pvp_loadout_can_arm_spec(Player* p, int loadout) {
    switch (loadout) {
        case LOADOUT_SPEC_MELEE:
            return find_best_melee_spec(p) != ITEM_NONE;
        case LOADOUT_SPEC_RANGE:
            return find_best_ranged_spec(p) != ITEM_NONE;
        case LOADOUT_SPEC_MAGIC:
            return find_best_magic_spec(p) != ITEM_NONE;
        case LOADOUT_GMAUL:
            return player_has_gmaul(p);
        default:
            return 0;
    }
}

/**
 * Apply a loadout to a player using dynamic resolution.
 * Returns number of slots that actually changed.
 */
static inline int apply_loadout(Player* p, int loadout) {
    if (loadout <= LOADOUT_KEEP || loadout > LOADOUT_GMAUL) return 0;

    uint8_t resolved[NUM_DYNAMIC_GEAR_SLOTS];
    resolve_loadout(p, loadout, resolved);

    int changed = 0;
    for (int i = 0; i < NUM_DYNAMIC_GEAR_SLOTS; i++) {
        uint8_t item = resolved[i];
        int gear_slot = DYNAMIC_GEAR_SLOTS[i];
        if (item == ITEM_NONE) {
            if (p->equipped[gear_slot] != ITEM_NONE) {
                p->equipped[gear_slot] = ITEM_NONE;
                p->slot_gear_dirty = 1;
                changed++;
            }
            continue;
        }
        if (p->equipped[gear_slot] == item) continue;
        if (!osrs_player_equip_inventory_item(p, item)) {
            fprintf(stderr, "apply_loadout: item %u is not owned\n", (unsigned)item);
            abort();
        }
        changed++;
    }
    if (changed > 0)
        osrs_refresh_player_equipment(p);

    return changed;
}

/**
 * Check if current equipment matches a resolved loadout.
 */
static inline int is_loadout_active(Player* p, int loadout) {
    if (loadout <= LOADOUT_KEEP || loadout > LOADOUT_GMAUL) return 0;

    uint8_t resolved[NUM_DYNAMIC_GEAR_SLOTS];
    resolve_loadout(p, loadout, resolved);

    for (int i = 0; i < NUM_DYNAMIC_GEAR_SLOTS; i++) {
        int gear_slot = DYNAMIC_GEAR_SLOTS[i];
        if (p->equipped[gear_slot] != resolved[i]) return 0;
    }
    return 1;
}

/**
 * Get current active loadout (1-8), or 0 if no loadout matches.
 */
static inline int get_current_loadout(Player* p) {
    for (int l = 1; l <= LOADOUT_GMAUL; l++) {
        if (is_loadout_active(p, l)) return l;
    }
    return 0;
}

/** Visible GearSet for each loadout (actual damage type, no GEAR_SPEC). */
static inline GearSet loadout_to_gear_set(int loadout) {
    switch (loadout) {
        case LOADOUT_MELEE:      return GEAR_MELEE;
        case LOADOUT_RANGE:      return GEAR_RANGED;
        case LOADOUT_MAGE:       return GEAR_MAGE;
        case LOADOUT_TANK:       return GEAR_TANK;
        case LOADOUT_SPEC_MELEE: return GEAR_MELEE;
        case LOADOUT_SPEC_RANGE: return GEAR_RANGED;
        case LOADOUT_SPEC_MAGIC: return GEAR_MAGE;
        case LOADOUT_GMAUL:      return GEAR_MELEE;
        default:                 return GEAR_MELEE;
    }
}

/** Get attack style for currently equipped weapon. */
static inline AttackStyle get_slot_weapon_attack_style(Player* p) {
    return osrs_player_weapon_attack_style(p);
}

/**
 * Initialize basic LMS equipment (tier 0).
 * Sets equipped[] and inventory[] arrays for the basic loadout.
 */
static inline void init_slot_equipment_lms(Player* p) {
    memset(p->equipped, ITEM_NONE, sizeof(p->equipped));
    osrs_player_inventory_clear(p);

    p->equipped[GEAR_SLOT_HEAD] = ITEM_HELM_NEITIZNOT;
    p->equipped[GEAR_SLOT_CAPE] = ITEM_GOD_CAPE;
    p->equipped[GEAR_SLOT_NECK] = ITEM_GLORY;
    p->equipped[GEAR_SLOT_AMMO] = ITEM_DIAMOND_BOLTS_E;
    p->equipped[GEAR_SLOT_WEAPON] = ITEM_WHIP;
    p->equipped[GEAR_SLOT_SHIELD] = ITEM_DRAGON_DEFENDER;
    p->equipped[GEAR_SLOT_BODY] = ITEM_BLACK_DHIDE_BODY;
    p->equipped[GEAR_SLOT_LEGS] = ITEM_RUNE_PLATELEGS;
    p->equipped[GEAR_SLOT_HANDS] = ITEM_BARROWS_GLOVES;
    p->equipped[GEAR_SLOT_FEET] = ITEM_CLIMBING_BOOTS;
    p->equipped[GEAR_SLOT_RING] = ITEM_BERSERKER_RING;
    osrs_player_refresh_weapon_state(p, p->equipped[GEAR_SLOT_WEAPON]);

    osrs_player_inventory_add(p, ITEM_RUNE_CROSSBOW);
    osrs_player_inventory_add(p, ITEM_AHRIM_STAFF);
    osrs_player_inventory_add(p, ITEM_DRAGON_DAGGER);
    osrs_player_inventory_add(p, ITEM_SPIRIT_SHIELD);
    osrs_player_inventory_add(p, ITEM_MYSTIC_TOP);
    osrs_player_inventory_add(p, ITEM_MYSTIC_BOTTOM);

    osrs_refresh_player_equipment(p);
    p->current_gear = GEAR_MELEE;
}

static inline int add_item_to_inventory(Player* p, int gear_slot, uint8_t item_idx) {
    (void)gear_slot;
    if (osrs_player_owns_item(p, item_idx)) return 0;
    return osrs_player_inventory_add(p, item_idx) >= 0;
}

// Maps each loot item to the basic item it replaces (ITEM_NONE = doesn't replace)
static const uint8_t UPGRADE_REPLACES[NUM_ITEMS] = {
    [ITEM_HELM_NEITIZNOT]       = ITEM_NONE,
    [ITEM_GOD_CAPE]             = ITEM_NONE,
    [ITEM_GLORY]                = ITEM_NONE,
    [ITEM_BLACK_DHIDE_BODY]     = ITEM_NONE,
    [ITEM_MYSTIC_TOP]           = ITEM_NONE,
    [ITEM_RUNE_PLATELEGS]       = ITEM_NONE,
    [ITEM_MYSTIC_BOTTOM]        = ITEM_NONE,
    [ITEM_WHIP]                 = ITEM_NONE,
    [ITEM_RUNE_CROSSBOW]        = ITEM_NONE,
    [ITEM_AHRIM_STAFF]          = ITEM_NONE,
    [ITEM_DRAGON_DAGGER]        = ITEM_NONE,
    [ITEM_DRAGON_DEFENDER]      = ITEM_NONE,
    [ITEM_SPIRIT_SHIELD]        = ITEM_NONE,
    [ITEM_BARROWS_GLOVES]       = ITEM_NONE,
    [ITEM_CLIMBING_BOOTS]       = ITEM_NONE,
    [ITEM_BERSERKER_RING]       = ITEM_NONE,
    [ITEM_DIAMOND_BOLTS_E]      = ITEM_NONE,
    // Weapons
    [ITEM_GHRAZI_RAPIER]        = ITEM_WHIP,
    [ITEM_INQUISITORS_MACE]     = ITEM_WHIP,
    [ITEM_STAFF_OF_DEAD]        = ITEM_AHRIM_STAFF,
    [ITEM_KODAI_WAND]           = ITEM_AHRIM_STAFF,
    [ITEM_VOLATILE_STAFF]       = ITEM_AHRIM_STAFF,
    [ITEM_ZURIELS_STAFF]        = ITEM_AHRIM_STAFF,
    [ITEM_ARMADYL_CROSSBOW]     = ITEM_RUNE_CROSSBOW,
    [ITEM_ZARYTE_CROSSBOW]      = ITEM_RUNE_CROSSBOW,
    [ITEM_DRAGON_CLAWS]         = ITEM_DRAGON_DAGGER,
    [ITEM_AGS]                  = ITEM_DRAGON_DAGGER,
    [ITEM_ANCIENT_GS]           = ITEM_DRAGON_DAGGER,
    [ITEM_GRANITE_MAUL]         = ITEM_NONE,
    [ITEM_ELDER_MAUL]           = ITEM_WHIP,
    [ITEM_DARK_BOW]             = ITEM_NONE,
    [ITEM_HEAVY_BALLISTA]       = ITEM_NONE,
    [ITEM_VESTAS]               = ITEM_DRAGON_DAGGER,
    [ITEM_VOIDWAKER]            = ITEM_DRAGON_DAGGER,
    [ITEM_STATIUS_WARHAMMER]    = ITEM_DRAGON_DAGGER,
    [ITEM_MORRIGANS_JAVELIN]    = ITEM_RUNE_CROSSBOW,
    // Armor and accessories
    [ITEM_ANCESTRAL_HAT]        = ITEM_NONE,
    [ITEM_ANCESTRAL_TOP]        = ITEM_MYSTIC_TOP,
    [ITEM_ANCESTRAL_BOTTOM]     = ITEM_MYSTIC_BOTTOM,
    [ITEM_AHRIMS_ROBETOP]       = ITEM_MYSTIC_TOP,
    [ITEM_AHRIMS_ROBESKIRT]     = ITEM_MYSTIC_BOTTOM,
    [ITEM_KARILS_TOP]           = ITEM_BLACK_DHIDE_BODY,
    [ITEM_BANDOS_TASSETS]       = ITEM_RUNE_PLATELEGS,
    [ITEM_BLESSED_SPIRIT_SHIELD]= ITEM_SPIRIT_SHIELD,
    [ITEM_FURY]                 = ITEM_GLORY,
    [ITEM_OCCULT_NECKLACE]      = ITEM_NONE,
    [ITEM_INFERNAL_CAPE]        = ITEM_NONE,
    [ITEM_ETERNAL_BOOTS]        = ITEM_CLIMBING_BOOTS,
    [ITEM_SEERS_RING_I]         = ITEM_NONE,
    [ITEM_LIGHTBEARER]          = ITEM_NONE,
    [ITEM_MAGES_BOOK]           = ITEM_NONE,
    [ITEM_DRAGON_ARROWS]        = ITEM_NONE,
    // Barrows armor
    [ITEM_TORAGS_PLATELEGS]     = ITEM_RUNE_PLATELEGS,
    [ITEM_DHAROKS_PLATELEGS]    = ITEM_RUNE_PLATELEGS,
    [ITEM_VERACS_PLATESKIRT]    = ITEM_RUNE_PLATELEGS,
    [ITEM_TORAGS_HELM]          = ITEM_HELM_NEITIZNOT,
    [ITEM_DHAROKS_HELM]         = ITEM_HELM_NEITIZNOT,
    [ITEM_VERACS_HELM]          = ITEM_HELM_NEITIZNOT,
    [ITEM_GUTHANS_HELM]         = ITEM_HELM_NEITIZNOT,
    [ITEM_OPAL_DRAGON_BOLTS]    = ITEM_NONE,  // conditional, handled in add_loot_item
};

static inline int remove_item_from_inventory(Player* p, int gear_slot, uint8_t item_idx) {
    (void)gear_slot;
    if (osrs_player_inventory_remove_item(p, item_idx)) return 1;
    for (int s = 0; s < NUM_GEAR_SLOTS; s++) {
        if (p->equipped[s] == item_idx) {
            p->equipped[s] = ITEM_NONE;
            if (s == GEAR_SLOT_WEAPON)
                osrs_player_refresh_weapon_state(p, ITEM_NONE);
            p->slot_gear_dirty = 1;
            osrs_refresh_player_equipment(p);
            return 1;
        }
    }
    return 0;
}

static inline int item_to_gear_slot(uint8_t item_idx) {
    return osrs_item_gear_slot(item_idx);
}

// Chain upgrades: loot items that also obsolete other loot items.
// UPGRADE_REPLACES handles basic→loot, these handle loot→loot chains.
// {new_item, obsolete_item} — when new_item is added, obsolete_item is dropped.
static const uint8_t CHAIN_REPLACES[][2] = {
    // VLS is a better primary melee weapon than whip
    { ITEM_VESTAS, ITEM_WHIP },
    // Zuriel's is strictly better than SotD and volatile
    { ITEM_ZURIELS_STAFF, ITEM_STAFF_OF_DEAD },
    { ITEM_ZURIELS_STAFF, ITEM_VOLATILE_STAFF },
    // Kodai is the best mage weapon — replaces all lesser mage weapons
    { ITEM_KODAI_WAND, ITEM_STAFF_OF_DEAD },
    { ITEM_KODAI_WAND, ITEM_VOLATILE_STAFF },
    { ITEM_KODAI_WAND, ITEM_ZURIELS_STAFF },
    // Volatile replaces SotD (both are magic weapons, volatile has spec)
    { ITEM_VOLATILE_STAFF, ITEM_STAFF_OF_DEAD },
    // ZCB is strictly better than ACB
    { ITEM_ZARYTE_CROSSBOW, ITEM_ARMADYL_CROSSBOW },
    // Morr javelin is the best ranged weapon — replaces all lesser ranged weapons
    { ITEM_MORRIGANS_JAVELIN, ITEM_ZARYTE_CROSSBOW },
    { ITEM_MORRIGANS_JAVELIN, ITEM_ARMADYL_CROSSBOW },
    { ITEM_MORRIGANS_JAVELIN, ITEM_HEAVY_BALLISTA },
    { ITEM_MORRIGANS_JAVELIN, ITEM_DARK_BOW },
    // ZCB replaces ballista and dark bow
    { ITEM_ZARYTE_CROSSBOW, ITEM_HEAVY_BALLISTA },
    { ITEM_ZARYTE_CROSSBOW, ITEM_DARK_BOW },
    // ACB replaces ballista and dark bow
    { ITEM_ARMADYL_CROSSBOW, ITEM_HEAVY_BALLISTA },
    { ITEM_ARMADYL_CROSSBOW, ITEM_DARK_BOW },
    // Ancestral is strictly better than Ahrim's
    { ITEM_ANCESTRAL_TOP, ITEM_AHRIMS_ROBETOP },
    { ITEM_ANCESTRAL_BOTTOM, ITEM_AHRIMS_ROBESKIRT },
    // Bandos tassets replaces all barrows legs
    { ITEM_BANDOS_TASSETS, ITEM_TORAGS_PLATELEGS },
    { ITEM_BANDOS_TASSETS, ITEM_DHAROKS_PLATELEGS },
    { ITEM_BANDOS_TASSETS, ITEM_VERACS_PLATESKIRT },
    // Rapier and inq mace are equivalent; rapier preferred, replaces inq mace
    { ITEM_GHRAZI_RAPIER, ITEM_INQUISITORS_MACE },
    // Rapier/inq mace/elder maul all replace whip as primary
    { ITEM_GHRAZI_RAPIER, ITEM_WHIP },
    { ITEM_INQUISITORS_MACE, ITEM_WHIP },
    { ITEM_ELDER_MAUL, ITEM_WHIP },
    // Rapier/inq mace replace elder maul (4-tick > 6-tick for primary DPS)
    { ITEM_GHRAZI_RAPIER, ITEM_ELDER_MAUL },
    { ITEM_INQUISITORS_MACE, ITEM_ELDER_MAUL },
    // VLS replaces all lesser melee primaries
    { ITEM_VESTAS, ITEM_ELDER_MAUL },
    { ITEM_VESTAS, ITEM_GHRAZI_RAPIER },
    { ITEM_VESTAS, ITEM_INQUISITORS_MACE },
    // Voidwaker replaces all lesser melee weapons (best spec + solid primary)
    { ITEM_VOIDWAKER, ITEM_WHIP },
    { ITEM_VOIDWAKER, ITEM_GHRAZI_RAPIER },
    { ITEM_VOIDWAKER, ITEM_INQUISITORS_MACE },
    { ITEM_VOIDWAKER, ITEM_ELDER_MAUL },
    // SWH replaces everything below it: primary + spec in one weapon
    { ITEM_STATIUS_WARHAMMER, ITEM_WHIP },
    { ITEM_STATIUS_WARHAMMER, ITEM_GHRAZI_RAPIER },
    { ITEM_STATIUS_WARHAMMER, ITEM_INQUISITORS_MACE },
    { ITEM_STATIUS_WARHAMMER, ITEM_ELDER_MAUL },
    { ITEM_STATIUS_WARHAMMER, ITEM_AGS },
    { ITEM_STATIUS_WARHAMMER, ITEM_ANCIENT_GS },
    { ITEM_STATIUS_WARHAMMER, ITEM_DRAGON_CLAWS },
    // Godswords/claws replace whip (strong enough as primary despite 6-tick)
    { ITEM_AGS, ITEM_WHIP },
    { ITEM_ANCIENT_GS, ITEM_WHIP },
    // Ancient GS > AGS > claws for mid-tier melee spec
    { ITEM_ANCIENT_GS, ITEM_AGS },
    { ITEM_ANCIENT_GS, ITEM_DRAGON_CLAWS },
    { ITEM_AGS, ITEM_DRAGON_CLAWS },
    // Lightbearer replaces seers ring (spec regen universally useful)
    { ITEM_LIGHTBEARER, ITEM_SEERS_RING_I },
    // Barrows helms: only keep the best one (torag > guthan > verac > dharok)
    { ITEM_TORAGS_HELM, ITEM_GUTHANS_HELM },
    { ITEM_TORAGS_HELM, ITEM_VERACS_HELM },
    { ITEM_TORAGS_HELM, ITEM_DHAROKS_HELM },
    { ITEM_GUTHANS_HELM, ITEM_VERACS_HELM },
    { ITEM_GUTHANS_HELM, ITEM_DHAROKS_HELM },
    { ITEM_VERACS_HELM, ITEM_DHAROKS_HELM },
};
#define CHAIN_REPLACES_LEN (sizeof(CHAIN_REPLACES) / sizeof(CHAIN_REPLACES[0]))

/**
 * Add a loot item with upgrade replacement logic.
 *
 * 1. UPGRADE_REPLACES: removes the basic item this loot replaces
 * 2. CHAIN_REPLACES: removes lesser loot items made obsolete by this one
 * 3. Crossbow bolt trigger: ACB/ZCB + opal bolts → swap diamond bolts
 */
static inline void add_loot_item(Player* p, uint8_t item_idx) {
    int gear_slot = item_to_gear_slot(item_idx);
    if (gear_slot < 0) return;

    // Reverse chain check: if a strictly better item already exists, skip this one
    for (int i = 0; i < (int)CHAIN_REPLACES_LEN; i++) {
        if (CHAIN_REPLACES[i][1] == item_idx) {
            uint8_t better = CHAIN_REPLACES[i][0];
            int better_slot = item_to_gear_slot(better);
            if (better_slot >= 0 && osrs_player_owns_item(p, better)) {
                return;
            }
        }
    }

    // Primary replacement: new loot replaces a basic item
    uint8_t replaces = UPGRADE_REPLACES[item_idx];
    if (replaces != ITEM_NONE) {
        int replace_slot = item_to_gear_slot(replaces);
        if (replace_slot >= 0) {
            remove_item_from_inventory(p, replace_slot, replaces);
        }
    }

    // Chain replacement: new loot also obsoletes lesser loot items
    for (int i = 0; i < (int)CHAIN_REPLACES_LEN; i++) {
        if (CHAIN_REPLACES[i][0] == item_idx) {
            uint8_t obsolete = CHAIN_REPLACES[i][1];
            int obs_slot = item_to_gear_slot(obsolete);
            if (obs_slot >= 0) {
                remove_item_from_inventory(p, obs_slot, obsolete);
            }
        }
    }

    add_item_to_inventory(p, gear_slot, item_idx);

    // Crossbow bolt trigger: ACB/ZCB + opal bolts in inventory → swap bolts
    if ((item_idx == ITEM_ARMADYL_CROSSBOW || item_idx == ITEM_ZARYTE_CROSSBOW)
        && osrs_player_owns_item(p, ITEM_OPAL_DRAGON_BOLTS)) {
        remove_item_from_inventory(p, GEAR_SLOT_AMMO, ITEM_DIAMOND_BOLTS_E);
        p->equipped[GEAR_SLOT_AMMO] = ITEM_OPAL_DRAGON_BOLTS;
    }

}
// DYNAMIC FOOD COUNT (28-slot inventory model)

#define FIXED_INVENTORY_SLOTS 11  // 4 brews + 2 restores + 1 combat + 1 ranged + 2 karambwan + 1 rune pouch

/** Count switch items: items beyond the first in each gear slot. */
static inline int count_switch_items(Player* p) {
    return osrs_player_unequipped_gear_count(p);
}

/** Compute food count from 28-slot inventory model. */
static inline int compute_food_count(Player* p) {
    int switches = count_switch_items(p);
    int food = 28 - FIXED_INVENTORY_SLOTS - switches;
    return food > 1 ? food : 1;
}

static const uint8_t CHEST_LOOT[] = {
    ITEM_DRAGON_CLAWS, ITEM_AGS, ITEM_ANCIENT_GS, ITEM_GRANITE_MAUL,
    ITEM_VOLATILE_STAFF, ITEM_ZARYTE_CROSSBOW, ITEM_ARMADYL_CROSSBOW,
    ITEM_DARK_BOW, ITEM_GHRAZI_RAPIER, ITEM_INQUISITORS_MACE,
    ITEM_KODAI_WAND, ITEM_STAFF_OF_DEAD, ITEM_ELDER_MAUL,
    ITEM_HEAVY_BALLISTA, ITEM_OCCULT_NECKLACE, ITEM_INFERNAL_CAPE,
    ITEM_SEERS_RING_I, ITEM_MAGES_BOOK,
    ITEM_ANCESTRAL_HAT, ITEM_ANCESTRAL_TOP, ITEM_ANCESTRAL_BOTTOM,
    ITEM_AHRIMS_ROBETOP, ITEM_AHRIMS_ROBESKIRT, ITEM_KARILS_TOP,
    ITEM_BANDOS_TASSETS, ITEM_BLESSED_SPIRIT_SHIELD,
    ITEM_FURY, ITEM_ETERNAL_BOOTS,
    ITEM_TORAGS_PLATELEGS, ITEM_DHAROKS_PLATELEGS, ITEM_VERACS_PLATESKIRT,
    ITEM_TORAGS_HELM, ITEM_DHAROKS_HELM, ITEM_VERACS_HELM, ITEM_GUTHANS_HELM,
    ITEM_OPAL_DRAGON_BOLTS,
};
#define CHEST_LOOT_LEN 36

static const uint8_t BLOODIER_LOOT[] = {
    ITEM_VESTAS, ITEM_VOIDWAKER, ITEM_STATIUS_WARHAMMER,
    ITEM_MORRIGANS_JAVELIN, ITEM_ZURIELS_STAFF, ITEM_LIGHTBEARER
};
#define BLOODIER_LOOT_LEN 6

/**
 * Initialize player gear for a given tier (randomized loot).
 *
 * Each chest = 2 rolls from a single combined loot pool.
 * Tier 0: basic LMS (17 items), no chests
 * Tier 1: basic + 1 own chest (2 rolls)
 * Tier 2: basic + 2 own chests + 1 killed player's chest (6 rolls)
 * Tier 3: basic + 2 own chests + 2 killed players' chests (8 rolls) + 1 bloodier key item
 *
 * Duplicates are handled by add_loot_item() (dedup + chain replacement).
 *
 * @param p    Player to initialize
 * @param tier Gear tier (0-3)
 * @param rng  RNG state pointer
 */
static inline void init_player_gear_randomized(Player* p, int tier, uint32_t* rng) {
    // Start with basic LMS loadout
    init_slot_equipment_lms(p);

    if (tier <= 0) return;

    // Helper: add a random item from a loot table with upgrade logic
    #define ADD_RANDOM_LOOT(table, len) do { \
        uint32_t _r = xorshift32(rng); \
        uint8_t _item = (table)[_r % (len)]; \
        add_loot_item(p, _item); \
    } while(0)

    // Tier 1: 1 own chest = 2 rolls
    if (tier >= 1) {
        ADD_RANDOM_LOOT(CHEST_LOOT, CHEST_LOOT_LEN);
        ADD_RANDOM_LOOT(CHEST_LOOT, CHEST_LOOT_LEN);
    }

    // Tier 2: 1 more own chest (2 rolls) + 1 killed player's chest (2 rolls)
    if (tier >= 2) {
        ADD_RANDOM_LOOT(CHEST_LOOT, CHEST_LOOT_LEN);
        ADD_RANDOM_LOOT(CHEST_LOOT, CHEST_LOOT_LEN);
        ADD_RANDOM_LOOT(CHEST_LOOT, CHEST_LOOT_LEN);
        ADD_RANDOM_LOOT(CHEST_LOOT, CHEST_LOOT_LEN);
    }

    // Tier 3: 1 more killed player's chest (2 rolls) + 1 bloodier key item
    if (tier >= 3) {
        ADD_RANDOM_LOOT(CHEST_LOOT, CHEST_LOOT_LEN);
        ADD_RANDOM_LOOT(CHEST_LOOT, CHEST_LOOT_LEN);
        ADD_RANDOM_LOOT(BLOODIER_LOOT, BLOODIER_LOOT_LEN);
    }

    #undef ADD_RANDOM_LOOT

    // Tier 3 only: drop defender if no 1-handed melee weapon exists.
    // At lower tiers future loot might add a 1H melee (VLS, SWH, voidwaker).
    if (tier >= 3 && osrs_player_owns_item(p, ITEM_DRAGON_DEFENDER)) {
        int has_1h_melee = 0;
        for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) {
            uint8_t w = p->inventory[i];
            if (w == ITEM_NONE) continue;
            if (osrs_item_gear_slot(w) != GEAR_SLOT_WEAPON) continue;
            if (get_item_attack_style(w) == ATTACK_STYLE_MELEE && !item_is_two_handed(w)) {
                has_1h_melee = 1;
                break;
            }
        }
        uint8_t equipped_weapon = p->equipped[GEAR_SLOT_WEAPON];
        if (equipped_weapon < NUM_ITEMS &&
                get_item_attack_style(equipped_weapon) == ATTACK_STYLE_MELEE &&
                !item_is_two_handed(equipped_weapon)) {
            has_1h_melee = 1;
        }
        if (!has_1h_melee) {
            remove_item_from_inventory(p, GEAR_SLOT_SHIELD, ITEM_DRAGON_DEFENDER);
        }
    }

    // Re-resolve starting equipment in melee loadout
    uint8_t resolved[NUM_DYNAMIC_GEAR_SLOTS];
    resolve_loadout(p, LOADOUT_MELEE, resolved);
    for (int i = 0; i < NUM_DYNAMIC_GEAR_SLOTS; i++) {
        uint8_t item = resolved[i];
        if (item == ITEM_NONE) {
            p->equipped[DYNAMIC_GEAR_SLOTS[i]] = ITEM_NONE;
        } else if (p->equipped[DYNAMIC_GEAR_SLOTS[i]] != item &&
                !osrs_player_equip_inventory_item(p, item)) {
            fprintf(stderr, "init_player_gear_randomized: item %u is not owned\n",
                (unsigned)item);
            abort();
        }
    }

    osrs_refresh_player_equipment(p);
    p->current_gear = GEAR_MELEE;
}

/**
 * Sample gear tier from weights using RNG.
 * Returns tier 0-3.
 */
static inline int sample_gear_tier(float weights[4], uint32_t* rng) {
    float r = (float)xorshift32(rng) / (float)UINT32_MAX;
    float cumulative = 0.0f;
    for (int i = 0; i < 4; i++) {
        cumulative += weights[i];
        if (r < cumulative) return i;
    }
    return 0;
}

#endif // OSRS_PVP_GEAR_H
