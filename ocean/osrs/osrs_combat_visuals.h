/**
 * @file osrs_combat_visuals.h
 * @brief Item-keyed combat visual lookup shared by OSRS renderers.
 *
 * RuneC exports combat visuals keyed by canonical OSRS item ids. This table keeps
 * the same boundary while preserving the current renderer's animation choices.
 */

#ifndef OSRS_COMBAT_VISUALS_H
#define OSRS_COMBAT_VISUALS_H

#include "osrs_items.h"
#include "osrs_types.h"
#include <stdint.h>
#include <stddef.h>

enum {
    OSRS_COMBAT_VISUAL_STYLE_ANY = -1,
    OSRS_COMBAT_VISUAL_NO_ANIMATION = -1,
    OSRS_PLAYER_UNARMED_ATTACK_ANIM = 422,
    OSRS_PLAYER_POWERED_STAFF_ATTACK_ANIM = 1167,
};

typedef enum {
    OSRS_ITEM_ID_ABYSSAL_WHIP = 4151,
    OSRS_ITEM_ID_GRANITE_MAUL = 4153,
    OSRS_ITEM_ID_AHRIMS_STAFF = 4710,
    OSRS_ITEM_ID_DRAGON_DAGGER = 5698,
    OSRS_ITEM_ID_RUNE_CROSSBOW = 9185,
    OSRS_ITEM_ID_DARK_BOW = 11235,
    OSRS_ITEM_ID_STAFF_OF_THE_DEAD = 11791,
    OSRS_ITEM_ID_ARMADYL_CROSSBOW = 11785,
    OSRS_ITEM_ID_ARMADYL_GODSWORD = 11802,
    OSRS_ITEM_ID_MAGIC_SHORTBOW_I = 12788,
    OSRS_ITEM_ID_TRIDENT_OF_THE_SWAMP = 12899,
    OSRS_ITEM_ID_TOXIC_BLOWPIPE = 12926,
    OSRS_ITEM_ID_DRAGON_CLAWS = 13652,
    OSRS_ITEM_ID_ZURIELS_STAFF = 13867,
    OSRS_ITEM_ID_HEAVY_BALLISTA = 19481,
    OSRS_ITEM_ID_TWISTED_BOW = 20997,
    OSRS_ITEM_ID_ELDER_MAUL = 21003,
    OSRS_ITEM_ID_KODAI_WAND = 21006,
    OSRS_ITEM_ID_GHRAZI_RAPIER = 22324,
    OSRS_ITEM_ID_SANGUINESTI_STAFF = 22481,
    OSRS_ITEM_ID_VESTAS_LONGSWORD = 22613,
    OSRS_ITEM_ID_STATIUSS_WARHAMMER = 22622,
    OSRS_ITEM_ID_MORRIGANS_JAVELIN = 22636,
    OSRS_ITEM_ID_INQUISITORS_MACE = 24417,
    OSRS_ITEM_ID_VOLATILE_NIGHTMARE_STAFF = 24424,
    OSRS_ITEM_ID_BOW_OF_FAERDHINEN = 25865,
    OSRS_ITEM_ID_ANCIENT_GODSWORD = 26233,
    OSRS_ITEM_ID_ZARYTE_CROSSBOW = 26374,
    OSRS_ITEM_ID_VOIDWAKER = 27690,
    OSRS_ITEM_ID_DRAGON_HUNTER_WAND = 30070,
    OSRS_ITEM_ID_EYE_OF_AYAK = 31113,
} OsrsCombatVisualItemId;

typedef struct {
    uint16_t item_id;
    int8_t style;
    int16_t attack_anim_id;
    int16_t special_attack_anim_id;
    uint8_t ranged_projectile_visual;
    uint8_t magic_projectile_visual;
} OsrsItemCombatVisual;

typedef enum {
    OSRS_COMBAT_PROJECTILE_NONE = 0,
    OSRS_COMBAT_PROJECTILE_BOLT,
    OSRS_COMBAT_PROJECTILE_RUNE_ARROW,
    OSRS_COMBAT_PROJECTILE_DRAGON_ARROW,
    OSRS_COMBAT_PROJECTILE_DRAGON_DART,
    OSRS_COMBAT_PROJECTILE_TRIDENT,
} OsrsCombatProjectileVisual;

