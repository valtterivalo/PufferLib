#line 76 "encounter_colosseum.h"
/**
 * @file encounter_colosseum_render_snapshot.inc
 * @brief Config kwargs + snapshot/restore + the EncounterDef vtable.
 */

static void col_put_int_ctx(
    EncounterState* state,
    EncounterContext* context,
    const char* key,
    int value
) {
    ColosseumState* s = (ColosseumState*)state;
    ColosseumContext* ctx = context ? (ColosseumContext*)context : col_legacy_context();
    /* wave is 1-indexed externally, 0-indexed internally */
    if (strcmp(key, "start_wave") == 0) {
        int internal = value - 1;
        if (internal < 0) internal = 0;
        if (internal >= COLO_NUM_WAVES) internal = COLO_NUM_WAVES - 1;
        s->start_wave = internal;
        ctx->config.start_wave = internal;
    }
    else if (strcmp(key, "seed") == 0) s->rng_state = (uint32_t)value;
    else if (strcmp(key, "world_offset_x") == 0) ctx->world_offset_x = value;
    else if (strcmp(key, "world_offset_y") == 0) ctx->world_offset_y = value;
    else if (strcmp(key, "player_dest_x") == 0) s->player_dest_x = value;
    else if (strcmp(key, "player_dest_y") == 0) s->player_dest_y = value;
    else if (strcmp(key, "human_command_mode") == 0)
        ctx->human_command_mode = encounter_require_binary_config("colosseum", key, value);
    else if (strcmp(key, "curriculum_agent") == 0)
        ctx->config.curriculum_agent =
            encounter_require_binary_config("colosseum", key, value);
    else if (strcmp(key, "step_out_forecast_obs_enabled") == 0)
        ctx->config.step_out_forecast_obs_enabled =
            encounter_require_binary_config("colosseum", key, value);
    else if (strcmp(key, "mask_inventory_heads") == 0)
        ctx->config.mask_inventory_heads =
            encounter_require_binary_config("colosseum", key, value);
    else if (strcmp(key, "action_debug_log") == 0)
        ctx->config.action_debug_log =
            encounter_require_binary_config("colosseum", key, value);
    else if (strcmp(key, "forecast_horizon") == 0) {
        if (value < 1 || value > COLO_STEP_OUT_FORECAST_HORIZON) {
            fprintf(stderr, "colosseum: forecast_horizon must be 1..%d, got %d\n",
                COLO_STEP_OUT_FORECAST_HORIZON, value);
            abort();
        }
        ctx->config.forecast_horizon = value;
    }
    else if (strcmp(key, "forecast_run_tile_mode") == 0) {
        if (value < 0 || value > COLO_FORECAST_RUN_TILE_STATIC_THREAT) {
            fprintf(stderr, "colosseum: forecast_run_tile_mode must be 0..%d, got %d\n",
                COLO_FORECAST_RUN_TILE_STATIC_THREAT, value);
            abort();
        }
        ctx->config.forecast_run_tile_mode = value;
    }
    else if (strcmp(key, "loadout_profile_mode") == 0) {
        if (value < 0 || value > COLO_LOADOUT_PROFILE_MODE_MIXED) {
            fprintf(stderr, "colosseum: loadout_profile_mode must be 0..2, got %d\n", value);
            abort();
        }
        ctx->config.loadout_profile_mode = value;
    }
    else encounter_abort_unknown_config("colosseum", "int", key);
}

static void col_put_float_ctx(
    EncounterState* state,
    EncounterContext* context,
    const char* key,
    float value
) {
    (void)state;
    ColosseumContext* ctx = context ? (ColosseumContext*)context : col_legacy_context();
    if (strcmp(key, "damage_reward_coeff") == 0) ctx->config.damage_reward_coeff = value;
    else if (strcmp(key, "boss_damage_reward_coeff") == 0)
        ctx->config.boss_damage_reward_coeff = value;
    else if (strcmp(key, "wave_clear_bonus") == 0) ctx->config.wave_clear_bonus = value;
    else if (strcmp(key, "boss_phase_bonus") == 0) ctx->config.boss_phase_bonus = value;
    else if (strcmp(key, "win_bonus") == 0) ctx->config.win_bonus = value;
    else if (strcmp(key, "prayer_correct_reward") == 0)
        ctx->config.prayer_correct_reward = value;
    else if (strcmp(key, "offpray_damage_penalty_coeff") == 0)
        ctx->config.offpray_damage_penalty_coeff = value;
    else if (strcmp(key, "avoided_damage_coeff") == 0)
        ctx->config.avoided_damage_coeff = value;
    else if (strcmp(key, "death_penalty_coeff") == 0) ctx->config.death_penalty_coeff = value;
    else if (strcmp(key, "timeout_penalty") == 0) ctx->config.timeout_penalty = value;
    else if (strcmp(key, "beginner_loadout_fraction") == 0) {
        if (!(value >= 0.0f && value <= 1.0f)) {
            fprintf(stderr, "colosseum: beginner_loadout_fraction must be in [0,1], got %.6f\n",
                    value);
            abort();
        }
        ctx->config.beginner_loadout_fraction = value;
    }
    else encounter_abort_unknown_config("colosseum", "float", key);
}

static void col_put_ptr_ctx(
    EncounterState* state,
    EncounterContext* context,
    const char* key,
    void* value
) {
    (void)state;
    ColosseumContext* ctx = context ? (ColosseumContext*)context : col_legacy_context();
    if (strcmp(key, "collision_map") == 0) ctx->collision_map = (const CollisionMap*)value;
    else encounter_abort_unknown_config("colosseum", "ptr", key);
}

static int col_get_tick_ctx(EncounterState* state, EncounterContext* context) {
    (void)context;
    return ((ColosseumState*)state)->tick;
}

static int col_get_winner_ctx(EncounterState* state, EncounterContext* context) {
    (void)context;
    return ((ColosseumState*)state)->winner;
}

