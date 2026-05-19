#include "../osrs_combat_visuals.h"
#include "../osrs_anim.h"
#include <assert.h>
#include <stdint.h>

static void test_empty_origin_transform_sets_fallback_pivot(void) {
    uint8_t vertex_skins[] = { 1 };
    int16_t base_vertices[] = { 20, 20, 30 };
    AnimModelState* state = anim_model_state_create(vertex_skins, 1);

    uint8_t types[] = { 0, 3 };
    uint8_t map_lengths[] = { 1, 1 };
    uint8_t empty_origin_labels[] = { 0 };
    uint8_t scale_labels[] = { 1 };
    uint8_t* frame_maps[] = { empty_origin_labels, scale_labels };
    AnimFrameBase fb = {
        .base_id = 1,
        .slot_count = 2,
        .types = types,
        .map_lengths = map_lengths,
        .frame_maps = frame_maps,
    };
    AnimTransform transforms[] = {
        { .slot_index = 0, .dx = 10, .dy = 20, .dz = 30 },
        { .slot_index = 1, .dx = 256, .dy = 128, .dz = 128 },
    };
    AnimFrameData frame = {
        .framebase_id = 1,
        .transform_count = 2,
        .transforms = transforms,
    };

    anim_apply_frame(state, base_vertices, &frame, &fb);
    assert(state->verts[0] == 30);
    assert(state->verts[1] == 20);
    assert(state->verts[2] == 30);
    anim_model_state_free(state);
}

static void test_alpha_transform_updates_face_alphas_and_mesh_colors(void) {
    uint8_t vertex_skins[] = { 0 };
    uint8_t face_alpha_labels[] = { 2, 2, 255 };
    uint8_t base_face_alphas[] = { 0, 250, 40 };
    int16_t base_vertices[] = { 0, 0, 0 };
    AnimModelState* state = anim_model_state_create_with_face_alpha(
        vertex_skins, 1, face_alpha_labels, base_face_alphas, 3);

    uint8_t types[] = { 5 };
    uint8_t map_lengths[] = { 1 };
    uint8_t alpha_labels[] = { 2 };
    uint8_t* frame_maps[] = { alpha_labels };
    AnimFrameBase fb = {
        .base_id = 1,
        .slot_count = 1,
        .types = types,
        .map_lengths = map_lengths,
        .frame_maps = frame_maps,
    };
    AnimTransform transforms[] = {
        { .slot_index = 0, .dx = 2, .dy = 0, .dz = 0 },
    };
    AnimFrameData frame = {
        .framebase_id = 1,
        .transform_count = 1,
        .transforms = transforms,
    };
    unsigned char mesh_colors[3 * 3 * 4];
    for (int i = 0; i < 3 * 3; i++) {
        mesh_colors[i * 4] = 255;
        mesh_colors[i * 4 + 1] = 255;
        mesh_colors[i * 4 + 2] = 255;
        mesh_colors[i * 4 + 3] = 255;
    }

    anim_apply_frame(state, base_vertices, &frame, &fb);
    assert(state->face_alphas[0] == 16);
    assert(state->face_alphas[1] == 255);
    assert(state->face_alphas[2] == 40);

    anim_update_mesh_alpha(mesh_colors, state, 3);
    assert(mesh_colors[3] == 239);
    assert(mesh_colors[15] == 0);
    assert(mesh_colors[27] == 215);
    anim_model_state_free(state);
}

int main(void) {
    test_empty_origin_transform_sets_fallback_pivot();
    test_alpha_transform_updates_face_alphas_and_mesh_colors();

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