#define OSRS_COMBAT_VISUAL_ITEM(item_id, attack_anim, special_anim) \
    {item_id, OSRS_COMBAT_VISUAL_STYLE_ANY, attack_anim, special_anim, \
     OSRS_COMBAT_PROJECTILE_NONE, OSRS_COMBAT_PROJECTILE_NONE}

#define OSRS_COMBAT_VISUAL_RANGED(item_id, attack_anim, special_anim, projectile) \
    {item_id, OSRS_COMBAT_VISUAL_STYLE_ANY, attack_anim, special_anim, \
     projectile, OSRS_COMBAT_PROJECTILE_NONE}

#define OSRS_COMBAT_VISUAL_MAGIC(item_id, attack_anim, special_anim, projectile) \
    {item_id, OSRS_COMBAT_VISUAL_STYLE_ANY, attack_anim, special_anim, \
     OSRS_COMBAT_PROJECTILE_NONE, projectile}

static const OsrsItemCombatVisual OSRS_ITEM_COMBAT_VISUALS[] = {
    OSRS_COMBAT_VISUAL_ITEM(OSRS_ITEM_ID_ABYSSAL_WHIP, 1658, OSRS_COMBAT_VISUAL_NO_ANIMATION),
    OSRS_COMBAT_VISUAL_ITEM(OSRS_ITEM_ID_GRANITE_MAUL, 1665, 1667),
    OSRS_COMBAT_VISUAL_ITEM(OSRS_ITEM_ID_AHRIMS_STAFF, 393, OSRS_COMBAT_VISUAL_NO_ANIMATION),
    OSRS_COMBAT_VISUAL_ITEM(OSRS_ITEM_ID_DRAGON_DAGGER, 376, 1062),
    OSRS_COMBAT_VISUAL_RANGED(OSRS_ITEM_ID_RUNE_CROSSBOW, 4230,
        OSRS_COMBAT_VISUAL_NO_ANIMATION, OSRS_COMBAT_PROJECTILE_BOLT),
    OSRS_COMBAT_VISUAL_RANGED(OSRS_ITEM_ID_DARK_BOW, 426,
        OSRS_COMBAT_VISUAL_NO_ANIMATION, OSRS_COMBAT_PROJECTILE_RUNE_ARROW),
    OSRS_COMBAT_VISUAL_ITEM(OSRS_ITEM_ID_STAFF_OF_THE_DEAD, 414,
        OSRS_COMBAT_VISUAL_NO_ANIMATION),
    OSRS_COMBAT_VISUAL_RANGED(OSRS_ITEM_ID_ARMADYL_CROSSBOW, 4230,
        OSRS_COMBAT_VISUAL_NO_ANIMATION, OSRS_COMBAT_PROJECTILE_BOLT),
    OSRS_COMBAT_VISUAL_ITEM(OSRS_ITEM_ID_ARMADYL_GODSWORD, 7045, 7644),
    OSRS_COMBAT_VISUAL_RANGED(OSRS_ITEM_ID_MAGIC_SHORTBOW_I, 426, 1074,
        OSRS_COMBAT_PROJECTILE_RUNE_ARROW),
    OSRS_COMBAT_VISUAL_MAGIC(OSRS_ITEM_ID_TRIDENT_OF_THE_SWAMP, 1167,
        OSRS_COMBAT_VISUAL_NO_ANIMATION, OSRS_COMBAT_PROJECTILE_TRIDENT),
    OSRS_COMBAT_VISUAL_RANGED(OSRS_ITEM_ID_TOXIC_BLOWPIPE, 5061, 5061,
        OSRS_COMBAT_PROJECTILE_DRAGON_DART),
    OSRS_COMBAT_VISUAL_ITEM(OSRS_ITEM_ID_DRAGON_CLAWS, 393, 7514),
    OSRS_COMBAT_VISUAL_ITEM(OSRS_ITEM_ID_ZURIELS_STAFF, 393,
        OSRS_COMBAT_VISUAL_NO_ANIMATION),
    OSRS_COMBAT_VISUAL_RANGED(OSRS_ITEM_ID_HEAVY_BALLISTA, 7218,
        OSRS_COMBAT_VISUAL_NO_ANIMATION, OSRS_COMBAT_PROJECTILE_BOLT),
    OSRS_COMBAT_VISUAL_RANGED(OSRS_ITEM_ID_TWISTED_BOW, 426,
        OSRS_COMBAT_VISUAL_NO_ANIMATION, OSRS_COMBAT_PROJECTILE_DRAGON_ARROW),
    OSRS_COMBAT_VISUAL_ITEM(OSRS_ITEM_ID_ELDER_MAUL, 7516,
        OSRS_COMBAT_VISUAL_NO_ANIMATION),
    OSRS_COMBAT_VISUAL_ITEM(OSRS_ITEM_ID_KODAI_WAND, 414,
        OSRS_COMBAT_VISUAL_NO_ANIMATION),
    OSRS_COMBAT_VISUAL_ITEM(OSRS_ITEM_ID_GHRAZI_RAPIER, 8145,
        OSRS_COMBAT_VISUAL_NO_ANIMATION),
    OSRS_COMBAT_VISUAL_MAGIC(OSRS_ITEM_ID_SANGUINESTI_STAFF, 1167,
        OSRS_COMBAT_VISUAL_NO_ANIMATION, OSRS_COMBAT_PROJECTILE_TRIDENT),
    OSRS_COMBAT_VISUAL_ITEM(OSRS_ITEM_ID_VESTAS_LONGSWORD, 390, 7515),
    OSRS_COMBAT_VISUAL_ITEM(OSRS_ITEM_ID_STATIUSS_WARHAMMER, 401, 1378),
    OSRS_COMBAT_VISUAL_RANGED(OSRS_ITEM_ID_MORRIGANS_JAVELIN, 806,
        OSRS_COMBAT_VISUAL_NO_ANIMATION, OSRS_COMBAT_PROJECTILE_BOLT),
    OSRS_COMBAT_VISUAL_ITEM(OSRS_ITEM_ID_INQUISITORS_MACE, 400, 1060),
    OSRS_COMBAT_VISUAL_ITEM(OSRS_ITEM_ID_VOLATILE_NIGHTMARE_STAFF, 414, 8532),
    OSRS_COMBAT_VISUAL_RANGED(OSRS_ITEM_ID_BOW_OF_FAERDHINEN, 426,
        OSRS_COMBAT_VISUAL_NO_ANIMATION, OSRS_COMBAT_PROJECTILE_RUNE_ARROW),
    OSRS_COMBAT_VISUAL_ITEM(OSRS_ITEM_ID_ANCIENT_GODSWORD, 7045, 7644),
    OSRS_COMBAT_VISUAL_RANGED(OSRS_ITEM_ID_ZARYTE_CROSSBOW, 4230,
        OSRS_COMBAT_VISUAL_NO_ANIMATION, OSRS_COMBAT_PROJECTILE_BOLT),
    OSRS_COMBAT_VISUAL_ITEM(OSRS_ITEM_ID_VOIDWAKER, 401, 1378),
    OSRS_COMBAT_VISUAL_ITEM(OSRS_ITEM_ID_DRAGON_HUNTER_WAND, 1167,
        OSRS_COMBAT_VISUAL_NO_ANIMATION),
    OSRS_COMBAT_VISUAL_MAGIC(OSRS_ITEM_ID_EYE_OF_AYAK, 1167,
        OSRS_COMBAT_VISUAL_NO_ANIMATION, OSRS_COMBAT_PROJECTILE_TRIDENT),
};