static void* col_get_log_ctx(EncounterState* state, EncounterContext* context) {
    ColosseumState* s = (ColosseumState*)state;
    (void)context;
    s->log.episode_return = s->episode_return;
    s->log.episode_length = s->tick;
    s->log.wave_reached = s->wave;
    s->log.win = (s->episode_over && s->winner == COLO_OUTCOME_PLAYER_WON) ? 1 : 0;
    s->log.died = (s->episode_over && s->winner == COLO_OUTCOME_PLAYER_DIED) ? 1 : 0;
    s->log.boss_phase_reached = s->sol.phase;
    s->log.active_tier_sum = col_modifier_active_tier_sum(s);
    return &s->log;
}

static const char* col_attack_style_debug_name(AttackStyle style) {
    switch (style) {
        case ATTACK_STYLE_NONE: return "none";
        case ATTACK_STYLE_MELEE: return "melee";
        case ATTACK_STYLE_RANGED: return "ranged";
        case ATTACK_STYLE_MAGIC: return "magic";
    }
    abort();
}

static const OsrsCombatVisualRow* col_require_npc_combat_visual_projectile_row(
    ColoNpcType type,
    AttackStyle style
) {
    assert(type >= 0 && type < COLO_NUM_NPC_TYPES);
    int npc_def_id = COLO_NPC_DEF_IDS[type];
    const OsrsCombatVisualRow* row = osrs_combat_visual_find_npc_id(
        (uint16_t)npc_def_id, style);
    if (!row || (row->projectile.travel_spotanim_id <= 0 &&
            row->projectile.projectile_model_id <= 0)) {
        fprintf(stderr,
            "colosseum: missing real combat visual projectile for npc def id %d style %s\n",
            npc_def_id, col_attack_style_debug_name(style));
        abort();
    }
    return row;
}

enum {
    COLO_JAVELIN_SKYFALL_LAUNCH_TRAVEL_GFX_ID = 2674,
    COLO_JAVELIN_SKYFALL_DROP_TRAVEL_GFX_ID = 2675,
    COLO_JAVELIN_SKYFALL_IMPACT_GFX_ID = 2676,
    COLO_JAVELIN_SKYFALL_SKY_HEIGHT = 1536,
    COLO_JAVELIN_SKYFALL_GROUND_HEIGHT = 16,
    COLO_JAVELIN_SKYFALL_LAUNCH_CURVE = -48,
    COLO_JAVELIN_SKYFALL_DROP_CURVE = 48,
    COLO_JAVELIN_SKYFALL_DROP_DURATION_TICKS = 18,
    COLO_JAVELIN_PROJECTILE_RELEASE_DELAY_TICKS = 1,
};

static float col_javelin_skyfall_shadow_scale(const ColoJavelinState* jv) {
    if (!jv->skyfall_pending || jv->skyfall_timer <= 0 ||
            jv->skyfall_timer > COLO_JAVELIN_SKYFALL_DELAY) {
        fprintf(stderr, "colosseum: invalid skyfall shadow timer %d pending %d\n",
            jv->skyfall_timer, jv->skyfall_pending);
        abort();
    }
    float progress = (float)(COLO_JAVELIN_SKYFALL_DELAY - jv->skyfall_timer) /
        (float)(COLO_JAVELIN_SKYFALL_DELAY - 1);
    return 0.35f + 0.65f * progress;
}

static void col_emit_javelin_skyfall_shadow(EncounterOverlay* ov, const ColoJavelinState* jv) {
    encounter_emit_tile_shadow(
        ov,
        jv->skyfall_tile_x,
        jv->skyfall_tile_y,
        col_javelin_skyfall_shadow_scale(jv));
}

static EncounterProjectileTiming col_npc_projectile_timing(
    ColoNpcType type,
    AttackStyle style,
    int distance
) {
    EncounterProjectileDelayOptions options = {0};
    if (type == COLO_JAVELIN_COLOSSUS && style == ATTACK_STYLE_RANGED)
        options.start_delay = COLO_JAVELIN_PROJECTILE_RELEASE_DELAY_TICKS;
    return encounter_projectile_timing(
        distance, 0, col_projectile_delay_kind_for_style(style), options);
}

static uint32_t col_manticore_orb_model_id(AttackStyle style) {
    switch (style) {
        case ATTACK_STYLE_RANGED: return 51221u;
        case ATTACK_STYLE_MAGIC: return 51215u;
        case ATTACK_STYLE_MELEE: return 51213u;
        case ATTACK_STYLE_NONE: break;
    }
    fprintf(stderr, "colosseum: invalid manticore orb style %d for telegraph model\n",
        (int)style);
    abort();
}

static int col_manticore_orb_anim_id(AttackStyle style) {
    switch (style) {
        case ATTACK_STYLE_RANGED: return 10327;
        case ATTACK_STYLE_MAGIC: return 10329;
        case ATTACK_STYLE_MELEE: return 10328;
        case ATTACK_STYLE_NONE: break;
    }
    fprintf(stderr, "colosseum: invalid manticore orb style %d for telegraph anim\n",
        (int)style);
    abort();
}

