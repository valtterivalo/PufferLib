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
    assert(strstr(source, "encounter_load_scene_assets(rc, &scene)"));
    assert(strstr(source, "OSRS_ASSET_GROUP_ZULRAH"));
    assert(strstr(source, "OSRS_ASSET_GROUP_COMBAT_VISUALS"));
    assert(strstr(source, "\"zulrah.cmap\""));
    assert(strstr(source, "\"zulrah.terrain\""));
    assert(strstr(source, "\"zulrah.objects\""));
    assert(strstr(source, "render_populate_entities(rc, re);"));
    assert(strstr(source, "rc->cam_target_x = (float)rc->arena_base_x"));
    assert(strstr(source, "render_seed_entity_visual_slot(rc, i);"));
    assert(strstr(source, "render_post_tick(rc, re);"));
    assert(strstr(source, "float tps = render_effective_ticks_per_second(rc);"));
    assert(strstr(source, "while (GetTime() < deadline)"));
    free(source);
}

static void test_render_inferno_debug_paths_guard_state_casts(void) {
    char* source = read_text_file("ocean/osrs/osrs_render.h");
    assert(strstr(source,
        "if (rc->npc_model_cache && rc->gui.encounter_state) {\n"
        "        InfernoState* is = (InfernoState*)rc->gui.encounter_state;") == NULL);
    assert(strstr(source,
        "if (rc->show_debug && rc->gui.encounter_state) {\n"
        "        InfernoState* is = (InfernoState*)rc->gui.encounter_state;") == NULL);
    assert(strstr(source,
        "if (rc->show_debug && p->entity_type == ENTITY_NPC && rc->gui.encounter_state) {\n"
        "            InfernoState* is = (InfernoState*)rc->gui.encounter_state;") == NULL);
    assert(strstr(source, "render_inferno_state_from_client(rc)") != NULL);
    free(source);
}

