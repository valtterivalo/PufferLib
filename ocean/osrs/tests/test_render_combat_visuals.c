#include "../osrs_combat_visuals.h"
#include "../osrs_anim.h"
#include "../data/npc_models.h"
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

static void test_projectile_profiles_match_runec_visual_rows(void) {
    assert(OSRS_COMBAT_VISUAL_ROW_COUNT > 1000);

    const OsrsCombatVisualRow* tbow_row =
        osrs_combat_visual_find_item_id(OSRS_ITEM_ID_TWISTED_BOW, ATTACK_STYLE_RANGED);
    assert(tbow_row);
    assert(tbow_row->attack_anim_id == 426);

    const OsrsCombatVisualRow* bowfa_row =
        osrs_combat_visual_find_item_id(OSRS_ITEM_ID_BOW_OF_FAERDHINEN, ATTACK_STYLE_RANGED);
    assert(bowfa_row);
    assert(bowfa_row->attack_anim_id == 426);

    const OsrsCombatProjectileProfile* tbow =
        osrs_combat_visual_ranged_projectile_profile(
            ITEM_TWISTED_BOW, OSRS_COMBAT_PROJECTILE_NONE);
    assert(tbow);
    assert(tbow->launch_spotanim_id == GFX_DRAGON_ARROW_LAUNCH);
    assert(tbow->travel_spotanim_id == GFX_DRAGON_ARROW);
    assert(tbow->projectile_model_id == OSRS_PROJECTILE_MODEL_DRAGON_ARROW);
    assert(tbow->projectile_anim_id == OSRS_PROJECTILE_ANIM_DRAGON_ARROW);

    const OsrsCombatProjectileProfile* blowpipe =
        osrs_combat_visual_ranged_projectile_profile(
            ITEM_TOXIC_BLOWPIPE, OSRS_COMBAT_PROJECTILE_NONE);
    assert(blowpipe);
    assert(blowpipe->launch_spotanim_id == GFX_DRAGON_DART_LAUNCH);
    assert(blowpipe->travel_spotanim_id == GFX_DRAGON_DART);
    assert(blowpipe->projectile_model_id == OSRS_PROJECTILE_MODEL_DRAGON_DART);
    assert(blowpipe->projectile_anim_id == OSRS_PROJECTILE_ANIM_DRAGON_DART);
    assert(blowpipe->projectile_start_height == 163);
    assert(blowpipe->projectile_end_height == 146);
    assert(blowpipe->projectile_delay == 32);
    assert(blowpipe->projectile_angle == 15);
    assert(blowpipe->projectile_progress == 11);

    const OsrsCombatProjectileProfile* trident =
        osrs_combat_visual_magic_projectile_profile(ITEM_TRIDENT_OF_SWAMP);
    assert(trident);
    assert(trident->launch_spotanim_id == GFX_TRIDENT_CAST);
    assert(trident->travel_spotanim_id == GFX_TRIDENT_PROJ);
    assert(trident->impact_spotanim_id == GFX_TRIDENT_IMPACT);

    const OsrsCombatVisualRow* jad_magic =
        osrs_combat_visual_find_npc_id(3127, ATTACK_STYLE_MAGIC);
    assert(jad_magic);
    assert(jad_magic->attack_anim_id == 2656);
    assert(jad_magic->projectile.travel_spotanim_id == 445);
    assert(jad_magic->projectile.impact_spotanim_id == 446);

    const OsrsCombatVisualRow* inferno_ranger =
        osrs_combat_visual_find_npc_id(7698, ATTACK_STYLE_RANGED);
    assert(inferno_ranger);
    assert(inferno_ranger->attack_anim_id == INF_GEN_ANIM_RANGER_ATTACK);
    assert(inferno_ranger->projectile.travel_spotanim_id == 1377);
    assert(inferno_ranger->projectile.impact_spotanim_id == 1378);
    assert(inferno_ranger->projectile.projectile_model_id == INF_GFX_1377_MODEL);

    const OsrsCombatVisualRow* inferno_mager =
        osrs_combat_visual_find_npc_id(7699, ATTACK_STYLE_MAGIC);
    assert(inferno_mager);
    assert(inferno_mager->attack_anim_id == INF_GEN_ANIM_MAGER_ATTACK);
    assert(inferno_mager->projectile.travel_spotanim_id == 1379);
    assert(inferno_mager->projectile.impact_spotanim_id == 1380);
    assert(inferno_mager->projectile.projectile_model_id == INF_GFX_1379_MODEL);
    assert(inferno_mager->projectile.projectile_anim_id == INF_GFX_1379_ANIM);

    const OsrsCombatVisualRow* inferno_jad_magic =
        osrs_combat_visual_find_npc_id(7700, ATTACK_STYLE_MAGIC);
    assert(inferno_jad_magic);
    assert(inferno_jad_magic->attack_anim_id == INF_GEN_ANIM_JALTOK_JAD_ATTACK_MAGIC);
    assert(inferno_jad_magic->projectile.travel_spotanim_id == 448);
    assert(inferno_jad_magic->projectile.projectile_model_id == INF_GFX_448_MODEL);

    const OsrsCombatVisualRow* inferno_jad_ranged =
        osrs_combat_visual_find_npc_id(7700, ATTACK_STYLE_RANGED);
    assert(inferno_jad_ranged);
    assert(inferno_jad_ranged->attack_anim_id == INF_GEN_ANIM_JALTOK_JAD_ATTACK_RANGED);
    assert(inferno_jad_ranged->projectile.travel_spotanim_id == 451);
    assert(inferno_jad_ranged->projectile.projectile_model_id == INF_GFX_451_MODEL);
    assert(inferno_jad_ranged->projectile.projectile_anim_id == INF_GFX_451_ANIM);

    const OsrsCombatVisualRow* inferno_zuk =
        osrs_combat_visual_find_npc_id(7706, ATTACK_STYLE_MAGIC);
    assert(inferno_zuk);
    assert(inferno_zuk->attack_anim_id == INF_GEN_ANIM_TZKAL_ZUK_ATTACK);
    assert(inferno_zuk->projectile.travel_spotanim_id == 1375);
    assert(inferno_zuk->projectile.projectile_model_id == INF_GFX_1375_MODEL);
    assert(inferno_zuk->projectile.projectile_anim_id == INF_GFX_1375_ANIM);

    const OsrsCombatVisualRow* inferno_zuk_healer =
        osrs_combat_visual_find_npc_id(7708, ATTACK_STYLE_MAGIC);
    assert(inferno_zuk_healer);
    assert(inferno_zuk_healer->attack_anim_id == OSRS_COMBAT_VISUAL_NO_ANIMATION);
    assert(inferno_zuk_healer->projectile.travel_spotanim_id == 660);
    assert(inferno_zuk_healer->projectile.impact_spotanim_id == 659);
    assert(inferno_zuk_healer->projectile.projectile_model_id == INF_GFX_660_MODEL);
    assert(inferno_zuk_healer->projectile.projectile_anim_id == INF_GFX_660_ANIM);
}