static void col_emit_javelin_skyfall_launch_projectile(
    ColosseumState* s,
    EncounterOverlay* ov,
    int npc_slot
) {
    ColoNPC* npc = &s->npcs[npc_slot];
    ColoJavelinState* jv = colo_npc_javelin(npc);
    if (!jv->skyfall_pending) {
        fprintf(stderr, "colosseum: skyfall launch render event without pending skyfall\n");
        abort();
    }

    int npc_size = col_npc_effective_size(npc);
    encounter_require_projectile_slots(ov, 1);
    OsrsProjectileEventSpec projectile_spec = {
        .src_x = npc->x,
        .src_y = npc->y,
        .dst_x = jv->skyfall_tile_x,
        .dst_y = jv->skyfall_tile_y,
        .style = encounter_attack_style_to_proj_style(ATTACK_STYLE_RANGED),
        .damage = 0,
        .duration_ticks = (COLO_JAVELIN_SKYFALL_DELAY -
            COLO_JAVELIN_PROJECTILE_RELEASE_DELAY_TICKS) * 30,
        .start_h = (int)(npc_size * 0.75f * 128),
        .end_h = COLO_JAVELIN_SKYFALL_SKY_HEIGHT,
        .curve = COLO_JAVELIN_SKYFALL_LAUNCH_CURVE,
        .arc_height = 0.0f,
        .src_size = npc_size,
        .dst_size = 1,
        .model_id = 0u,
        .anim_id = -1,
        .travel_gfx_id = COLO_JAVELIN_SKYFALL_LAUNCH_TRAVEL_GFX_ID,
        .impact_gfx_id = 0,
        .start_delay = COLO_JAVELIN_PROJECTILE_RELEASE_DELAY_TICKS * 30,
    };
    int projectile_idx = osrs_emit_projectile_with_spec(
        ov,
        &projectile_spec,
        0);
    encounter_set_projectile_source_npc_slot(ov, projectile_idx, npc_slot);
}

static void col_emit_manticore_orb_telegraphs(
    EncounterOverlay* ov,
    const ColoNPC* npc,
    int npc_slot
) {
    const ColoManticoreState* mc = &npc->type_state.manticore;
    if (mc->cycle_step < 0) return;
    if (mc->cycle_step >= 3) {
        fprintf(stderr, "colosseum: invalid manticore cycle_step %d\n", mc->cycle_step);
        abort();
    }
    int npc_size = col_npc_effective_size(npc);
    for (int orb = mc->cycle_step; orb < 3; orb++) {
        AttackStyle style = mc->orb_style[orb];
        int stack_index = orb - mc->cycle_step;
        encounter_emit_floating_model(
            ov,
            ENCOUNTER_PROJECTILE_TARGET_NPC_SLOT,
            npc_slot,
            npc->x,
            npc->y,
            col_manticore_orb_model_id(style),
            col_manticore_orb_anim_id(style),
            2.35f + (float)stack_index * 0.62f,
            0.0f,
            0.72f + 0.05f * (float)(npc_size - 1));
    }
}

static void col_emit_javelin_skyfall_landing_projectile(
    EncounterOverlay* ov,
    const ColoJavelinSkyfallLandingRenderEvent* event
) {
    if (!event || !event->active)
        return;
    encounter_require_projectile_slots(ov, 1);
    OsrsProjectileEventSpec projectile_spec = {
        .src_x = event->tile_x,
        .src_y = event->tile_y,
        .dst_x = event->tile_x,
        .dst_y = event->tile_y,
        .style = encounter_attack_style_to_proj_style(ATTACK_STYLE_RANGED),
        .damage = event->damage,
        .duration_ticks = COLO_JAVELIN_SKYFALL_DROP_DURATION_TICKS,
        .start_h = COLO_JAVELIN_SKYFALL_SKY_HEIGHT,
        .end_h = COLO_JAVELIN_SKYFALL_GROUND_HEIGHT,
        .curve = COLO_JAVELIN_SKYFALL_DROP_CURVE,
        .arc_height = 0.0f,
        .src_size = 1,
        .dst_size = 1,
        .model_id = 0u,
        .anim_id = -1,
        .travel_gfx_id = COLO_JAVELIN_SKYFALL_DROP_TRAVEL_GFX_ID,
        .impact_gfx_id = COLO_JAVELIN_SKYFALL_IMPACT_GFX_ID,
        .start_delay = 0,
    };
    (void)osrs_emit_projectile_with_spec(
        ov,
        &projectile_spec,
        0);
}

