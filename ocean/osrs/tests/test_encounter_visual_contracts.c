#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../encounters/encounter_nh_pvp.h"
#include "../encounters/encounter_zulrah.h"
#include "../osrs_combat_visuals.h"

static char* read_text_file(const char* path) {
    FILE* file = fopen(path, "rb");
    assert(file != NULL);
    assert(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length >= 0);
    assert(fseek(file, 0, SEEK_SET) == 0);

    char* text = (char*)malloc((size_t)length + 1);
    assert(text != NULL);
    size_t read = fread(text, 1, (size_t)length, file);
    assert(read == (size_t)length);
    text[length] = '\0';
    fclose(file);
    return text;
}

static void test_zulrah_eval_binding_bootstraps_3d_scene(void) {
    char* source = read_text_file("ocean/osrs_zulrah/binding.c");
    assert(strstr(source, "osrs_asset_require_group(OSRS_ASSET_GROUP_ZULRAH);"));
    assert(strstr(source, "osrs_asset_require_group(OSRS_ASSET_GROUP_COMBAT_VISUALS);"));
    assert(strstr(source, "collision_map_load(OSRS_ASSET(\"zulrah.cmap\"))"));
    assert(strstr(source, "rc->collision_map = cmap;"));
    assert(strstr(source, "render_populate_entities(rc, re);"));
    assert(strstr(source, "rc->cam_target_x = (float)rc->arena_base_x"));
    assert(strstr(source, "render_seed_entity_visual_slot(rc, i);"));
    assert(strstr(source, "render_post_tick(rc, re);"));
    assert(strstr(source, "float tps = render_effective_ticks_per_second(rc);"));
    assert(strstr(source, "while (GetTime() < deadline)"));
    free(source);
}

static void test_nh_pvp_exposes_two_visible_fighters(void) {
    NhPvpState* state = (NhPvpState*)nh_pvp_create();
    NhPvpContext context;
    memset(&context, 0, sizeof(context));
    nh_pvp_put_int((EncounterState*)state, (EncounterContext*)&context, "use_c_opponent", 1);
    nh_pvp_put_int((EncounterState*)state, (EncounterContext*)&context, "opponent_type", OPP_IMPROVED);
    nh_pvp_put_int((EncounterState*)state, (EncounterContext*)&context, "is_lms", 1);
    nh_pvp_reset((EncounterState*)state, (EncounterContext*)&context, 123);

    RenderEntity entities[4];
    int count = 0;
    nh_pvp_fill_render_entities(
        (EncounterState*)state, (EncounterContext*)&context, entities, 4, &count);

    assert(count == 2);
    assert(entities[0].entity_type == ENTITY_PLAYER);
    assert(entities[1].entity_type == ENTITY_PLAYER);
    assert(entities[0].attack_target_entity_idx == 1);
    assert(entities[1].attack_target_entity_idx == 0);
    assert(entities[0].current_hitpoints > 0);
    assert(entities[1].current_hitpoints > 0);

    nh_pvp_destroy((EncounterState*)state);
}

static void test_nh_pvp_scripted_demo_ticks_both_sides(void) {
    NhPvpState* state = (NhPvpState*)nh_pvp_create();
    NhPvpContext context;
    memset(&context, 0, sizeof(context));
    nh_pvp_put_int((EncounterState*)state, (EncounterContext*)&context, "use_c_opponent", 1);
    nh_pvp_put_int((EncounterState*)state, (EncounterContext*)&context, "opponent_type", OPP_IMPROVED);
    nh_pvp_put_int((EncounterState*)state, (EncounterContext*)&context, "use_c_opponent_p0", 1);
    nh_pvp_put_int((EncounterState*)state, (EncounterContext*)&context, "opponent_p0_type", OPP_IMPROVED);
    nh_pvp_put_int((EncounterState*)state, (EncounterContext*)&context, "is_lms", 1);
    nh_pvp_reset((EncounterState*)state, (EncounterContext*)&context, 123);

    int start_tick = nh_pvp_get_tick((EncounterState*)state, (EncounterContext*)&context);
    int actions[NUM_ACTION_HEADS] = {0};
    for (int i = 0; i < 8; i++)
        nh_pvp_step((EncounterState*)state, (EncounterContext*)&context, actions);

    assert(nh_pvp_get_tick((EncounterState*)state, (EncounterContext*)&context) > start_tick);
    assert(state->env.players[0].current_hitpoints > 0);
    assert(state->env.players[1].current_hitpoints > 0);

    nh_pvp_destroy((EncounterState*)state);
}