#undef OSRS_COMBAT_VISUAL_ITEM
#undef OSRS_COMBAT_VISUAL_RANGED
#undef OSRS_COMBAT_VISUAL_MAGIC

static const size_t OSRS_ITEM_COMBAT_VISUAL_COUNT =
    sizeof(OSRS_ITEM_COMBAT_VISUALS) / sizeof(OSRS_ITEM_COMBAT_VISUALS[0]);

/**
 * Return the combat visual row for an OSRS item id and style.
 */
static inline const OsrsItemCombatVisual* osrs_combat_visual_find_item_id(
    uint16_t item_id, AttackStyle style
) {
    const OsrsItemCombatVisual* any_match = NULL;

    for (size_t i = 0; i < OSRS_ITEM_COMBAT_VISUAL_COUNT; i++) {
        const OsrsItemCombatVisual* visual = &OSRS_ITEM_COMBAT_VISUALS[i];
        if (visual->item_id != item_id) continue;
        if (visual->style == (int8_t)style) return visual;
        if (visual->style == OSRS_COMBAT_VISUAL_STYLE_ANY) any_match = visual;
    }

    return any_match;
}

/**
 * Return the combat visual row for an OSRS item database index and style.
 */
static inline const OsrsItemCombatVisual* osrs_combat_visual_find_item_db(
    uint8_t item_db_idx, AttackStyle style
) {
    if (item_db_idx >= NUM_ITEMS) return NULL;
    return osrs_combat_visual_find_item_id(ITEM_DATABASE[item_db_idx].item_id, style);
}

