#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ocean/osrs/osrs_encounter_player.h"
#include "ocean/osrs/osrs_encounter_visual_events.h"

typedef struct {
    int target_x;
    int target_y;
    int target_size;
    int arena_w;
    int arena_h;
} SdkStepArena;

static int sdk_tile_walkable(void* ctx, int x, int y) {
    SdkStepArena* arena = (SdkStepArena*)ctx;
    if (x < 0 || y < 0 || x >= arena->arena_w || y >= arena->arena_h)
        return 0;
    return !encounter_entity_footprints_overlap(
        x, y, 1, arena->target_x, arena->target_y, arena->target_size);
}

static int sdk_lookup_target(void* ctx, int target_slot, OsrsAttackTarget* out) {
    SdkStepArena* arena = (SdkStepArena*)ctx;
    if (target_slot != 7) return 0;
    *out = (OsrsAttackTarget){
        .slot = target_slot,
        .x = arena->target_x,
        .y = arena->target_y,
        .size = arena->target_size,
        .attack_range = 6,
    };
    return 1;
}

static void test_shared_player_step_attack_click_chases_to_range(void) {
    SdkStepArena arena = {
        .target_x = 12,
        .target_y = 10,
        .target_size = 5,
        .arena_w = 25,
        .arena_h = 25,
    };
    Player player;
    memset(&player, 0, sizeof(player));
    player.x = 1;
    player.y = 10;
    player.dest_x = player.x;
    player.dest_y = player.y;
    OsrsInteraction interaction;
    osrs_interaction_init(&interaction);

    int dest_x = -1;
    int dest_y = -1;
    OsrsPlayerStepInput input = {
        .player = &player,
        .interaction = &interaction,
        .target_lookup = sdk_lookup_target,
        .target_ctx = &arena,
        .has_new_target = 1,
        .new_target_slot = 7,
        .move_kind = OSRS_PLAYER_MOVE_NONE,
        .dest_x = &dest_x,
        .dest_y = &dest_y,
        .arena = {
            .collision_map = NULL,
            .world_offset_x = 0,
            .world_offset_y = 0,
            .is_walkable = sdk_tile_walkable,
            .walkable_ctx = &arena,
            .arena_base_x = 0,
            .arena_base_y = 0,
            .arena_w = arena.arena_w,
            .arena_h = arena.arena_h,
        },
    };

    OsrsPlayerStepResult result = osrs_encounter_player_step(&input);
    assert(result.moved == 1);
    assert(result.chased_target == 1);
    assert(result.interaction_active == 1);
    assert(result.target_slot == 7);
    assert(player.x > 1);
}

static void test_shared_player_step_ground_move_interrupts_interaction(void) {
    SdkStepArena arena = {
        .target_x = 12,
        .target_y = 10,
        .target_size = 5,
        .arena_w = 25,
        .arena_h = 25,
    };
    Player player;
    memset(&player, 0, sizeof(player));
    player.x = 4;
    player.y = 4;
    OsrsInteraction interaction;
    osrs_interaction_init(&interaction);
    osrs_interaction_set(&interaction, 7);

    int dest_x = 8;
    int dest_y = 4;
    OsrsPlayerStepInput input = {
        .player = &player,
        .interaction = &interaction,
        .target_lookup = sdk_lookup_target,
        .target_ctx = &arena,
        .move_kind = OSRS_PLAYER_MOVE_DESTINATION,
        .dest_x = &dest_x,
        .dest_y = &dest_y,
        .arena = {
            .collision_map = NULL,
            .world_offset_x = 0,
            .world_offset_y = 0,
            .is_walkable = sdk_tile_walkable,
            .walkable_ctx = &arena,
            .arena_base_x = 0,
            .arena_base_y = 0,
            .arena_w = arena.arena_w,
            .arena_h = arena.arena_h,
        },
    };

    OsrsPlayerStepResult result = osrs_encounter_player_step(&input);
    assert(result.explicit_moved == 1);
    assert(result.chased_target == 0);
    assert(result.interaction_active == 0);
    assert(osrs_interaction_active(&interaction) == 0);
    assert(player.x > 4);
}