static void test_nh_pvp_respects_visual_tier_override(void) {
    NhPvpState* state = (NhPvpState*)nh_pvp_create();
    NhPvpContext context;
    memset(&context, 0, sizeof(context));
    nh_pvp_put_int((EncounterState*)state, (EncounterContext*)&context, "gear_tier", 3);
    nh_pvp_reset((EncounterState*)state, (EncounterContext*)&context, 123);

    assert(state->env.pvp_runtime.gear_tier_weights[0] == 0.0f);
    assert(state->env.pvp_runtime.gear_tier_weights[1] == 0.0f);
    assert(state->env.pvp_runtime.gear_tier_weights[2] == 0.0f);
    assert(state->env.pvp_runtime.gear_tier_weights[3] == 1.0f);
    assert(state->env.players[0].equipped[GEAR_SLOT_WEAPON] < NUM_ITEMS);
    assert(state->env.players[1].equipped[GEAR_SLOT_WEAPON] < NUM_ITEMS);

    nh_pvp_destroy((EncounterState*)state);
}

static void test_zulrah_uses_generated_cache_mapping(void) {
    const NpcModelMapping* green = npc_model_lookup(2042);
    const NpcModelMapping* red = npc_model_lookup(2043);
    const NpcModelMapping* blue = npc_model_lookup(2044);
    assert(green != NULL);
    assert(red != NULL);
    assert(blue != NULL);
    assert(green->model_id == NPC_MODEL_MAP_ZULRAH_GEN[0].model_id);
    assert(red->model_id == NPC_MODEL_MAP_ZULRAH_GEN[1].model_id);
    assert(blue->model_id == NPC_MODEL_MAP_ZULRAH_GEN[2].model_id);
    assert(green->idle_anim == ZULRAH_ANIM_IDLE);
    assert(red->attack_anim == ZULRAH_ANIM_TAIL_LEFT);
    assert(blue->attack_anim == ZULRAH_ANIM_ATTACK);
    assert(ZULRAH_ANIM_SURFACE == ZUL_GEN_ANIM_SNAKEBOSS_SPAWN);
    assert(ZULRAH_ANIM_DIVE == ZUL_GEN_ANIM_SNAKEBOSS_SINKFAST);
    assert(ZULRAH_ANIM_RISE == ZUL_GEN_ANIM_SNAKEBOSS_EMERGEFAST);
    assert(ZULRAH_ANIM_ATTACK_MAGIC == ZUL_GEN_ANIM_SNAKEBOSS_ATTACK_ACIDX1);
}

static void test_zulrah_animation_events_have_lifetimes(void) {
    ZulrahState* state = (ZulrahState*)zul_create();
    zul_reset((EncounterState*)state, 123);

    assert(state->zulrah_visible == 1);
    assert(state->zulrah.npc_anim_id == ZULRAH_ANIM_SURFACE ||
           state->zulrah.npc_anim_id == ZULRAH_ANIM_RISE);
    assert(state->zulrah_anim_event_tick == state->tick);

    state->surface_timer = 0;
    state->is_diving = 0;
    state->zulrah_anim_until_tick = 0;
    zul_update_npc_anim_lifetime(state);
    assert(state->zulrah.npc_anim_id == -1);

    zul_record_attack(state, state->zulrah.x, state->zulrah.y,
        state->player.x, state->player.y, 0, 0);
    assert(state->zulrah.npc_anim_id == ZULRAH_ANIM_ATTACK);
    assert(state->zulrah_anim_event_tick == state->tick);
    assert(state->zulrah_anim_until_tick == state->tick + ZUL_RANGED_ANIM_TICKS);

    state->tick = state->zulrah_anim_until_tick;
    zul_update_npc_anim_lifetime(state);
    assert(state->zulrah.npc_anim_id == -1);

    zul_enter_dive(state);
    assert(state->is_diving == 1);
    assert(state->zulrah.npc_anim_id == ZULRAH_ANIM_DIVE);
    assert(state->zulrah_anim_event_tick == state->tick);
    assert(state->zulrah_anim_until_tick == state->tick + ZUL_DIVE_ANIM_TICKS);

    zul_destroy((EncounterState*)state);
}