static void col_emit_npc_attack_projectile(
    ColosseumState* s,
    ColosseumContext* ctx,
    EncounterOverlay* ov,
    int npc_slot
) {
    ColoNPC* npc = &s->npcs[npc_slot];
    AttackStyle actual_style = npc->attack_style_this_tick;
    if (actual_style == ATTACK_STYLE_NONE)
        return;
    if (actual_style == ATTACK_STYLE_MELEE && npc->type != COLO_MANTICORE)
        return;
    if (col_type_is_hazard_entity(npc->type))
        return;
    if (ctx && npc->type == COLO_JAVELIN_COLOSSUS &&
            ctx->npc_render_attack_anim[npc_slot] ==
                COLO_NPC_RENDER_ATTACK_JAVELIN_SKYFALL) {
        col_emit_javelin_skyfall_launch_projectile(s, ov, npc_slot);
        return;
    }

    int npc_size = col_npc_effective_size(npc);
    int start_h = (int)(npc_size * 0.75f * 128);
    int end_h = 64;
    int target_tile_x = s->player.x;
    int target_tile_y = s->player.y;
    int target_x = s->player.x;
    int target_y = s->player.y;
    int target_size = 1;
    int target_npc_slot = -1;

    if (npc->attack_visual_target >= 0 && npc->attack_visual_target < COLO_MAX_NPCS) {
        ColoNPC* target = &s->npcs[npc->attack_visual_target];
        if (col_npc_is_live_target(target)) {
            target_tile_x = target->x;
            target_tile_y = target->y;
            target_size = col_npc_effective_size(target);
            target_x = target->x + target_size / 2;
            target_y = target->y + target_size / 2;
            end_h = (int)(target_size * 0.5f * 128);
            target_npc_slot = npc->attack_visual_target;
        }
    }

    int dist = encounter_projectile_distance(
        npc->x, npc->y, npc_size, target_tile_x, target_tile_y, target_size,
        ENCOUNTER_PROJECTILE_DISTANCE_CLOSEST_TILE);
    EncounterProjectileTiming timing =
        col_npc_projectile_timing(npc->type, actual_style, dist);
    int duration = timing.visual_duration_ticks * 30;
    int start_delay = timing.visual_start_delay_ticks * 30;

    const OsrsCombatVisualRow* npc_visual =
        col_require_npc_combat_visual_projectile_row(npc->type, actual_style);
    const OsrsCombatProjectileProfile* projectile = &npc_visual->projectile;
    uint32_t model_id = projectile->projectile_model_id > 0
        ? (uint32_t)projectile->projectile_model_id
        : 0u;
    int anim_id = projectile->projectile_model_id > 0
        ? projectile->projectile_anim_id
        : OSRS_COMBAT_PROJECTILE_MISSING;
    int travel_gfx_id = osrs_combat_projectile_value_or(
        projectile->travel_spotanim_id, 0);
    int launch_gfx_id = osrs_combat_projectile_value_or(
        projectile->launch_spotanim_id, 0);
    int impact_gfx_id = osrs_combat_projectile_value_or(
        projectile->impact_spotanim_id, 0);
    int curve = osrs_combat_projectile_value_or(projectile->projectile_angle, 16);
    float arc = osrs_combat_projectile_profile_arc_height(projectile, actual_style);
    /* Colosseum-only: magic casters (serpent shaman Water Surge, seer, shockwave)
       must lob UP. The shared arc-height helper returns 0 for ATTACK_STYLE_MAGIC,
       which drops them into the quadratic path where the positive projectile_angle
       (curve=16) gives a negative height_vel and the arc dips into the ground.
       A positive arc_height routes them into the always-upward sinusoidal path,
       matching inferno's Jad/Zuk magic projectiles. */
    if (actual_style == ATTACK_STYLE_MAGIC && arc <= 0.0f)
        arc = 1.0f;
    encounter_require_projectile_slots(ov, 1);
    OsrsProjectileEventSpec spec = {
        .src_x = npc->x,
        .src_y = npc->y,
        .dst_x = target_x,
        .dst_y = target_y,
        .style = encounter_attack_style_to_proj_style(actual_style),
        .damage = (int)s->tick_scratch.damage_received,
        .duration_ticks = duration,
        .start_h = start_h,
        .end_h = end_h,
        .curve = curve,
        .arc_height = arc,
        .src_size = npc_size,
        .dst_size = target_size,
        .model_id = model_id,
        .anim_id = anim_id,
        .travel_gfx_id = travel_gfx_id,
        .launch_gfx_id = launch_gfx_id,
        .impact_gfx_id = impact_gfx_id,
        .start_delay = start_delay,
    };

    if (target_npc_slot >= 0) {
        (void)osrs_emit_projectile_npc_to_npc(
            ov, &spec, npc_slot, target_npc_slot);
    } else {
        (void)osrs_emit_projectile_npc_to_player(ov, &spec, npc_slot);
    }
}

/** Emit Venator bounce hops after the primary player-to-NPC projectile. */
static void col_emit_player_venator_bounce_projectiles(
    ColosseumState* s,
    ColosseumContext* ctx,
    EncounterOverlay* ov,
    const OsrsCombatProjectileProfile* profile,
    int start_delay,
    int duration
) {
    if (!ctx || ctx->player_venator_chain_count <= 1) return;
    if (ctx->player_venator_chain_count > OSRS_VENATOR_MAX_CHAIN_HITS) abort();
    if (!profile || profile->projectile_model_id <= 0) {
        fprintf(stderr, "colosseum: missing Venator bounce projectile model\n");
        abort();
    }

    int bounce_count = ctx->player_venator_chain_count - 1;
    encounter_require_projectile_slots(ov, bounce_count);
    for (int hop = 1; hop < ctx->player_venator_chain_count; hop++) {
        int source_slot = ctx->player_venator_chain_slots[hop - 1];
        int target_slot = ctx->player_venator_chain_slots[hop];
        if (source_slot < 0 || source_slot >= COLO_MAX_NPCS ||
                target_slot < 0 || target_slot >= COLO_MAX_NPCS) {
            fprintf(stderr, "colosseum: invalid Venator chain slot\n");
            abort();
        }
        ColoNPC* source = &s->npcs[source_slot];
        ColoNPC* target = &s->npcs[target_slot];
        if (!source->active || !target->active) {
            fprintf(stderr, "colosseum: Venator chain target inactive\n");
            abort();
        }
        int source_size = col_npc_effective_size(source);
        int target_size = col_npc_effective_size(target);
        int source_h = (int)(source_size * 0.5f * 128);
        int target_h = (int)(target_size * 0.5f * 128);
        OsrsProjectileEventSpec spec = {
            .src_x = source->x,
            .src_y = source->y,
            .dst_x = target->x,
            .dst_y = target->y,
            .style = encounter_attack_style_to_proj_style(ATTACK_STYLE_RANGED),
            .damage = 0,
            .duration_ticks = duration,
            .start_h = source_h,
            .end_h = target_h,
            .curve = osrs_combat_projectile_value_or(profile->projectile_angle, 16),
            .arc_height = osrs_combat_projectile_profile_arc_height(
                profile, ATTACK_STYLE_RANGED),
            .src_size = source_size,
            .dst_size = target_size,
            .model_id = (uint32_t)profile->projectile_model_id,
            .anim_id = profile->projectile_anim_id,
            .travel_gfx_id = 0,
            .launch_gfx_id = 0,
            .impact_gfx_id = osrs_combat_projectile_value_or(
                profile->impact_spotanim_id, 0),
            .start_delay = start_delay + (hop - 1) * duration,
        };
        (void)osrs_emit_projectile_npc_to_npc(
            ov, &spec, source_slot, target_slot);
    }
}