static void test_shared_player_step_targeted_move_can_preserve_safe_spot(void) {
    SdkStepArena arena = {
        .target_x = 12,
        .target_y = 10,
        .target_size = 5,
        .arena_w = 25,
        .arena_h = 25,
    };
    Player player;
    memset(&player, 0, sizeof(player));
    player.x = 4;
    player.y = 4;
    player.dest_x = player.x;
    player.dest_y = player.y;
    OsrsInteraction interaction;
    osrs_interaction_init(&interaction);

    int dest_x = 4;
    int dest_y = 6;
    OsrsPlayerStepInput input = {
        .player = &player,
        .interaction = &interaction,
        .target_lookup = sdk_lookup_target,
        .target_ctx = &arena,
        .has_new_target = 1,
        .new_target_slot = 7,
        .move_kind = OSRS_PLAYER_MOVE_DESTINATION,
        .target_move_policy = OSRS_PLAYER_TARGET_MOVE_EXPLICIT_FIRST,
        .dest_x = &dest_x,
        .dest_y = &dest_y,
        .arena = {
            .collision_map = NULL,
            .world_offset_x = 0,
            .world_offset_y = 0,
            .is_walkable = sdk_tile_walkable,
            .walkable_ctx = &arena,
            .arena_base_x = 0,
            .arena_base_y = 0,
            .arena_w = arena.arena_w,
            .arena_h = arena.arena_h,
        },
    };

    OsrsPlayerStepResult result = osrs_encounter_player_step(&input);
    assert(result.explicit_moved == 1);
    assert(result.chased_target == 0);
    assert(result.interaction_active == 1);
    assert(result.target_slot == 7);
    assert(osrs_interaction_active(&interaction) == 1);
    assert(player.x == 4);
    assert(player.y == 6);
}

static void test_shared_render_entity_suppresses_persistent_pose_anims(void) {
    Player npc;
    memset(&npc, 0, sizeof(npc));
    npc.entity_type = ENTITY_NPC;
    npc.npc_def_id = 2042;
    npc.npc_visible = 1;
    npc.npc_size = 5;
    npc.npc_anim_id = 100;
    npc.x = 10;
    npc.y = 20;

    RenderEntity entity;
    osrs_render_entity_from_npc_player(&npc, &entity, 3, 55);
    osrs_render_entity_suppress_pose_anims(&entity, 100, 101);
    assert(entity.npc_anim_id == -1);
    assert(entity.npc_slot == 3);
    assert(entity.npc_instance_id == 55);
}

static void test_shared_player_render_entity_resets_stale_fields(void) {
    Player player;
    memset(&player, 0, sizeof(player));
    player.entity_type = ENTITY_PLAYER;
    player.x = 4;
    player.y = 5;
    player.dest_x = 6;
    player.dest_y = 7;

    RenderEntity entity;
    memset(&entity, 0x7f, sizeof(entity));
    osrs_render_entity_from_player_entity(&player, &entity);

    assert(entity.entity_type == ENTITY_PLAYER);
    assert(entity.x == 4);
    assert(entity.dest_y == 7);
    assert(entity.npc_slot == -1);
    assert(entity.npc_instance_id == 0);
    assert(entity.attack_target_entity_idx == -1);
    assert(entity.hit_spell_type == 0);
}

static void test_shared_render_entity_from_npc_spec_sets_identity_and_hits(void) {
    RenderEntity entity;
    osrs_render_entity_from_npc_spec(&(OsrsNpcRenderEntitySpec){
        .npc_def_id = 7700,
        .npc_slot = 8,
        .npc_instance_id = 123,
        .npc_visible = 1,
        .npc_size = 3,
        .npc_anim_id = 265,
        .x = 11,
        .y = 12,
        .dest_x = 13,
        .dest_y = 14,
        .current_hitpoints = 17,
        .base_hitpoints = 40,
        .attack_style_this_tick = ATTACK_STYLE_MAGIC,
        .hit_landed_this_tick = 1,
        .hit_damage = 9,
        .hit_was_successful = 1,
        .hit_spell_type = ENCOUNTER_SPELL_ICE,
        .attack_target_entity_idx = 0,
    }, &entity);

    assert(entity.entity_type == ENTITY_NPC);
    assert(entity.npc_def_id == 7700);
    assert(entity.npc_slot == 8);
    assert(entity.npc_instance_id == 123);
    assert(entity.npc_visible == 1);
    assert(entity.npc_size == 3);
    assert(entity.npc_anim_id == 265);
    assert(entity.x == 11);
    assert(entity.dest_y == 14);
    assert(entity.current_hitpoints == 17);
    assert(entity.attack_style_this_tick == ATTACK_STYLE_MAGIC);
    assert(entity.hit_spell_type == ENCOUNTER_SPELL_ICE);
    assert(entity.attack_target_entity_idx == 0);
    assert(entity.equipped[0] == ITEM_NONE);
}