static void test_zulrah_primary_animations_are_single_tick_render_events(void) {
    ZulrahState* state = (ZulrahState*)zul_create();
    zul_reset((EncounterState*)state, 123);

    RenderEntity entities[8];
    int count = 0;
    int surface_anim = state->zulrah.npc_anim_id;
    zul_fill_render_entities((EncounterState*)state, entities, 8, &count);
    assert(count >= 2);
    assert(entities[1].npc_anim_id == surface_anim);

    state->tick = state->zulrah_anim_event_tick + 1;
    zul_fill_render_entities((EncounterState*)state, entities, 8, &count);
    assert(count >= 2);
    assert(entities[1].npc_anim_id == -1);

    state->tick = 10;
    state->surface_timer = 0;
    state->is_diving = 0;
    zul_record_attack(state, state->zulrah.x, state->zulrah.y,
        state->player.x, state->player.y, 1, 0);
    assert(state->zulrah.npc_anim_id == ZULRAH_ANIM_ATTACK_MAGIC);
    assert(state->zulrah_anim_until_tick == state->tick + ZUL_MAGIC_ANIM_TICKS);
    zul_fill_render_entities((EncounterState*)state, entities, 8, &count);
    assert(count >= 2);
    assert(entities[1].npc_anim_id == ZULRAH_ANIM_ATTACK_MAGIC);

    state->tick++;
    zul_fill_render_entities((EncounterState*)state, entities, 8, &count);
    assert(count >= 2);
    assert(entities[1].npc_anim_id == -1);
    assert(state->zulrah.npc_anim_id == ZULRAH_ANIM_ATTACK_MAGIC);

    state->tick = 20;
    state->is_diving = 0;
    zul_enter_dive(state);
    zul_fill_render_entities((EncounterState*)state, entities, 8, &count);
    assert(count >= 2);
    assert(entities[1].npc_anim_id == ZULRAH_ANIM_DIVE);

    state->tick++;
    zul_fill_render_entities((EncounterState*)state, entities, 8, &count);
    assert(count >= 2);
    assert(entities[1].npc_anim_id == -1);

    state->tick = state->zulrah_anim_until_tick;
    zul_fill_render_entities((EncounterState*)state, entities, 8, &count);
    assert(count >= 2);
    assert(entities[1].npc_visible == 0);

    zul_destroy((EncounterState*)state);
}

static void test_zulrah_attack_event_faces_player(void) {
    ZulrahState* state = (ZulrahState*)zul_create();
    zul_reset((EncounterState*)state, 123);

    state->surface_timer = 0;
    state->is_diving = 0;
    state->zulrah_visible = 1;
    state->zulrah.npc_visible = 1;
    zul_record_attack(state, state->zulrah.x, state->zulrah.y,
        state->player.x, state->player.y, 0, 0);

    RenderEntity entities[8];
    int count = 0;
    zul_fill_render_entities((EncounterState*)state, entities, 8, &count);
    assert(count >= 2);
    assert(entities[1].attack_target_entity_idx == 0);
    assert(entities[1].npc_anim_id == ZULRAH_ANIM_ATTACK);

    zul_destroy((EncounterState*)state);
}