static void col_emit_player_attack_projectiles(
    ColosseumState* s,
    ColosseumContext* ctx,
    EncounterOverlay* ov
) {
    if (!s->tick_scratch.player_attacked ||
            s->player_attack_style_id == ATTACK_STYLE_MELEE) {
        return;
    }

    int target_idx = s->player_attack_npc_idx;
    if (target_idx < 0 || target_idx >= COLO_MAX_NPCS)
        return;
    ColoNPC* target = &s->npcs[target_idx];
    if (!target->active)
        return;

    uint8_t weapon = s->player.equipped[GEAR_SLOT_WEAPON];
    int target_size = col_npc_effective_size(target);
    int p_start_h = 64;
    int p_end_h = (int)(target_size * 0.5f * 128);
    EncounterProjectileTiming timing = s->player_attack_timing;
    if (timing.visual_duration_ticks <= 0) {
        int dist = encounter_projectile_distance(
            s->player.x, s->player.y, 1, target->x, target->y, target_size,
            ENCOUNTER_PROJECTILE_DISTANCE_CLOSEST_TILE);
        timing = col_player_projectile_timing(
            s->player_attack_style_id, weapon, dist);
    }
    int p_duration = timing.visual_duration_ticks * 30;
    int p_start_delay = timing.visual_start_delay_ticks * 30;

    if (s->player_attack_style_id == ATTACK_STYLE_MAGIC) {
        const OsrsCombatProjectileProfile* profile =
            osrs_combat_visual_magic_projectile_profile(weapon);
        if (!profile || profile->projectile_model_id <= 0)
            return;
        OsrsCombatProjectileEmitSpec emit_spec = {
            .src_x = s->player.x,
            .src_y = s->player.y,
            .dst_x = target->x,
            .dst_y = target->y,
            .src_size = 1,
            .dst_size = target_size,
            .target_npc_slot = target_idx,
            .attack_style = s->player_attack_style_id,
            .damage = s->player_attack_dmg,
            .duration_ticks = p_duration,
            .start_delay = p_start_delay,
            .fallback_start_h = p_start_h,
            .fallback_end_h = p_end_h,
            .curve = 16,
        };
        (void)osrs_emit_combat_projectile_profile_player_to_npc(
            ov,
            profile,
            &emit_spec);
        return;
    }

    const OsrsCombatProjectileProfile* base_profile =
        osrs_combat_visual_ranged_projectile_profile(
            weapon, OSRS_COMBAT_PROJECTILE_NONE);
    const OsrsCombatVisualRow* effect = NULL;
    if (s->player.used_special_this_tick && weapon < NUM_ITEMS) {
        effect = osrs_combat_visual_find_special_projectile_item_id(
            ITEM_DATABASE[weapon].item_id, ATTACK_STYLE_RANGED);
    }
    OsrsCombatProjectileSequencePart parts[OSRS_COMBAT_PROJECTILE_SEQUENCE_MAX];
    int part_count = osrs_combat_visual_build_projectile_sequence(
        base_profile, effect, parts, OSRS_COMBAT_PROJECTILE_SEQUENCE_MAX);
    if (part_count < 0)
        abort();
    encounter_require_projectile_slots(ov, part_count);
    for (int part_i = 0; part_i < part_count; part_i++) {
        const OsrsCombatProjectileProfile* profile = &parts[part_i].projectile;
        if (profile->projectile_model_id <= 0)
            continue;
        int visual_damage = part_i == part_count - 1
            ? s->player_attack_dmg
            : 0;
        OsrsCombatProjectileEmitSpec emit_spec = {
            .src_x = s->player.x,
            .src_y = s->player.y,
            .dst_x = target->x,
            .dst_y = target->y,
            .src_size = 1,
            .dst_size = target_size,
            .target_npc_slot = target_idx,
            .attack_style = s->player_attack_style_id,
            .damage = visual_damage,
            .duration_ticks = p_duration,
            .start_delay = p_start_delay,
            .fallback_start_h = p_start_h,
            .fallback_end_h = p_end_h,
            .curve = 16,
        };
        (void)osrs_emit_combat_projectile_profile_player_to_npc(
            ov,
            profile,
            &emit_spec);
    }
    col_emit_player_venator_bounce_projectiles(
        s, ctx, ov, base_profile, p_start_delay + p_duration, 30);
}

/** Render-only: the allied thrall's magic bolt, lobbed from its render tile to
    the NPC it hit this tick. Mirrors the NPC magic projectile path (thrall magic
    spotanims THRALL_MAGIC_TRAVEL 1907 / THRALL_MAGIC_IMPACT 1908, arc_height
    forced positive so magic lobs up) but keeps the source fixed at the thrall
    tile and tracks the target so it lands. The damage is cosmetic; the sim
    already queued it. */
static void col_emit_thrall_projectile(ColosseumState* s, EncounterOverlay* ov) {
    int slot = s->thrall_target_slot;
    if (!col_npc_is_thrall_target(s, slot))
        return;
    ColoNPC* target = &s->npcs[slot];
    int target_size = col_npc_effective_size(target);

    int src_x, src_y;
    col_thrall_render_tile(s, &src_x, &src_y);

    int dist = encounter_projectile_distance(
        src_x, src_y, 1, target->x, target->y, target_size,
        ENCOUNTER_PROJECTILE_DISTANCE_CLOSEST_TILE);
    EncounterProjectileTiming timing =
        col_player_projectile_timing(ATTACK_STYLE_MAGIC, 0, dist);

    encounter_require_projectile_slots(ov, 1);
    OsrsProjectileEventSpec spec = {
        .src_x = src_x,
        .src_y = src_y,
        .dst_x = target->x + target_size / 2,
        .dst_y = target->y + target_size / 2,
        .style = encounter_attack_style_to_proj_style(ATTACK_STYLE_MAGIC),
        .damage = 0,
        .duration_ticks = timing.visual_duration_ticks * 30,
        .start_h = 64,
        .end_h = (int)(target_size * 0.5f * 128),
        .curve = 16,
        .arc_height = 1.0f,
        .src_size = 1,
        .dst_size = target_size,
        .model_id = 0,
        .anim_id = -1,
        .travel_gfx_id = 1907,
        .launch_gfx_id = 0,
        .impact_gfx_id = 1908,
        .start_delay = timing.visual_start_delay_ticks * 30,
    };
    int idx = osrs_emit_projectile_with_spec(ov, &spec, 1);
    encounter_set_projectile_target_npc_slot(ov, idx, slot);
}