static void test_shared_death_linger_starts_once_and_ticks_to_finish(void) {
    int death_ticks = 0;
    assert(osrs_npc_death_linger_start(0, 1, &death_ticks, 3) == 1);
    assert(death_ticks == 3);
    assert(osrs_npc_death_linger_start(0, 1, &death_ticks, 3) == 0);
    assert(death_ticks == 3);
    assert(osrs_npc_death_linger_tick(&death_ticks) == 0);
    assert(death_ticks == 2);
    assert(osrs_npc_death_linger_tick(&death_ticks) == 0);
    assert(death_ticks == 1);
    assert(osrs_npc_death_linger_tick(&death_ticks) == 1);
    assert(death_ticks == 0);
}

static void test_shared_projectile_builders_require_anchors(void) {
    EncounterOverlay overlay;
    memset(&overlay, 0, sizeof(overlay));

    OsrsProjectileEventSpec spec = {
        .src_x = 1,
        .src_y = 2,
        .dst_x = 10,
        .dst_y = 11,
        .style = 1,
        .damage = 7,
        .duration_ticks = 90,
        .start_h = 120,
        .end_h = 80,
        .curve = 16,
        .arc_height = 1.0f,
        .src_size = 1,
        .dst_size = 5,
        .model_id = 1234,
        .anim_id = 5678,
        .launch_gfx_id = 91,
        .impact_gfx_id = 92,
        .start_delay = 30,
    };

    int projectile_idx = osrs_emit_projectile_player_to_npc(
        &overlay, &spec, 4);
    assert(projectile_idx == 0);
    assert(overlay.projectile_count == 1);
    assert(overlay.projectiles[0].source_kind == ENCOUNTER_PROJECTILE_TARGET_PLAYER);
    assert(overlay.projectiles[0].target_kind == ENCOUNTER_PROJECTILE_TARGET_NPC_SLOT);
    assert(overlay.projectiles[0].target_npc_slot == 4);
    assert(overlay.projectiles[0].anim_id == 5678);
    assert(overlay.projectiles[0].launch_gfx_id == 91);
    assert(overlay.projectiles[0].start_delay == 30);
}

static void test_shared_projectile_profile_emits_cache_visuals(void) {
    EncounterOverlay overlay;
    memset(&overlay, 0, sizeof(overlay));

    OsrsCombatProjectileProfile profile = {
        .launch_spotanim_id = 45,
        .travel_spotanim_id = GFX_DRAGON_DART,
        .impact_spotanim_id = 46,
        .projectile_model_id = 1234,
        .projectile_anim_id = 5678,
        .projectile_start_height = 150,
        .projectile_end_height = 90,
        .projectile_angle = 12,
    };

    int projectile_idx = osrs_emit_combat_projectile_profile_player_to_npc(
        &overlay,
        &profile,
        &(OsrsCombatProjectileEmitSpec){
            .src_x = 2,
            .src_y = 3,
            .dst_x = 10,
            .dst_y = 11,
            .src_size = 1,
            .dst_size = 5,
            .target_npc_slot = 4,
            .attack_style = ATTACK_STYLE_MAGIC,
            .damage = 0,
            .duration_ticks = 60,
            .start_delay = 15,
            .fallback_start_h = 64,
            .fallback_end_h = 320,
            .splash_gfx_id = 85,
        });

    assert(projectile_idx == 0);
    assert(overlay.projectile_count == 1);
    assert(overlay.projectiles[0].source_kind == ENCOUNTER_PROJECTILE_TARGET_PLAYER);
    assert(overlay.projectiles[0].target_kind == ENCOUNTER_PROJECTILE_TARGET_NPC_SLOT);
    assert(overlay.projectiles[0].target_npc_slot == 4);
    assert(overlay.projectiles[0].model_id == 1234);
    assert(overlay.projectiles[0].anim_id == 5678);
    assert(overlay.projectiles[0].launch_gfx_id == 45);
    assert(overlay.projectiles[0].impact_gfx_id == 85);
    assert(overlay.projectiles[0].start_h == 150);
    assert(overlay.projectiles[0].end_h == 90);
    assert(overlay.projectiles[0].curve == 12);
    assert(overlay.projectiles[0].arc_height == 0.0f);
    assert(overlay.projectiles[0].start_delay == 15);
}

int main(void) {
    test_shared_player_step_attack_click_chases_to_range();
    test_shared_player_step_ground_move_interrupts_interaction();
    test_shared_player_step_targeted_move_can_preserve_safe_spot();
    test_shared_render_entity_suppresses_persistent_pose_anims();
    test_shared_player_render_entity_resets_stale_fields();
    test_shared_render_entity_from_npc_spec_sets_identity_and_hits();
    test_shared_death_linger_starts_once_and_ticks_to_finish();
    test_shared_projectile_builders_require_anchors();
    test_shared_projectile_profile_emits_cache_visuals();
    printf("encounter sdk contracts passed\n");
    return 0;
}