static void test_zulrah_idle_and_walk_are_secondary_pose(void) {
    ZulrahState* state = (ZulrahState*)zul_create();
    zul_reset((EncounterState*)state, 123);

    state->surface_timer = 0;
    state->is_diving = 0;
    state->zulrah_anim_until_tick = 0;
    zul_update_npc_anim_lifetime(state);

    RenderEntity entities[8];
    int count = 0;
    zul_fill_render_entities((EncounterState*)state, entities, 8, &count);
    assert(count >= 2);
    assert(entities[1].npc_anim_id == -1);

    zul_spawn_snakeling(state);
    state->snakelings[0].entity.npc_anim_id = SNAKELING_ANIM_WALK;
    zul_fill_render_entities((EncounterState*)state, entities, 8, &count);
    assert(count >= 3);
    assert(entities[2].npc_anim_id == -1);

    zul_destroy((EncounterState*)state);
}

static void test_zulrah_snakelings_are_render_entities_not_overlay_adds(void) {
    ZulrahState* state = (ZulrahState*)zul_create();
    zul_reset((EncounterState*)state, 123);
    zul_spawn_snakeling(state);
    zul_spawn_snakeling(state);

    int active = 0;
    for (int i = 0; i < ZUL_MAX_SNAKELINGS; i++)
        if (state->snakelings[i].active) active++;

    RenderEntity entities[8];
    int count = 0;
    zul_fill_render_entities((EncounterState*)state, entities, 8, &count);
    assert(count == 2 + active);

    EncounterOverlay ov;
    memset(&ov, 0, sizeof(ov));
    zul_render_post_tick((EncounterState*)state, &ov);
    assert(ov.add_count == 0);

    zul_destroy((EncounterState*)state);
}

static void test_zulrah_snakeling_slot_reuse_gets_new_render_identity(void) {
    ZulrahState* state = (ZulrahState*)zul_create();
    zul_reset((EncounterState*)state, 123);

    RenderEntity entities[8];
    int count = 0;
    zul_spawn_snakeling(state);
    zul_fill_render_entities((EncounterState*)state, entities, 8, &count);
    assert(count >= 3);
    uint32_t first_id = entities[2].npc_instance_id;
    assert(first_id != 0);

    state->snakelings[0].active = 0;
    zul_spawn_snakeling(state);
    zul_fill_render_entities((EncounterState*)state, entities, 8, &count);
    assert(count >= 3);
    uint32_t second_id = entities[2].npc_instance_id;
    assert(second_id != 0);
    assert(second_id != first_id);

    zul_destroy((EncounterState*)state);
}

static void test_zulrah_loadouts_have_weapon_visual_contracts(void) {
    for (int tier = 0; tier < ZUL_NUM_GEAR_TIERS; tier++) {
        uint8_t mage_weapon = ZUL_MAGE_LOADOUT[tier][GEAR_SLOT_WEAPON];
        const OsrsCombatProjectileProfile* mage_projectile =
            osrs_combat_visual_magic_projectile_profile(mage_weapon);
        assert(mage_projectile);
        if (mage_weapon == ITEM_EYE_OF_AYAK) {
            assert(mage_projectile->launch_spotanim_id == GFX_EYE_OF_AYAK_CAST);
            assert(mage_projectile->travel_spotanim_id == GFX_EYE_OF_AYAK_PROJ);
            assert(mage_projectile->impact_spotanim_id == GFX_EYE_OF_AYAK_IMPACT);
            assert(mage_projectile->projectile_model_id == 28450);
            assert(mage_projectile->projectile_anim_id == 12398);
            assert(osrs_combat_visual_magic_attack_anim_for_fight_style(
                mage_weapon, FIGHT_STYLE_ACCURATE, 0, -1) == 12397);
            assert(osrs_combat_visual_magic_attack_anim_for_fight_style(
                mage_weapon, FIGHT_STYLE_ACCURATE, 1, -1) == 12394);
        } else {
            assert(mage_projectile->launch_spotanim_id == GFX_TRIDENT_CAST);
            assert(mage_projectile->travel_spotanim_id == GFX_TRIDENT_PROJ);
            assert(mage_projectile->impact_spotanim_id == GFX_TRIDENT_IMPACT);
            assert(mage_projectile->projectile_model_id == OSRS_PROJECTILE_MODEL_TRIDENT);
            assert(mage_projectile->projectile_anim_id == OSRS_PROJECTILE_ANIM_TRIDENT);
            assert(osrs_combat_visual_magic_attack_anim_for_fight_style(
                mage_weapon, FIGHT_STYLE_ACCURATE, 0, -1)
                == OSRS_PLAYER_POWERED_STAFF_ATTACK_ANIM);
        }

        uint8_t range_weapon = ZUL_RANGE_LOADOUT[tier][GEAR_SLOT_WEAPON];
        const OsrsCombatProjectileProfile* range_projectile =
            osrs_combat_visual_ranged_projectile_profile(
                range_weapon, OSRS_COMBAT_PROJECTILE_NONE);
        assert(range_projectile);
        assert(range_projectile->projectile_model_id > 0);
        assert(osrs_combat_visual_weapon_attack_anim_for_fight_style(
            range_weapon, ATTACK_STYLE_RANGED, FIGHT_STYLE_RAPID, 0, -1) != -1);
    }
}