/**
 * Return the normal or special weapon attack animation for an item database index.
 */
static inline int osrs_combat_visual_weapon_attack_anim(
    uint8_t item_db_idx, AttackStyle style, int is_special, int fallback_anim_id
) {
    const OsrsItemCombatVisual* visual =
        osrs_combat_visual_find_item_db(item_db_idx, style);
    if (!visual) return fallback_anim_id;
    if (is_special && visual->special_attack_anim_id != OSRS_COMBAT_VISUAL_NO_ANIMATION) {
        return visual->special_attack_anim_id;
    }
    if (visual->attack_anim_id == OSRS_COMBAT_VISUAL_NO_ANIMATION) return fallback_anim_id;
    return visual->attack_anim_id;
}

/**
 * Return the ranged projectile category for an item database index.
 */
static inline OsrsCombatProjectileVisual osrs_combat_visual_ranged_projectile(
    uint8_t item_db_idx, OsrsCombatProjectileVisual fallback
) {
    const OsrsItemCombatVisual* visual =
        osrs_combat_visual_find_item_db(item_db_idx, ATTACK_STYLE_RANGED);
    if (!visual) return fallback;
    if (visual->ranged_projectile_visual == OSRS_COMBAT_PROJECTILE_NONE) return fallback;
    return (OsrsCombatProjectileVisual)visual->ranged_projectile_visual;
}

/**
 * Return the magic projectile category for an item database index.
 */
static inline OsrsCombatProjectileVisual osrs_combat_visual_magic_projectile(
    uint8_t item_db_idx
) {
    const OsrsItemCombatVisual* visual =
        osrs_combat_visual_find_item_db(item_db_idx, ATTACK_STYLE_MAGIC);
    if (!visual) return OSRS_COMBAT_PROJECTILE_NONE;
    return (OsrsCombatProjectileVisual)visual->magic_projectile_visual;
}

/**
 * Return whether an item uses the powered-staff cast animation on magic attacks.
 */
static inline int osrs_combat_visual_item_is_powered_staff(uint16_t item_id) {
    const OsrsItemCombatVisual* visual =
        osrs_combat_visual_find_item_id(item_id, ATTACK_STYLE_MAGIC);
    return visual && visual->magic_projectile_visual == OSRS_COMBAT_PROJECTILE_TRIDENT;
}

/**
 * Return the magic attack animation for powered staves, otherwise the fallback.
 */
static inline int osrs_combat_visual_magic_attack_anim(
    uint8_t item_db_idx, int is_special, int fallback_anim_id
) {
    if (item_db_idx >= NUM_ITEMS) return fallback_anim_id;
    uint16_t item_id = ITEM_DATABASE[item_db_idx].item_id;
    if (!osrs_combat_visual_item_is_powered_staff(item_id)) return fallback_anim_id;
    return osrs_combat_visual_weapon_attack_anim(
        item_db_idx, ATTACK_STYLE_MAGIC, is_special, fallback_anim_id);
}

#endif