static void col_render_post_tick_ctx(
    EncounterState* state,
    EncounterContext* context,
    EncounterOverlay* ov
) {
    ColosseumState* s = (ColosseumState*)state;
    ColosseumContext* ctx = context ? (ColosseumContext*)context : col_legacy_context();
    ov->tile_shadow_count = 0;
    ov->floating_model_count = 0;
    ov->active_modifier_count = 0;
    ov->projectile_count = 0;
    encounter_write_terminal_status_text(
        s->episode_over,
        s->winner,
        COLO_OUTCOME_PLAYER_WON,
        "Colosseum cleared",
        colo_npc_type_name(s->last_hit_by_type),
        ov->status_text,
        sizeof(ov->status_text));
    ov->status_text_active = ov->status_text[0] != '\0';

    for (int mod = 0; mod < COLO_NUM_REAL_MODIFIERS; mod++) {
        int tier = col_mod_tier(s, (ColoModifier)mod);
        if (tier <= 0) continue;
        if (tier > COLO_MODIFIER_MAX_TIER[mod] || tier > 3) {
            fprintf(stderr, "colosseum: active modifier %d has invalid tier %d\n",
                mod, tier);
            abort();
        }
        encounter_emit_active_modifier(ov, mod, tier);
    }

    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        ColoNPC* npc = &s->npcs[i];
        if (!npc->active || npc->type != COLO_JAVELIN_COLOSSUS)
            continue;
        ColoJavelinState* jv = colo_npc_javelin(npc);
        if (jv->skyfall_pending)
            col_emit_javelin_skyfall_shadow(ov, jv);
    }

    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        ColoNPC* npc = &s->npcs[i];
        if (!npc->active || npc->type != COLO_MANTICORE)
            continue;
        col_emit_manticore_orb_telegraphs(ov, npc, i);
    }

    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        ColoNPC* npc = &s->npcs[i];
        if (!npc->active || !npc->attacked_this_tick)
            continue;
        col_emit_npc_attack_projectile(s, ctx, ov, i);
    }
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        col_emit_javelin_skyfall_landing_projectile(
            ov, &ctx->javelin_skyfall_landing[i]);
    }
    col_emit_player_attack_projectiles(s, ctx, ov);
    if (ctx->thrall_attacked_this_tick)
        col_emit_thrall_projectile(s, ov);
}

/* ----- human mode --------------------------------------------------------- */
/* Maps semantic HumanInput intents onto the 9 Colosseum action heads. Mirrors
   inferno's translate/step pair: MOVE (25-dir walk) + TARGET (obs slot of the
   clicked NPC) drive attacking, plus prayer/gear/eat/potion/spec. The two
   Colosseum-only heads have no inferno analog: MODIFIER_SELECT picks a draft
   option and GRAPPLE_PARRY clicks a Sol grapple body slot. */

static void* col_get_player_for_input(void* state, int idx) {
    ColosseumState* s = (ColosseumState*)state;
    return (idx == 0) ? (void*)&s->player : NULL;
}

/** obs slot the agent sees for a raw NPC array index, or -1. Inverse of
    current_obs_slots, refreshed by the caller. */
static int col_find_target_obs_slot(const ColosseumState* s, int npc_slot) {
    return encounter_find_observed_target_slot(
        s->current_obs_slots, COLO_OBS_NPCS, npc_slot);
}

static int col_is_human_targetable_npc_slot_ctx(
    EncounterState* state,
    EncounterContext* context,
    int npc_slot
) {
    ColosseumState* s = (ColosseumState*)state;
    ColosseumContext* ctx = context ? (ColosseumContext*)context : col_legacy_context();
    col_refresh_current_obs_slots_ctx(s, ctx);
    int obs_slot = col_find_target_obs_slot(s, npc_slot);
    if (obs_slot < 0) return 0;
    return col_obs_slot_is_targetable(s, obs_slot);
}

static int col_queue_inventory_click_action(int* actions, int inventory_slot) {
    if (inventory_slot < 0 || inventory_slot >= COLO_INVENTORY_DISPLAY_SLOTS) return -1;
    for (int head = 0; head < COLO_INV_CLICK_HEADS; head++) {
        int action_head = COLO_HEAD_INV_CLICK_0 + head;
        if (actions[action_head] == 0) {
            actions[action_head] = inventory_slot + 1;
            return action_head;
        }
    }
    return -1;
}

static void col_rearrange_inventory_cells(
    ColosseumState* s,
    int source_slot,
    int target_slot
) {
    osrs_inventory_swap_cells(s->inventory_cells, source_slot, target_slot);
}

static void col_translate_human_input_ctx(
    HumanInput* hi,
    int* actions,
    EncounterState* state,
    EncounterContext* context
) {
    for (int h = 0; h < COLO_NUM_ACTION_HEADS; h++) actions[h] = 0;

    encounter_translate_movement(hi, actions, COLO_HEAD_PRIMARY, col_get_player_for_input, state);
    encounter_translate_prayer(hi, actions, COLO_HEAD_PRAYER);

    ColosseumState* s = (ColosseumState*)state;
    ColosseumContext* ctx = context ? (ColosseumContext*)context : col_legacy_context();
    col_refresh_current_obs_slots_ctx(s, ctx);

    /* map raw pending_target_idx to the observation slot the agent sees */
    if (hi->pending_target_idx >= 0) {
        int found_slot = col_find_target_obs_slot(s, hi->pending_target_idx);
        actions[COLO_HEAD_PRIMARY] =
            (found_slot >= 0 && col_obs_slot_is_targetable(s, found_slot))
                ? col_primary_attack_action_for_obs_slot(found_slot) : 0;
    } else {
        actions[COLO_HEAD_PRIMARY] = 0;
    }

    if (hi->pending_spec) actions[COLO_HEAD_SPEC] = 1;
    encounter_translate_offensive_prayer(hi, actions, COLO_HEAD_OFFENSIVE);

    /* Colosseum-only heads: draft pick (0=none, 1..3) + grapple body slot. */
    if (hi->pending_modifier_select > 0 &&
            hi->pending_modifier_select <= COLO_MODIFIER_DRAFT_OPTIONS)
        actions[COLO_HEAD_MODIFIER_SELECT] = hi->pending_modifier_select;
    if (hi->pending_grapple_slot > 0 && hi->pending_grapple_slot <= COLO_NUM_GRAPPLE_SLOTS)
        actions[COLO_HEAD_GRAPPLE_PARRY] = hi->pending_grapple_slot;
}