static void test_zulrah_eye_of_ayak_uses_generated_item_stats(void) {
    ZulrahState* state = (ZulrahState*)zul_create();
    state->gear_tier = 2;
    state->gear_tier_fixed = 2;
    zul_reset((EncounterState*)state, 123);

    assert(state->mage_stats.attack_speed == 3);
    assert(state->mage_stats.attack_range == 6);

    state->surface_timer = 0;
    state->is_diving = 0;
    state->zulrah_visible = 1;
    state->player.attack_timer = 0;
    state->player_gear = ZUL_GEAR_MAGE;
    encounter_apply_loadout(&state->player, ZUL_MAGE_LOADOUT[2], GEAR_MAGE);
    zul_player_attack(state, 1);
    assert(state->player.attack_timer == 3);

    zul_destroy((EncounterState*)state);
}

static void test_zulrah_eye_of_ayak_cannot_attack_past_item_range(void) {
    ZulrahState* state = (ZulrahState*)zul_create();
    state->gear_tier = 2;
    state->gear_tier_fixed = 2;
    zul_reset((EncounterState*)state, 123);

    state->surface_timer = 0;
    state->is_diving = 0;
    state->zulrah_visible = 1;
    state->zulrah.x = ZUL_POSITIONS[ZUL_POS_NORTH][0];
    state->zulrah.y = ZUL_POSITIONS[ZUL_POS_NORTH][1];
    state->player.x = 22;
    state->player.y = 22;
    state->player.attack_timer = 0;
    state->player_gear = ZUL_GEAR_MAGE;
    encounter_apply_loadout(&state->player, ZUL_MAGE_LOADOUT[2], GEAR_MAGE);

    zul_player_attack(state, 1);
    assert(state->player.attack_timer == 0);
    assert(state->player_attacked_this_tick == 0);

    state->player.x = 18;
    state->player.y = 16;
    zul_player_attack(state, 1);
    assert(state->player.attack_timer == 3);
    assert(state->player_attacked_this_tick == 1);

    zul_destroy((EncounterState*)state);
}

static int overlay_has_player_to_zulrah_projectile(const EncounterOverlay* ov) {
    for (int i = 0; i < ov->projectile_count; i++) {
        if (ov->projectiles[i].source_kind == ENCOUNTER_PROJECTILE_TARGET_PLAYER &&
                ov->projectiles[i].target_kind == ENCOUNTER_PROJECTILE_TARGET_NPC_SLOT &&
                ov->projectiles[i].target_npc_slot == 0 &&
                ov->projectiles[i].model_id > 0) {
            return 1;
        }
    }
    return 0;
}