static void test_spell_profiles_match_runec_visual_rows(void) {
    const OsrsCombatProjectileProfile* ice =
        osrs_combat_visual_spell_projectile(OSRS_COMBAT_VISUAL_SPELL_ICE_BARRAGE);
    assert(ice);
    assert(ice->travel_spotanim_id == GFX_ICE_BARRAGE_PROJ);
    assert(ice->impact_spotanim_id == GFX_ICE_BARRAGE_HIT);
    assert(ice->projectile_model_id == OSRS_PROJECTILE_MODEL_ICE_BARRAGE);
    assert(ice->projectile_anim_id == OSRS_PROJECTILE_ANIM_BARRAGE);
    assert(ice->projectile_start_height == 172);
    assert(ice->projectile_end_height == 0);
    assert(ice->projectile_delay == 51);
    assert(ice->projectile_angle == 16);
    assert(ice->projectile_length_adjustment == -5);
    assert(ice->projectile_progress == 64);
    assert(ice->projectile_step_multiplier == 10);

    const OsrsCombatProjectileProfile* blood =
        osrs_combat_visual_spell_projectile(OSRS_COMBAT_VISUAL_SPELL_BLOOD_BARRAGE);
    assert(blood);
    assert(blood->travel_spotanim_id == OSRS_COMBAT_PROJECTILE_MISSING);
    assert(blood->impact_spotanim_id == GFX_BLOOD_BARRAGE_HIT);
    assert(blood->projectile_delay == 51);
}

int main(void) {
    test_empty_origin_transform_sets_fallback_pivot();
    test_alpha_transform_updates_face_alphas_and_mesh_colors();
    test_projectile_profiles_match_runec_visual_rows();
    test_spell_profiles_match_runec_visual_rows();

    assert(osrs_combat_visual_weapon_attack_anim(
        ITEM_WHIP, ATTACK_STYLE_MELEE, 0, OSRS_PLAYER_UNARMED_ATTACK_ANIM) == 1658);
    assert(osrs_combat_visual_weapon_attack_anim(
        ITEM_INQUISITORS_MACE, ATTACK_STYLE_MELEE, 0, OSRS_PLAYER_UNARMED_ATTACK_ANIM) == 4503);
    assert(osrs_combat_visual_weapon_attack_anim(
        ITEM_INQUISITORS_MACE, ATTACK_STYLE_MELEE, 1, OSRS_PLAYER_UNARMED_ATTACK_ANIM) == 1060);
    assert(osrs_combat_visual_weapon_attack_anim(
        ITEM_RUNE_CROSSBOW, ATTACK_STYLE_RANGED, 0, OSRS_PLAYER_UNARMED_ATTACK_ANIM) == 7552);
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
    assert(osrs_combat_visual_weapon_attack_anim(
        ITEM_BGS, ATTACK_STYLE_MELEE, 0, OSRS_PLAYER_UNARMED_ATTACK_ANIM) == 7045);
    assert(osrs_combat_visual_weapon_attack_anim_for_fight_style(
        ITEM_BGS, ATTACK_STYLE_MELEE, FIGHT_STYLE_ACCURATE, 0,
        OSRS_PLAYER_UNARMED_ATTACK_ANIM) == 7045);
    assert(osrs_combat_visual_weapon_attack_anim_for_fight_style(
        ITEM_BGS, ATTACK_STYLE_MELEE, FIGHT_STYLE_CONTROLLED, 0,
        OSRS_PLAYER_UNARMED_ATTACK_ANIM) == 7054);
    assert(osrs_combat_visual_weapon_attack_anim_for_fight_style(
        ITEM_BGS, ATTACK_STYLE_MELEE, FIGHT_STYLE_DEFENSIVE, 0,
        OSRS_PLAYER_UNARMED_ATTACK_ANIM) == 7055);
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

    const OsrsCombatVisualRow* visual =
        osrs_combat_visual_find_item_id(OSRS_ITEM_ID_TWISTED_BOW, ATTACK_STYLE_RANGED);
    assert(visual);
    assert(visual->attack_anim_id == 426);

    return 0;
}