static void col_translate_human_commands_ctx(
    HumanInput* hi,
    int* actions,
    ColosseumState* s,
    ColosseumContext* ctx
) {
    for (int h = 0; h < COLO_NUM_ACTION_HEADS; h++) actions[h] = 0;
    col_refresh_current_obs_slots_ctx(s, ctx);

    for (int i = 0; i < hi->commands.count; i++) {
        const HumanCommand* cmd = &hi->commands.items[i];
        switch (cmd->kind) {
            case HUMAN_COMMAND_WALK:
                s->player_dest_x = cmd->world_x;
                s->player_dest_y = cmd->world_y;
                actions[COLO_HEAD_PRIMARY] = 0;
                break;
            case HUMAN_COMMAND_ATTACK_NPC: {
                int found_slot = col_find_target_obs_slot(s, cmd->npc_slot);
                actions[COLO_HEAD_PRIMARY] =
                    (found_slot >= 0 && col_obs_slot_is_targetable(s, found_slot))
                        ? col_primary_attack_action_for_obs_slot(found_slot) : 0;
                s->player_dest_x = -1;
                s->player_dest_y = -1;
                break;
            }
            case HUMAN_COMMAND_OVERHEAD_PRAYER:
                actions[COLO_HEAD_PRAYER] = cmd->overhead_prayer;
                break;
            case HUMAN_COMMAND_EAT:
                col_queue_inventory_click_action(actions, cmd->inventory_slot);
                break;
            case HUMAN_COMMAND_DRINK:
                col_queue_inventory_click_action(actions, cmd->inventory_slot);
                break;
            case HUMAN_COMMAND_SPEC_TOGGLE:
                actions[COLO_HEAD_SPEC] = 1;
                break;
            case HUMAN_COMMAND_EQUIP_INVENTORY_ITEM:
                col_queue_inventory_click_action(actions, cmd->inventory_slot);
                break;
            case HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK: {
                int action_head =
                    col_queue_inventory_click_action(actions, cmd->inventory_slot);
                if (ctx->config.action_debug_log) {
                    fprintf(stderr,
                        "colo_action tick=%d human_inventory_click slot=%d head=%d action=%d\n",
                        s->tick, cmd->inventory_slot, action_head,
                        action_head >= 0 ? actions[action_head] : 0);
                }
                break;
            }
            case HUMAN_COMMAND_ITEM_ON_ITEM:
                col_rearrange_inventory_cells(
                    s, cmd->inventory_slot, cmd->target_inventory_slot);
                break;
            case HUMAN_COMMAND_OFFENSIVE_PRAYER:
                actions[COLO_HEAD_OFFENSIVE] = cmd->offensive_prayer;
                break;
            case HUMAN_COMMAND_SPELL_TARGET:
            case HUMAN_COMMAND_FIGHT_STYLE:
            case HUMAN_COMMAND_SET_AUTOCAST:
            case HUMAN_COMMAND_ITEM_ON_WIDGET:
            case HUMAN_COMMAND_SPELL_ON_WIDGET:
            case HUMAN_COMMAND_NONE:
                break;
        }
    }

    /* gear/draft/grapple intents are staged on pending_* (not the command queue),
       so fold them in here exactly as the per-tick translate path does. */
    if (hi->pending_modifier_select > 0 &&
            hi->pending_modifier_select <= COLO_MODIFIER_DRAFT_OPTIONS)
        actions[COLO_HEAD_MODIFIER_SELECT] = hi->pending_modifier_select;
    if (hi->pending_grapple_slot > 0 && hi->pending_grapple_slot <= COLO_NUM_GRAPPLE_SLOTS)
        actions[COLO_HEAD_GRAPPLE_PARRY] = hi->pending_grapple_slot;
}

static void col_step_human_commands_ctx(
    EncounterState* state,
    EncounterContext* context,
    HumanInput* hi
) {
    ColosseumState* s = (ColosseumState*)state;
    ColosseumContext* ctx = context ? (ColosseumContext*)context : col_legacy_context();
    int actions[COLO_NUM_ACTION_HEADS];
    ctx->human_command_mode = 1;
    ctx->human_commands = hi->commands.items;
    ctx->human_command_count = hi->commands.count;
    col_translate_human_commands_ctx(hi, actions, s, ctx);
    col_step_ctx(state, (EncounterContext*)ctx, actions);
    ctx->human_commands = NULL;
    ctx->human_command_count = 0;
    human_input_clear_pending(hi);
}

/* snapshot/restore stores only gameplay state; live process data stays in context. */