static void test_zulrah_player_attack_projectiles_cover_mage_and_range(void) {
    for (int tier = 0; tier < ZUL_NUM_GEAR_TIERS; tier++) {
        ZulrahState* state = (ZulrahState*)zul_create();
        state->gear_tier = tier;
        zul_reset((EncounterState*)state, 123);
        state->surface_timer = 0;
        state->is_diving = 0;
        state->zulrah_visible = 1;
        state->player.attack_timer = 0;
        state->player_gear = ZUL_GEAR_MAGE;
        zul_player_attack(state, 1);

        EncounterOverlay ov;
        memset(&ov, 0, sizeof(ov));
        zul_render_post_tick((EncounterState*)state, &ov);
        assert(overlay_has_player_to_zulrah_projectile(&ov));

        state->attack_event_count = 0;
        state->cloud_event_count = 0;
        state->player_attacked_this_tick = 0;
        state->player.attack_timer = 0;
        state->player_gear = ZUL_GEAR_RANGE;
        encounter_apply_loadout(&state->player, ZUL_RANGE_LOADOUT[tier], GEAR_RANGED);
        zul_player_attack(state, 0);

        memset(&ov, 0, sizeof(ov));
        zul_render_post_tick((EncounterState*)state, &ov);
        assert(overlay_has_player_to_zulrah_projectile(&ov));

        zul_destroy((EncounterState*)state);
    }
}

static void test_zulrah_melee_attack_does_not_emit_projectile(void) {
    ZulrahState* state = (ZulrahState*)zul_create();
    zul_reset((EncounterState*)state, 123);
    state->surface_timer = 0;
    state->is_diving = 0;
    state->zulrah_visible = 1;
    zul_record_attack(state, state->zulrah.x, state->zulrah.y,
        state->player.x, state->player.y, 2, 25);

    EncounterOverlay ov;
    memset(&ov, 0, sizeof(ov));
    zul_render_post_tick((EncounterState*)state, &ov);
    assert(ov.projectile_count == 0);

    zul_destroy((EncounterState*)state);
}

static void test_zulrah_eye_of_ayak_special_uses_soul_rend_projectile(void) {
    ZulrahState* state = (ZulrahState*)zul_create();
    state->gear_tier = 2;
    state->gear_tier_fixed = 2;
    zul_reset((EncounterState*)state, 123);
    state->surface_timer = 0;
    state->is_diving = 0;
    state->zulrah_visible = 1;
    state->player.attack_timer = 0;
    state->player_gear = ZUL_GEAR_MAGE;
    encounter_apply_loadout(&state->player, ZUL_MAGE_LOADOUT[2], GEAR_MAGE);
    state->player.special_energy = 100;
    zul_player_spec(state);

    EncounterOverlay ov;
    memset(&ov, 0, sizeof(ov));
    zul_render_post_tick((EncounterState*)state, &ov);
    assert(ov.projectile_count == 1);
    assert(ov.projectiles[0].launch_gfx_id == GFX_EYE_OF_AYAK_SPECIAL_CAST);
    assert(ov.projectiles[0].impact_gfx_id == GFX_EYE_OF_AYAK_SPECIAL_IMPACT);
    assert(ov.projectiles[0].model_id == 28450);
    assert(ov.projectiles[0].anim_id == 12398);

    zul_destroy((EncounterState*)state);
}

int main(void) {
    test_zulrah_eval_binding_bootstraps_3d_scene();
    test_nh_pvp_exposes_two_visible_fighters();
    test_nh_pvp_scripted_demo_ticks_both_sides();
    test_nh_pvp_respects_visual_tier_override();
    test_zulrah_uses_generated_cache_mapping();
    test_zulrah_animation_events_have_lifetimes();
    test_zulrah_primary_animations_are_single_tick_render_events();
    test_zulrah_attack_event_faces_player();
    test_zulrah_idle_and_walk_are_secondary_pose();
    test_zulrah_snakelings_are_render_entities_not_overlay_adds();
    test_zulrah_snakeling_slot_reuse_gets_new_render_identity();
    test_zulrah_loadouts_have_weapon_visual_contracts();
    test_zulrah_eye_of_ayak_uses_generated_item_stats();
    test_zulrah_eye_of_ayak_cannot_attack_past_item_range();
    test_zulrah_player_attack_projectiles_cover_mage_and_range();
    test_zulrah_melee_attack_does_not_emit_projectile();
    test_zulrah_eye_of_ayak_special_uses_soul_rend_projectile();
    return 0;
}