static void test_render_debug_overlay_has_no_long_camera_ray(void) {
    char* source = read_text_file("ocean/osrs/osrs_render.h");
    assert(strstr(source, "rc->debug_ray_dir.x * 50.0f") == NULL);
    assert(strstr(source, "render_ensure_entity_visual_slots(rc);") != NULL);
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

static void test_nh_pvp_head_move_drives_walk_dest(void) {
    /* HEAD_MOVE = 5 (one tile NW per ENCOUNTER_MOVE_TARGET_DX/DY) writes a
       walk_dest one tile NW of the player, and pvp_step actually moves them
       there via osrs_encounter_player_step. */
    NhPvpState* state = (NhPvpState*)nh_pvp_create();
    NhPvpContext context;
    memset(&context, 0, sizeof(context));
    nh_pvp_put_int((EncounterState*)state, (EncounterContext*)&context, "use_c_opponent", 1);
    nh_pvp_put_int((EncounterState*)state, (EncounterContext*)&context, "opponent_type", OPP_NONE);
    nh_pvp_put_int((EncounterState*)state, (EncounterContext*)&context, "is_lms", 1);
    nh_pvp_reset((EncounterState*)state, (EncounterContext*)&context, 123);

    int start_x = state->env.players[0].x;
    int start_y = state->env.players[0].y;
    int expected_x = start_x + ENCOUNTER_MOVE_TARGET_DX[5];
    int expected_y = start_y + ENCOUNTER_MOVE_TARGET_DY[5];

    int actions[NUM_ACTION_HEADS] = {0};
    actions[HEAD_MOVE] = 5;
    nh_pvp_step((EncounterState*)state, (EncounterContext*)&context, actions);

    assert(state->env.players[0].x == expected_x);
    assert(state->env.players[0].y == expected_y);

    nh_pvp_destroy((EncounterState*)state);
}

static void test_nh_pvp_walk_dest_persists_until_arrival(void) {
    /* Set walk_dest far enough that the player can't arrive in one tick.
       Confirm walk_dest is still set after one step and that an idle action
       on the next tick continues the BFS toward the destination. */
    NhPvpState* state = (NhPvpState*)nh_pvp_create();
    NhPvpContext context;
    memset(&context, 0, sizeof(context));
    nh_pvp_put_int((EncounterState*)state, (EncounterContext*)&context, "use_c_opponent", 1);
    nh_pvp_put_int((EncounterState*)state, (EncounterContext*)&context, "opponent_type", OPP_NONE);
    nh_pvp_put_int((EncounterState*)state, (EncounterContext*)&context, "is_lms", 1);
    nh_pvp_reset((EncounterState*)state, (EncounterContext*)&context, 123);

    /* manually set walk_dest 10 tiles east of player 0. shared SDK steps up
       to 2 tiles per tick toward it; far enough to require multiple ticks. */
    Player* p0 = &state->env.players[0];
    int target_x = p0->x + 10;
    int target_y = p0->y;
    state->env.pvp_runtime.walk_dest_x[0] = target_x;
    state->env.pvp_runtime.walk_dest_y[0] = target_y;

    int idle[NUM_ACTION_HEADS] = {0};
    int start_x = p0->x;
    nh_pvp_step((EncounterState*)state, (EncounterContext*)&context, idle);
    int after_first_step_x = state->env.players[0].x;
    assert(after_first_step_x > start_x);                                       /* moved east */
    assert(after_first_step_x < target_x);                                      /* not yet arrived */
    assert(state->env.pvp_runtime.walk_dest_x[0] == target_x);                   /* dest unchanged */

    /* tick 2 (idle) continues BFS walk via persistent walk_dest. */
    nh_pvp_step((EncounterState*)state, (EncounterContext*)&context, idle);
    int after_second_step_x = state->env.players[0].x;
    assert(after_second_step_x > after_first_step_x);                            /* moved again */
    assert(state->env.pvp_runtime.walk_dest_x[0] == target_x);

    nh_pvp_destroy((EncounterState*)state);
}

static void test_nh_pvp_head_move_mask_blocks_unwalkable_idle(void) {
    /* HEAD_MOVE = 0 is always valid (idle). other actions are masked by
       wilderness bounds + collision. confirm idle is set in the mask buffer. */
    NhPvpState* state = (NhPvpState*)nh_pvp_create();
    NhPvpContext context;
    memset(&context, 0, sizeof(context));
    nh_pvp_put_int((EncounterState*)state, (EncounterContext*)&context, "use_c_opponent", 1);
    nh_pvp_put_int((EncounterState*)state, (EncounterContext*)&context, "opponent_type", OPP_NONE);
    nh_pvp_put_int((EncounterState*)state, (EncounterContext*)&context, "is_lms", 1);
    nh_pvp_reset((EncounterState*)state, (EncounterContext*)&context, 123);
    compute_action_masks(&state->env, 0);

    int head_move_offset = LOADOUT_DIM + COMBAT_DIM + OVERHEAD_DIM
        + FOOD_DIM + POTION_DIM + KARAMBWAN_DIM + VENG_DIM + OFFENSIVE_DIM;
    unsigned char* mask = state->env.action_masks;
    assert(mask[head_move_offset + 0] == 1);  /* idle always valid */
    /* total mask size accounts for the 25-action move head */
    /* compute_action_masks writes ACTION_MASK_SIZE bytes per agent */
    assert(ACTION_MASK_SIZE == head_move_offset + MOVE_DIM);

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

static void test_zulrah_death_animation_renders_before_terminal(void) {
    ZulrahState* state = (ZulrahState*)zul_create();
    zul_reset((EncounterState*)state, 123);

    state->surface_timer = 0;
    state->is_diving = 0;
    state->zulrah_visible = 1;
    state->zulrah.npc_visible = 1;
    state->zulrah.current_hitpoints = 0;

    int actions[ZUL_NUM_ACTION_HEADS] = {0};
    zul_step((EncounterState*)state, actions);

    assert(state->episode_over == 0);
    assert(state->boss_killed_this_tick == 1);
    assert(state->zulrah.npc_anim_id == ZULRAH_ANIM_DEATH);
    assert(state->zulrah_anim_event_tick == state->tick);

    RenderEntity entities[8];
    int count = 0;
    zul_fill_render_entities((EncounterState*)state, entities, 8, &count);
    assert(count >= 2);
    assert(entities[1].npc_visible == 1);
    assert(entities[1].npc_anim_id == ZULRAH_ANIM_DEATH);

    for (int i = 0; i < ZUL_DEATH_ANIM_TICKS; i++)
        zul_step((EncounterState*)state, actions);

    assert(state->episode_over == 1);
    assert(state->winner == 0);

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

static void test_zulrah_cloud_events_animate_and_face_player(void) {
    ZulrahState* state = (ZulrahState*)zul_create();
    zul_reset((EncounterState*)state, 123);

    state->surface_timer = 0;
    state->is_diving = 0;
    state->zulrah_visible = 1;
    state->zulrah.npc_visible = 1;
    state->cloud_event_count = 0;
    zul_spawn_cloud(state);

    assert(state->cloud_event_count > 0);
    assert(state->zulrah.npc_anim_id == ZULRAH_ANIM_ATTACK);

    RenderEntity entities[8];
    int count = 0;
    zul_fill_render_entities((EncounterState*)state, entities, 8, &count);
    assert(count >= 2);
    assert(entities[1].attack_target_entity_idx == 0);
    assert(entities[1].npc_anim_id == ZULRAH_ANIM_ATTACK);

    zul_destroy((EncounterState*)state);
}

static void test_zulrah_idle_large_npc_faces_own_center_not_origin(void) {
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
    assert(entities[1].attack_target_entity_idx == -1);
    assert(entities[1].dest_x == state->zulrah.x + ZUL_NPC_SIZE / 2);
    assert(entities[1].dest_y == state->zulrah.y + ZUL_NPC_SIZE / 2);

    zul_destroy((EncounterState*)state);
}

static void test_zulrah_cloud_spawns_do_not_overlap_safe_footprints(void) {
    ZulrahState* state = (ZulrahState*)zul_create();
    zul_reset((EncounterState*)state, 123);

    int sx = ZUL_STAND_COORDS[ZUL_STAND_CENTER][0];
    int sy = ZUL_STAND_COORDS[ZUL_STAND_CENTER][1];
    assert(zul_cloud_overlaps_safe_area(
        sx - 2, sy, ZUL_STAND_CENTER, ZUL_STAND_NONE));
    assert(zul_cloud_overlaps_safe_area(
        sx, sy - 2, ZUL_STAND_CENTER, ZUL_STAND_NONE));
    assert(!zul_cloud_overlaps_safe_area(
        sx - 4, sy, ZUL_STAND_CENTER, ZUL_STAND_NONE));

    for (int stand = 0; stand < ZUL_NUM_STAND_LOCATIONS; stand++) {
        for (int seed = 1; seed < 256; seed++) {
            memset(state->clouds, 0, sizeof(state->clouds));
            memset(state->pending_clouds, 0, sizeof(state->pending_clouds));
            state->rng_state = (uint32_t)seed;
            int x = -1;
            int y = -1;
            if (zul_pick_cloud_pos(state, stand, ZUL_STAND_NONE, &x, &y))
                assert(!zul_cloud_overlaps_safe_area(x, y, stand, ZUL_STAND_NONE));
        }
    }

    zul_destroy((EncounterState*)state);
}

static void test_zulrah_cloud_obs_exposes_footprint_and_pending_target(void) {
    ZulrahState* state = (ZulrahState*)zul_create();
    zul_reset((EncounterState*)state, 123);

    memset(state->clouds, 0, sizeof(state->clouds));
    memset(state->pending_clouds, 0, sizeof(state->pending_clouds));
    state->player.x = 10;
    state->player.y = 10;
    state->clouds[0] = (ZulrahCloud){ .x = 9, .y = 9, .active = 1, .ticks_remaining = 12 };
    state->pending_clouds[0] = (ZulrahPendingCloud){ .x = 13, .y = 12, .delay = 3 };

    float obs[ZUL_NUM_OBS];
    zul_write_obs((EncounterState*)state, obs);

    assert(ZUL_NUM_OBS >= 88);
    assert(obs[ZUL_OBS_CLOUD_PLAYER_INSIDE] == 1.0f);
    assert(obs[ZUL_OBS_CLOUD_ACTIVE_COUNT] > 0.0f);
    assert(obs[ZUL_OBS_CLOUD_PENDING_COUNT] > 0.0f);
    assert(obs[ZUL_OBS_CLOUD_NEAREST_PENDING_DX] > 0.0f);
    assert(obs[ZUL_OBS_CLOUD_NEAREST_PENDING_DY] > 0.0f);
    assert(obs[ZUL_OBS_CLOUD_NEAREST_PENDING_DELAY] > 0.0f);

    zul_destroy((EncounterState*)state);
}

static void test_zulrah_cloud_obs_exposes_move_action_hazard(void) {
    ZulrahState* state = (ZulrahState*)zul_create();
    zul_reset((EncounterState*)state, 123);

    memset(state->clouds, 0, sizeof(state->clouds));
    memset(state->pending_clouds, 0, sizeof(state->pending_clouds));
    state->player.x = 10;
    state->player.y = 10;
    state->clouds[0] = (ZulrahCloud){ .x = 11, .y = 10, .active = 1, .ticks_remaining = 12 };
    state->pending_clouds[0] = (ZulrahPendingCloud){ .x = 9, .y = 10, .delay = 1 };

    assert(ZUL_NUM_OBS >= 123);
    float obs[ZUL_NUM_OBS];
    zul_write_obs((EncounterState*)state, obs);

    assert(obs[ZUL_OBS_MOVE_CLOUD_UNSAFE_START + 7] == 1.0f);
    assert(obs[ZUL_OBS_MOVE_CLOUD_UNSAFE_START + 2] == 1.0f);
    assert(obs[ZUL_OBS_MOVE_CLOUD_UNSAFE_START + 4] == 0.0f);

    zul_destroy((EncounterState*)state);
}

static void test_zulrah_cloud_tick_tracks_occupancy_and_damage(void) {
    ZulrahState* state = (ZulrahState*)zul_create();
    zul_reset((EncounterState*)state, 123);

    memset(state->clouds, 0, sizeof(state->clouds));
    memset(state->pending_clouds, 0, sizeof(state->pending_clouds));
    state->player.x = 10;
    state->player.y = 10;
    state->clouds[0] = (ZulrahCloud){ .x = 9, .y = 9, .active = 1, .ticks_remaining = 12 };
    state->pending_clouds[0] = (ZulrahPendingCloud){ .x = 13, .y = 12, .delay = 3 };

    zul_cloud_tick(state);

    assert(state->total_cloud_occupancy_ticks == 1);
    assert(state->total_active_cloud_ticks == 1);
    assert(state->total_pending_cloud_ticks == 1);
    assert(state->total_cloud_damage_received > 0);
    assert(state->total_damage_received == state->total_cloud_damage_received);

    zul_destroy((EncounterState*)state);
}

static void test_zulrah_cloud_occupancy_is_direct_reward_penalty(void) {
    ZulrahState* state = (ZulrahState*)zul_create();
    zul_reset((EncounterState*)state, 123);

    state->cloud_occupancy_this_tick = 0;
    float base_reward = zul_compute_reward(state);
    state->cloud_occupancy_this_tick = 1;
    float cloud_reward = zul_compute_reward(state);

    assert(base_reward == 0.0f);
    assert(cloud_reward < -0.03f);

    zul_destroy((EncounterState*)state);
}

static void test_zulrah_cloud_reward_penalty_is_configurable(void) {
    ZulrahState* state = (ZulrahState*)zul_create();
    zul_put_float((EncounterState*)state, "reward_cloud_occupancy_penalty", 0.2f);
    zul_reset((EncounterState*)state, 123);

    state->cloud_occupancy_this_tick = 1;
    float reward = zul_compute_reward(state);

    assert(reward < -0.19f);
    assert(reward > -0.21f);

    zul_destroy((EncounterState*)state);
}

static void test_zulrah_loss_reward_penalty_is_configurable(void) {
    ZulrahState* state = (ZulrahState*)zul_create();
    zul_put_float((EncounterState*)state, "reward_loss_penalty", 0.4f);
    zul_reset((EncounterState*)state, 123);

    state->player_lost_this_tick = 1;
    float reward = zul_compute_reward(state);

    assert(reward < -0.39f);
    assert(reward > -0.41f);

    zul_destroy((EncounterState*)state);
}

static void test_zulrah_score_ignores_reward_penalty_knobs(void) {
    ZulrahState* state = (ZulrahState*)zul_create();
    zul_put_float((EncounterState*)state, "reward_cloud_occupancy_penalty", 0.2f);
    zul_reset((EncounterState*)state, 123);

    state->episode_over = 1;
    state->winner = 0;
    state->tick = ZUL_MAX_TICKS / 2;
    state->gear_tier = 0;
    state->total_damage_received = 999.0f;
    state->total_cloud_occupancy_ticks = 99;
    Log* log = (Log*)zul_get_log((EncounterState*)state);
    float expected = 1.0f + 0.5f * ZUL_SCORE_SPEED_BONUS_DEFAULT;

    assert(log->zulrah_tier_score_sum[0] > expected - 0.001f);
    assert(log->zulrah_tier_score_sum[0] < expected + 0.001f);

    zul_destroy((EncounterState*)state);
}

static void test_zulrah_trip_mode_respawns_boss_after_kill(void) {
    ZulrahState* state = (ZulrahState*)zul_create();
    zul_put_int((EncounterState*)state, "episode_mode", ZUL_EPISODE_TRIP);
    zul_reset((EncounterState*)state, 123);

    state->tick = 100;
    state->zulrah.current_hitpoints = 0;
    zul_record_boss_kill(state);

    assert(state->episode_over == 0);
    assert(state->kills_this_episode == 1);
    assert(state->respawn_timer == 0);
    assert(state->zulrah_death_ticks == ZUL_DEATH_ANIM_TICKS);
    assert(state->zulrah_visible == 1);

    int actions[ZUL_NUM_ACTION_HEADS] = {0};
    for (int i = 0; i < ZUL_DEATH_ANIM_TICKS; i++)
        zul_step((EncounterState*)state, actions);

    assert(state->episode_over == 0);
    assert(state->respawn_timer == ZUL_TRIP_RESPAWN_DELAY_TICKS);
    assert(state->zulrah_visible == 0);

    for (int i = 0; i < ZUL_TRIP_RESPAWN_DELAY_TICKS; i++)
        zul_step((EncounterState*)state, actions);

    assert(state->episode_over == 0);
    assert(state->zulrah_visible == 1);
    assert(state->zulrah.current_hitpoints == MONSTER_DATABASE[MON_ZULRAH_GREEN].hp);

    zul_destroy((EncounterState*)state);
}

static void test_zulrah_trip_score_counts_kills_partial_progress_and_speed(void) {
    ZulrahState* state = (ZulrahState*)zul_create();
    zul_put_int((EncounterState*)state, "episode_mode", ZUL_EPISODE_TRIP);
    zul_reset((EncounterState*)state, 123);

    state->tick = 100;
    state->zulrah.current_hitpoints = 0;
    zul_record_boss_kill(state);
    zul_start_active_kill(state);
    state->tick = 180;
    state->zulrah.current_hitpoints = MONSTER_DATABASE[MON_ZULRAH_GREEN].hp / 2;
    zul_record_episode_timeout(state);

    Log* log = (Log*)zul_get_log((EncounterState*)state);

    assert(log->zulrah_tier_wins[state->gear_tier] == 1.0f);
    assert(log->zulrah_tier_score_sum[state->gear_tier] > 1.5f);
    assert(log->zulrah_kills == 1.0f);

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

static void test_zulrah_max_tier_starts_with_saturated_heart_boost(void) {
    ZulrahState* state = (ZulrahState*)zul_create();
    state->gear_tier = 2;
    state->gear_tier_fixed = 2;
    zul_reset((EncounterState*)state, 123);

    assert(state->player.base_attack == 99);
    assert(state->player.current_attack == 99);
    assert(state->player.base_magic == 99);
    assert(state->player.current_magic == 112);
    assert(state->player.saturated_heart_count == 1);
    assert(state->mage_stats.eff_level == 152);

    zul_destroy((EncounterState*)state);
}

static void test_zulrah_saturated_heart_boost_holds_until_timer_expires(void) {
    ZulrahState* state = (ZulrahState*)zul_create();
    state->gear_tier = 2;
    state->gear_tier_fixed = 2;
    zul_reset((EncounterState*)state, 123);

    int actions[ZUL_NUM_ACTION_HEADS] = {0};
    state->tick = 59;
    zul_step((EncounterState*)state, actions);

    assert(state->player.current_magic == 112);
    assert(state->player.saturated_heart_active_ticks == 499);
    assert(state->mage_stats.eff_level == 152);

    state->player.saturated_heart_active_ticks = 1;
    state->tick = 499;
    zul_step((EncounterState*)state, actions);

    assert(state->player.current_magic == 99);
    assert(state->player.saturated_heart_active_ticks == 0);
    assert(state->mage_stats.eff_level == 135);

    zul_destroy((EncounterState*)state);
}

static void test_zulrah_eye_of_ayak_uses_weapon_hit_delay_table(void) {
    for (int dist = 1; dist <= 8; dist++) {
        EncounterProjectileTiming timing = zul_player_projectile_timing(
            ATTACK_STYLE_MAGIC, ITEM_EYE_OF_AYAK, 0, dist);
        int expected_delay = dist <= 2 ? 1 : 2;
        assert(timing.damage_delay_ticks == expected_delay);
        assert(timing.visual_duration_ticks == expected_delay);
    }

    EncounterProjectileTiming special_timing = zul_player_projectile_timing(
        ATTACK_STYLE_MAGIC, ITEM_EYE_OF_AYAK, 1, 6);
    assert(special_timing.damage_delay_ticks == 2);
    assert(special_timing.visual_duration_ticks == 2);
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

static int zul_food_mask_offset(void) {
    return ZUL_MOVE_DIM + ZUL_ATTACK_DIM + ZUL_PRAYER_DIM;
}

static void test_zulrah_allows_shark_then_karambwan_combo_eat(void) {
    ZulrahState* state = (ZulrahState*)zul_create();
    state->gear_tier = 0;
    state->gear_tier_fixed = 0;
    zul_reset((EncounterState*)state, 123);

    state->surface_timer = 100;
    state->player.current_hitpoints = 40;
    state->player.food_count = 1;
    state->player.karambwan_count = 1;
    state->player.food_timer = 0;
    state->player.karambwan_timer = 0;
    state->player.potion_timer = 0;

    int actions[ZUL_NUM_ACTION_HEADS] = {0};
    actions[ZUL_HEAD_FOOD] = 1;
    zul_step((EncounterState*)state, actions);
    assert(state->player.current_hitpoints == 60);
    assert(state->player.food_count == 0);
    assert(state->player.food_timer == 3);
    assert(state->player.ate_food_this_tick == 1);

    float mask[ZUL_ACTION_MASK_SIZE];
    zul_write_mask((EncounterState*)state, mask);
    int food_off = zul_food_mask_offset();
    assert(mask[food_off + 1] == 0.0f);
    assert(mask[food_off + 2] == 1.0f);

    memset(actions, 0, sizeof(actions));
    actions[ZUL_HEAD_FOOD] = 2;
    zul_step((EncounterState*)state, actions);
    assert(state->player.current_hitpoints == 78);
    assert(state->player.karambwan_count == 0);
    assert(state->player.food_timer == 3);
    assert(state->player.karambwan_timer == 2);
    assert(state->player.potion_timer == 3);
    assert(state->player.ate_food_this_tick == 0);
    assert(state->player.ate_karambwan_this_tick == 1);

    zul_write_mask((EncounterState*)state, mask);
    assert(mask[food_off + 2] == 0.0f);

    zul_destroy((EncounterState*)state);
}

typedef struct {
    int target_x;
    int target_y;
    int target_size;
    int arena_w;
    int arena_h;
} TestAttackApproachArena;

static int test_attack_approach_walkable(void* ctx, int x, int y) {
    TestAttackApproachArena* arena = (TestAttackApproachArena*)ctx;
    if (x < 0 || y < 0 || x >= arena->arena_w || y >= arena->arena_h) return 0;
    return !encounter_entity_footprints_overlap(
        x, y, 1, arena->target_x, arena->target_y, arena->target_size);
}

static int test_attack_approach_blocks_adjacent_ring(void* ctx, int x, int y) {
    TestAttackApproachArena* arena = (TestAttackApproachArena*)ctx;
    return encounter_entity_footprint_distance(
        x, y, 1, arena->target_x, arena->target_y, arena->target_size) == 1;
}

static void test_arena_attack_approach_uses_ranged_tiles_not_adjacent_ring(void) {
    TestAttackApproachArena arena = {
        .target_x = 8,
        .target_y = 8,
        .target_size = 5,
        .arena_w = 20,
        .arena_h = 20,
    };

    PathResult path = encounter_pathfind_arena_attack_approach(
        NULL, 0, 0,
        1, 10,
        arena.target_x, arena.target_y, arena.target_size, 6,
        test_attack_approach_walkable, &arena,
        test_attack_approach_blocks_adjacent_ring, &arena,
        NULL, 0,
        0, 0, arena.arena_w, arena.arena_h);

    assert(path.found);
    assert(path.next_dx != 0 || path.next_dy != 0);
    assert(encounter_player_can_attack(
        path.dest_x, path.dest_y,
        arena.target_x, arena.target_y, arena.target_size, 6,
        NULL, 0));
}

static void test_zulrah_attack_click_chases_into_range(void) {
    ZulrahState* state = (ZulrahState*)zul_create();
    state->gear_tier = 2;
    state->gear_tier_fixed = 2;
    zul_reset((EncounterState*)state, 123);

    state->surface_timer = 0;
    state->phase_timer = 100;
    state->is_diving = 0;
    state->zulrah_visible = 1;
    state->zulrah.npc_visible = 1;
    state->zulrah.x = ZUL_POSITIONS[ZUL_POS_NORTH][0];
    state->zulrah.y = ZUL_POSITIONS[ZUL_POS_NORTH][1];
    state->player.x = 22;
    state->player.y = 22;
    state->player.dest_x = state->player.x;
    state->player.dest_y = state->player.y;
    state->player.attack_timer = 0;
    state->player_gear = ZUL_GEAR_MAGE;
    encounter_apply_loadout(&state->player, ZUL_MAGE_LOADOUT[2], GEAR_MAGE);

    int start_dist = encounter_entity_footprint_distance(
        state->player.x, state->player.y, 1,
        state->zulrah.x, state->zulrah.y, ZUL_NPC_SIZE);

    HumanInput hi;
    human_input_init(&hi);
    human_input_queue_attack_npc(&hi, 0);
    zul_step_human_commands((EncounterState*)state, &hi);

    int after_dist = encounter_entity_footprint_distance(
        state->player.x, state->player.y, 1,
        state->zulrah.x, state->zulrah.y, ZUL_NPC_SIZE);
    assert(after_dist < start_dist);
    assert(osrs_interaction_active(&state->interaction));
    assert(state->player_moved_this_tick == 1);
    assert(state->player_attacked_this_tick == 1);

    RenderEntity entities[8];
    int count = 0;
    zul_fill_render_entities((EncounterState*)state, entities, 8, &count);
    assert(count >= 2);
    assert(entities[0].attack_target_entity_idx == 1);

    int actions[ZUL_NUM_ACTION_HEADS] = {0};
    int attacked = state->player_attacked_this_tick;
    for (int i = 0; i < 8 && !attacked; i++) {
        zul_step((EncounterState*)state, actions);
        attacked = state->player_attacked_this_tick;
    }
    assert(attacked);
    zul_fill_render_entities((EncounterState*)state, entities, 8, &count);
    assert(count >= 2);
    assert(entities[0].attack_target_entity_idx == 1);

    human_input_destroy(&hi);
    zul_destroy((EncounterState*)state);
}

static void test_zulrah_chase_between_attacks_faces_target(void) {
    ZulrahState* state = (ZulrahState*)zul_create();
    state->gear_tier = 2;
    state->gear_tier_fixed = 2;
    zul_reset((EncounterState*)state, 123);

    state->surface_timer = 0;
    state->phase_timer = 100;
    state->is_diving = 0;
    state->zulrah_visible = 1;
    state->zulrah.npc_visible = 1;
    state->zulrah.x = ZUL_POSITIONS[ZUL_POS_NORTH][0];
    state->zulrah.y = ZUL_POSITIONS[ZUL_POS_NORTH][1];
    state->player.x = 22;
    state->player.y = 22;
    state->player.dest_x = state->player.x;
    state->player.dest_y = state->player.y;
    state->player.attack_timer = 2;
    state->player_gear = ZUL_GEAR_MAGE;
    encounter_apply_loadout(&state->player, ZUL_MAGE_LOADOUT[2], GEAR_MAGE);
    osrs_interaction_set(&state->interaction, 0);

    int actions[ZUL_NUM_ACTION_HEADS] = {0};
    zul_step((EncounterState*)state, actions);

    assert(osrs_interaction_active(&state->interaction));
    assert(state->player_moved_this_tick == 1);
    assert(state->player_attacked_this_tick == 0);

    RenderEntity entities[8];
    int count = 0;
    zul_fill_render_entities((EncounterState*)state, entities, 8, &count);
    assert(count >= 2);
    assert(entities[0].attack_target_entity_idx == 1);

    zul_destroy((EncounterState*)state);
}

static void test_zulrah_attack_click_paths_to_ranged_tile_around_arena_obstacles(void) {
    CollisionMap* cmap = collision_map_load(OSRS_ASSET("zulrah.cmap"));
    assert(cmap != NULL);

    ZulrahState* state = (ZulrahState*)zul_create();
    state->gear_tier = 2;
    state->gear_tier_fixed = 2;
    state->collision_map = cmap;
    state->world_offset_x = 2256;
    state->world_offset_y = 3061;
    zul_reset((EncounterState*)state, 123);

    state->surface_timer = 0;
    state->phase_timer = 100;
    state->is_diving = 0;
    state->zulrah_visible = 1;
    state->zulrah.npc_visible = 1;
    state->zulrah.x = ZUL_POSITIONS[ZUL_POS_EAST][0];
    state->zulrah.y = ZUL_POSITIONS[ZUL_POS_EAST][1];
    state->player.x = 8;
    state->player.y = 16;
    state->player.dest_x = state->player.x;
    state->player.dest_y = state->player.y;
    state->player.attack_timer = 2;
    state->player_gear = ZUL_GEAR_MAGE;
    encounter_apply_loadout(&state->player, ZUL_MAGE_LOADOUT[2], GEAR_MAGE);
    osrs_interaction_set(&state->interaction, 0);

    const EncounterLoadoutStats* loadout = zul_current_loadout_stats(state, 1);
    assert(!encounter_player_can_attack(
        state->player.x, state->player.y,
        state->zulrah.x, state->zulrah.y, ZUL_NPC_SIZE,
        loadout->attack_range, NULL, 0));

    int moved = 0;
    int actions[ZUL_NUM_ACTION_HEADS] = {0};
    for (int i = 0; i < 16; i++) {
        int prev_x = state->player.x;
        int prev_y = state->player.y;
        zul_step((EncounterState*)state, actions);
        moved += state->player.x != prev_x || state->player.y != prev_y;
        if (encounter_player_can_attack(
                state->player.x, state->player.y,
                state->zulrah.x, state->zulrah.y, ZUL_NPC_SIZE,
                loadout->attack_range, NULL, 0))
            break;
    }

    assert(moved > 0);
    assert(encounter_player_can_attack(
        state->player.x, state->player.y,
        state->zulrah.x, state->zulrah.y, ZUL_NPC_SIZE,
        loadout->attack_range, NULL, 0));

    zul_destroy((EncounterState*)state);
    collision_map_free(cmap);
}

static void test_zulrah_human_walk_click_persists_until_destination(void) {
    CollisionMap* cmap = collision_map_load(OSRS_ASSET("zulrah.cmap"));
    assert(cmap != NULL);

    ZulrahState* state = (ZulrahState*)zul_create();
    state->collision_map = cmap;
    state->world_offset_x = 2256;
    state->world_offset_y = 3061;
    zul_reset((EncounterState*)state, 123);

    state->surface_timer = 0;
    state->phase_timer = 100;
    state->is_diving = 0;
    state->zulrah_visible = 1;
    state->zulrah.npc_visible = 1;
    state->player.x = 8;
    state->player.y = 16;
    state->player.dest_x = state->player.x;
    state->player.dest_y = state->player.y;

    HumanInput hi;
    human_input_init(&hi);
    hi.pending_move_x = 16;
    hi.pending_move_y = 9;
    human_input_queue_walk(&hi, hi.pending_move_x, hi.pending_move_y);
    zul_step_human_commands((EncounterState*)state, &hi);

    int after_first_x = state->player.x;
    int after_first_y = state->player.y;
    assert(after_first_x != 8 || after_first_y != 16);

    zul_step_human_commands((EncounterState*)state, &hi);
    assert(state->player.x != after_first_x || state->player.y != after_first_y);

    human_input_destroy(&hi);
    zul_destroy((EncounterState*)state);
    collision_map_free(cmap);
}

static void test_zulrah_explicit_movement_keeps_safe_spot_priority(void) {
    ZulrahState* state = (ZulrahState*)zul_create();
    state->gear_tier = 2;
    state->gear_tier_fixed = 2;
    zul_reset((EncounterState*)state, 123);

    state->surface_timer = 0;
    state->phase_timer = 100;
    state->is_diving = 0;
    state->zulrah_visible = 1;
    state->zulrah.npc_visible = 1;
    state->zulrah.x = ZUL_POSITIONS[ZUL_POS_NORTH][0];
    state->zulrah.y = ZUL_POSITIONS[ZUL_POS_NORTH][1];
    state->player.x = 22;
    state->player.y = 22;
    state->player.dest_x = state->player.x;
    state->player.dest_y = state->player.y;
    state->player.attack_timer = 0;
    state->player_gear = ZUL_GEAR_MAGE;
    encounter_apply_loadout(&state->player, ZUL_MAGE_LOADOUT[2], GEAR_MAGE);

    state->player_dest_x = 22;
    state->player_dest_y = 20;
    state->player_dest_explicit = 1;
    int actions[ZUL_NUM_ACTION_HEADS] = {0};
    actions[ZUL_HEAD_ATTACK] = ZUL_ATK_MAGE;
    zul_step((EncounterState*)state, actions);

    assert(state->player.x == 22);
    assert(state->player.y == 20);
    assert(osrs_interaction_active(&state->interaction));
    assert(state->player_moved_this_tick == 1);

    RenderEntity entities[8];
    int count = 0;
    zul_fill_render_entities((EncounterState*)state, entities, 8, &count);
    assert(count >= 2);
    assert(entities[0].attack_target_entity_idx == -1);

    zul_destroy((EncounterState*)state);
}

static void test_zulrah_pure_movement_does_not_face_target(void) {
    ZulrahState* state = (ZulrahState*)zul_create();
    state->gear_tier = 2;
    state->gear_tier_fixed = 2;
    zul_reset((EncounterState*)state, 123);

    state->surface_timer = 0;
    state->phase_timer = 100;
    state->is_diving = 0;
    state->zulrah_visible = 1;
    state->zulrah.npc_visible = 1;
    state->player.x = 18;
    state->player.y = 16;
    state->player.dest_x = state->player.x;
    state->player.dest_y = state->player.y;
    osrs_interaction_clear(&state->interaction);

    HumanInput hi;
    human_input_init(&hi);
    human_input_queue_walk(&hi, state->player.x + 1, state->player.y);
    zul_step_human_commands((EncounterState*)state, &hi);

    assert(!osrs_interaction_active(&state->interaction));
    assert(state->player_moved_this_tick == 1);

    RenderEntity entities[8];
    int count = 0;
    zul_fill_render_entities((EncounterState*)state, entities, 8, &count);
    assert(count >= 2);
    assert(entities[0].attack_target_entity_idx == -1);

    human_input_destroy(&hi);
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

static void test_zulrah_projectile_events_use_generated_gfx_animation_rows(void) {
    ZulrahState* state = (ZulrahState*)zul_create();
    zul_reset((EncounterState*)state, 123);
    state->surface_timer = 0;
    state->is_diving = 0;
    state->zulrah_visible = 1;

    zul_record_attack(state, state->zulrah.x, state->zulrah.y,
        state->player.x, state->player.y, 0, 12);
    zul_record_attack(state, state->zulrah.x, state->zulrah.y,
        state->player.x, state->player.y, 1, 12);
    state->cloud_event_count = 1;
    state->cloud_events[0].src_x = state->zulrah.x;
    state->cloud_events[0].src_y = state->zulrah.y;
    state->cloud_events[0].dst_x = state->player.x;
    state->cloud_events[0].dst_y = state->player.y;
    state->cloud_events[0].flight_ticks = 3;
    zul_spawn_snakeling(state);

    EncounterOverlay ov;
    memset(&ov, 0, sizeof(ov));
    zul_render_post_tick((EncounterState*)state, &ov);

    int saw_range = 0;
    int saw_magic = 0;
    int saw_cloud = 0;
    int saw_snakeling = 0;
    for (int i = 0; i < ov.projectile_count; i++) {
        if (ov.projectiles[i].model_id == ZUL_GEN_GFX_1044_MODEL) {
            assert(ov.projectiles[i].anim_id == ZUL_GEN_GFX_1044_ANIM);
            saw_range = 1;
        }
        if (ov.projectiles[i].model_id == ZUL_GEN_GFX_1046_MODEL) {
            assert(ov.projectiles[i].anim_id == ZUL_GEN_GFX_1046_ANIM);
            saw_magic = 1;
        }
        if (ov.projectiles[i].model_id == ZUL_GEN_GFX_1045_MODEL) {
            assert(ov.projectiles[i].anim_id == ZUL_GEN_GFX_1045_ANIM);
            saw_cloud = 1;
        }
        if (ov.projectiles[i].model_id == ZUL_GEN_GFX_1047_MODEL) {
            assert(ov.projectiles[i].anim_id == ZUL_GEN_GFX_1047_ANIM);
            saw_snakeling = 1;
        }
    }
    assert(saw_range);
    assert(saw_magic);
    assert(saw_cloud);
    assert(saw_snakeling);

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
    test_render_inferno_debug_paths_guard_state_casts();
    test_render_debug_overlay_has_no_long_camera_ray();
    test_nh_pvp_exposes_two_visible_fighters();
    test_nh_pvp_scripted_demo_ticks_both_sides();
    test_nh_pvp_respects_visual_tier_override();
    test_nh_pvp_head_move_drives_walk_dest();
    test_nh_pvp_walk_dest_persists_until_arrival();
    test_nh_pvp_head_move_mask_blocks_unwalkable_idle();
    test_zulrah_uses_generated_cache_mapping();
    test_zulrah_animation_events_have_lifetimes();
    test_zulrah_primary_animations_are_single_tick_render_events();
    test_zulrah_death_animation_renders_before_terminal();
    test_zulrah_attack_event_faces_player();
    test_zulrah_cloud_events_animate_and_face_player();
    test_zulrah_idle_large_npc_faces_own_center_not_origin();
    test_zulrah_cloud_spawns_do_not_overlap_safe_footprints();
    test_zulrah_cloud_obs_exposes_footprint_and_pending_target();
    test_zulrah_cloud_obs_exposes_move_action_hazard();
    test_zulrah_cloud_tick_tracks_occupancy_and_damage();
    test_zulrah_cloud_occupancy_is_direct_reward_penalty();
    test_zulrah_cloud_reward_penalty_is_configurable();
    test_zulrah_loss_reward_penalty_is_configurable();
    test_zulrah_score_ignores_reward_penalty_knobs();
    test_zulrah_trip_mode_respawns_boss_after_kill();
    test_zulrah_trip_score_counts_kills_partial_progress_and_speed();
    test_zulrah_idle_and_walk_are_secondary_pose();
    test_zulrah_snakelings_are_render_entities_not_overlay_adds();
    test_zulrah_snakeling_slot_reuse_gets_new_render_identity();
    test_zulrah_loadouts_have_weapon_visual_contracts();
    test_zulrah_eye_of_ayak_uses_generated_item_stats();
    test_zulrah_max_tier_starts_with_saturated_heart_boost();
    test_zulrah_saturated_heart_boost_holds_until_timer_expires();
    test_zulrah_eye_of_ayak_uses_weapon_hit_delay_table();
    test_zulrah_eye_of_ayak_cannot_attack_past_item_range();
    test_zulrah_allows_shark_then_karambwan_combo_eat();
    test_arena_attack_approach_uses_ranged_tiles_not_adjacent_ring();
    test_zulrah_attack_click_chases_into_range();
    test_zulrah_chase_between_attacks_faces_target();
    test_zulrah_attack_click_paths_to_ranged_tile_around_arena_obstacles();
    test_zulrah_human_walk_click_persists_until_destination();
    test_zulrah_explicit_movement_keeps_safe_spot_priority();
    test_zulrah_pure_movement_does_not_face_target();
    test_zulrah_player_attack_projectiles_cover_mage_and_range();
    test_zulrah_melee_attack_does_not_emit_projectile();
    test_zulrah_projectile_events_use_generated_gfx_animation_rows();
    test_zulrah_eye_of_ayak_special_uses_soul_rend_projectile();
    return 0;
}
