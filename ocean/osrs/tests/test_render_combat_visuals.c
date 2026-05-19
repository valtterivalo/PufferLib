#include "../osrs_combat_visuals.h"
#include <assert.h>

int main(void) {
    assert(osrs_combat_visual_weapon_attack_anim(
        ITEM_WHIP, ATTACK_STYLE_MELEE, 0, OSRS_PLAYER_UNARMED_ATTACK_ANIM) == 1658);
    assert(osrs_combat_visual_weapon_attack_anim(
        ITEM_INQUISITORS_MACE, ATTACK_STYLE_MELEE, 0, OSRS_PLAYER_UNARMED_ATTACK_ANIM) == 400);
    assert(osrs_combat_visual_weapon_attack_anim(
        ITEM_INQUISITORS_MACE, ATTACK_STYLE_MELEE, 1, OSRS_PLAYER_UNARMED_ATTACK_ANIM) == 1060);
    assert(osrs_combat_visual_weapon_attack_anim(
        ITEM_RUNE_CROSSBOW, ATTACK_STYLE_RANGED, 0, OSRS_PLAYER_UNARMED_ATTACK_ANIM) == 4230);
    assert(osrs_combat_visual_ranged_projectile(
        ITEM_RUNE_CROSSBOW, OSRS_COMBAT_PROJECTILE_NONE) == OSRS_COMBAT_PROJECTILE_BOLT);
    assert(osrs_combat_visual_ranged_projectile(
        ITEM_TWISTED_BOW, OSRS_COMBAT_PROJECTILE_NONE)
        == OSRS_COMBAT_PROJECTILE_DRAGON_ARROW);
    assert(osrs_combat_visual_ranged_projectile(
        ITEM_BOW_OF_FAERDHINEN, OSRS_COMBAT_PROJECTILE_NONE)
        == OSRS_COMBAT_PROJECTILE_RUNE_ARROW);
    assert(osrs_combat_visual_weapon_attack_anim(
        ITEM_TOXIC_BLOWPIPE, ATTACK_STYLE_RANGED, 1, OSRS_PLAYER_UNARMED_ATTACK_ANIM) == 5061);
    assert(osrs_combat_visual_ranged_projectile(
        ITEM_TOXIC_BLOWPIPE, OSRS_COMBAT_PROJECTILE_NONE)
        == OSRS_COMBAT_PROJECTILE_DRAGON_DART);
    assert(osrs_combat_visual_magic_attack_anim(
        ITEM_TRIDENT_OF_SWAMP, 0, 1979) == OSRS_PLAYER_POWERED_STAFF_ATTACK_ANIM);
    assert(osrs_combat_visual_magic_projectile(
        ITEM_TRIDENT_OF_SWAMP) == OSRS_COMBAT_PROJECTILE_TRIDENT);
    assert(osrs_combat_visual_magic_attack_anim(
        ITEM_KODAI_WAND, 0, 1979) == 1979);
    assert(osrs_combat_visual_magic_projectile(
        ITEM_KODAI_WAND) == OSRS_COMBAT_PROJECTILE_NONE);
    assert(osrs_combat_visual_magic_attack_anim(
        ITEM_DRAGON_HUNTER_WAND, 0, 1979) == 1979);
    assert(osrs_combat_visual_weapon_attack_anim(
        NUM_ITEMS, ATTACK_STYLE_MELEE, 0, OSRS_PLAYER_UNARMED_ATTACK_ANIM)
        == OSRS_PLAYER_UNARMED_ATTACK_ANIM);

    const OsrsItemCombatVisual* visual =
        osrs_combat_visual_find_item_id(OSRS_ITEM_ID_TWISTED_BOW, ATTACK_STYLE_RANGED);
    assert(visual);
    assert(visual->attack_anim_id == 426);

    return 0;
}