#define COLO_SNAPSHOT_MAGIC 0xC010C001u
/* v2: P4 SolHereditState rewrite (A1-A15, B4, B7).
   v3: P5 draft rework (draft_gates_spawn replaces draft_timer), hazard-entity
   totems/bees (ColoTotem/ColoBeeSwarm relaid), molten_kind, venom fields.
   v5: E8 bee-contact poison state fields.
   v6: E13 stat drift and divine potion timers.
   v7: manticore fixed orb cycle split from active telegraph state.
   v8: combat-fidelity pass — magic weapon set (COLO_GEAR_MAGIC), Arceuus thrall
       fields, and Death Charge window/cooldown added to ColosseumState.
   v9: real 28-cell inventory, inventory-click action heads, and equipped-weapon
       special attack contract.
   v10: enriched inventory/equipped item-semantic obs (role/kind/magnitude/effect-class/weapon-speed)
       + player offensive/spec/profile scalars.
   v11: colosseum obs adds static pillar geometry and enriched per-NPC combat
       and source-aware prayer timing features.
   v12: colosseum log adds per-NPC total damage attribution.
   v13: Solarflare stores shared per-pillar ring cadence.
   v14: sim-fidelity pass adds kinded molten pools (molten_lifetime,
       molten_burn_timer) for the stronger permanent Reentry sand. */
#define COLO_SNAPSHOT_VERSION 14u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t state_size;
    uint32_t reserved;
    ColosseumState state;
} ColoSnapshot;

static void col_refresh_after_state_load(ColosseumState* s, ColosseumContext* ctx) {
    col_build_npc_stats();
    col_mark_live_loadout_dirty(s);
    col_ensure_live_loadout_stats(s);
    col_rebuild_player_collision_flags(s);
    col_refresh_current_obs_slots_ctx(s, ctx);
}

static size_t col_snapshot_size_ctx(EncounterState* state, EncounterContext* context) {
    (void)state;
    (void)context;
    return sizeof(ColoSnapshot);
}

static void col_snapshot_ctx(EncounterState* state, EncounterContext* context, void* out) {
    (void)context;
    ColoSnapshot* snap = (ColoSnapshot*)out;
    encounter_snapshot_write_frame(
        snap, sizeof(*snap), COLO_SNAPSHOT_MAGIC, COLO_SNAPSHOT_VERSION,
        sizeof(ColosseumState));
    encounter_snapshot_copy_state_to(
        snap, offsetof(ColoSnapshot, state), state, sizeof(ColosseumState));
}

static void col_restore_ctx(
    EncounterState* state,
    EncounterContext* context,
    const void* data,
    size_t n
) {
    ColosseumContext* ctx = context ? (ColosseumContext*)context : col_legacy_context();
    ColosseumState* dst = (ColosseumState*)state;
    encounter_snapshot_validate_frame(
        "col_restore", data, n, sizeof(ColoSnapshot),
        COLO_SNAPSHOT_MAGIC, COLO_SNAPSHOT_VERSION, sizeof(ColosseumState));
    encounter_snapshot_copy_state_from(
        dst, data, offsetof(ColoSnapshot, state), sizeof(ColosseumState));
    col_context_clear_render_events(ctx);
    col_refresh_after_state_load(dst, ctx);
}


static int col_lab_apply_script_line_alloc_json_void(
    void* state, const char* line, char** out_json
) {
    return col_lab_apply_script_line_alloc_json((ColosseumState*)state, line, out_json);
}

static int col_apply_lab_command_ctx(
    EncounterState* state,
    EncounterContext* context,
    const char* line
) {
    ColosseumContext* ctx = context ? (ColosseumContext*)context : col_legacy_context();
    col_context_clear_render_events(ctx);
    return encounter_apply_lab_command_dump_wrapper(
        state, line, COLO_LAB_LINE_DUMP, col_lab_apply_script_line_alloc_json_void);
}


static const EncounterDef ENCOUNTER_COLOSSEUM = {
    .name = "colosseum",
    .obs_size = COLO_NUM_OBS,
    .num_action_heads = COLO_NUM_ACTION_HEADS,
    .action_head_dims = COLO_ACTION_DIMS,
    .mask_size = COLO_ACTION_MASK_SIZE,

    .state_size = sizeof(ColosseumState),
    .context_size = sizeof(ColosseumContext),
    .init_context = col_init_context,
    .destroy_context = col_destroy_context,
    .init_state = col_init_state,

    .create = col_create,
    .destroy = col_destroy,
    .reset = col_reset_ctx,
    .step = col_step_ctx,
    .step_human_commands = col_step_human_commands_ctx,

    .snapshot_size = col_snapshot_size_ctx,
    .snapshot = col_snapshot_ctx,
    .restore = col_restore_ctx,

    .cell_key_size = NULL,
    .write_cell_key = NULL,
    .progress_score = NULL,

    .write_obs = col_write_obs_ctx,
    .write_mask = col_write_mask_ctx,
    .get_reward = col_get_reward_ctx,
    .is_terminal = col_is_terminal_ctx,

    .get_entity_count = col_get_entity_count_ctx,
    .get_entity = col_get_entity_ctx,
    .fill_render_entities = col_fill_render_entities_ctx,

    .put_int = col_put_int_ctx,
    .put_float = col_put_float_ctx,
    .put_ptr = col_put_ptr_ctx,

    .arena_base_x = COLO_ARENA_MIN_X,
    .arena_base_y = COLO_ARENA_MIN_Y,
    .arena_width = COLO_ARENA_WIDTH,
    .arena_height = COLO_ARENA_HEIGHT,

    .translate_human_input = col_translate_human_input_ctx,
    .is_human_targetable_npc_slot = col_is_human_targetable_npc_slot_ctx,
    .apply_lab_command = col_apply_lab_command_ctx,
    .head_move = COLO_HEAD_PRIMARY,
    .head_prayer = COLO_HEAD_PRAYER,
    .head_target = COLO_HEAD_PRIMARY,

    .render_post_tick = col_render_post_tick_ctx,
    .get_log = col_get_log_ctx,
    .get_tick = col_get_tick_ctx,
    .get_winner = col_get_winner_ctx,
};

__attribute__((constructor))
static void col_register(void) {
    encounter_register(&ENCOUNTER_COLOSSEUM);
}
