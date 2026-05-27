#line 1436 "encounter_inferno.h"
static InfSupplyDoses inf_full_starting_supplies(void) {
    return (InfSupplyDoses){
        .brew_doses = 24,
        .restore_doses = 40,
        .bastion_doses = 16,
        .stamina_doses = 4,
    };
}

static const InfSupplyProfileAnchor INF_SUPPLY_PROFILE_ANCHORS[] = {
    { 1,  { 1.0000f, 1.0000f, 1.0000f, 1.0000f } },
    { 20, { 1.0000f, 0.9500f, 1.0000f, 1.0000f } },
    { 40, { 0.9167f, 0.8750f, 1.0000f, 1.0000f } },
    { 61, { 0.8333f, 0.7500f, 1.0000f, 1.0000f } },
    { 64, { 0.5833f, 0.5000f, 0.7500f, 1.0000f } },
    { 68, { 0.5833f, 0.4250f, 0.6250f, 1.0000f } },
    { 69, { 0.5000f, 0.3000f, 0.3750f, 1.0000f } },
    { 70, { 0.4375f, 0.2250f, 0.3750f, 1.0000f } },
    { 71, { 0.3750f, 0.1500f, 0.3750f, 1.0000f } },
};

static float inf_lerp_float(float a, float b, float t) {
    return a + (b - a) * t;
}

static void inf_require_unit_interval_float_config(const char* key, float value) {
    if (value < 0.0f || value > 1.0f) {
        fprintf(stderr, "inferno %s must be in [0, 1], got %.6f\n", key, value);
        abort();
    }
}

static void inf_require_valid_supply_scale(float scale) {
    inf_require_unit_interval_float_config("late_start_supply_profile_scale", scale);
}

static void inf_require_valid_curriculum_supply_mode(const char* key, int mode) {
    if (mode < INF_CURRICULUM_SUPPLY_MODE_OFF ||
            mode > INF_CURRICULUM_SUPPLY_MODE_PRE_ZUK) {
        fprintf(stderr, "inferno %s must be in [0, 3], got %d\n", key, mode);
        abort();
    }
}

static void inf_require_valid_budget_loadout_fraction(float fraction) {
    inf_require_unit_interval_float_config("budget_loadout_fraction", fraction);
}

static void inf_require_valid_loadout_profile_mode(int mode) {
    if (mode < INF_LOADOUT_PROFILE_MODE_MAX_ONLY ||
            mode > INF_LOADOUT_PROFILE_MODE_MIXED_MAX_BUDGET) {
        fprintf(stderr, "inferno loadout_profile_mode must be in [0, 2], got %d\n",
            mode);
        abort();
    }
}

static InfLoadoutProfile inf_sample_loadout_profile(
    InfLoadoutProfileMode mode,
    float budget_fraction,
    uint32_t* rng_state
) {
    inf_require_valid_loadout_profile_mode(mode);
    inf_require_valid_budget_loadout_fraction(budget_fraction);

    if (mode == INF_LOADOUT_PROFILE_MODE_MAX_ONLY)
        return INF_LOADOUT_PROFILE_MAX;
    if (mode == INF_LOADOUT_PROFILE_MODE_BUDGET_ONLY)
        return INF_LOADOUT_PROFILE_BUDGET;
    return encounter_rand_float(rng_state) < budget_fraction
        ? INF_LOADOUT_PROFILE_BUDGET
        : INF_LOADOUT_PROFILE_MAX;
}

static void inf_require_nonnegative_float_config(const char* key, float value) {
    if (!(value >= 0.0f)) {
        fprintf(stderr, "inferno %s must be >= 0, got %.6f\n", key, value);
        abort();
    }
}

static void inf_require_valid_public_wave(int public_wave) {
    if (public_wave < 1 || public_wave > INF_MAX_PUBLIC_START_WAVE) {
        fprintf(stderr, "inferno start_wave must be in [1, %d], got %d\n",
            INF_MAX_PUBLIC_START_WAVE, public_wave);
        abort();
    }
}

static int inf_runtime_wave_from_start_wave(int internal_start_wave) {
    return internal_start_wave >= INF_WAVE_ZUK ? INF_WAVE_ZUK : internal_start_wave;
}

static int inf_initial_attack_timer_after_stun(int attack_speed, int stun_timer) {
    return attack_speed + stun_timer;
}

static InfSupplyFractions inf_supply_profile_fractions(int public_wave) {
    inf_require_valid_public_wave(public_wave);

    const int n = (int)(sizeof(INF_SUPPLY_PROFILE_ANCHORS) /
        sizeof(INF_SUPPLY_PROFILE_ANCHORS[0]));
    for (int i = 1; i < n; i++) {
        const InfSupplyProfileAnchor* lo = &INF_SUPPLY_PROFILE_ANCHORS[i - 1];
        const InfSupplyProfileAnchor* hi = &INF_SUPPLY_PROFILE_ANCHORS[i];
        if (public_wave <= hi->public_wave) {
            float t = (float)(public_wave - lo->public_wave) /
                (float)(hi->public_wave - lo->public_wave);
            return (InfSupplyFractions){
                .brew_fraction = inf_lerp_float(lo->fractions.brew_fraction,
                    hi->fractions.brew_fraction, t),
                .restore_fraction = inf_lerp_float(lo->fractions.restore_fraction,
                    hi->fractions.restore_fraction, t),
                .bastion_fraction = inf_lerp_float(lo->fractions.bastion_fraction,
                    hi->fractions.bastion_fraction, t),
                .stamina_fraction = inf_lerp_float(lo->fractions.stamina_fraction,
                    hi->fractions.stamina_fraction, t),
            };
        }
    }

    fprintf(stderr, "inferno supply profile has no anchor for wave %d\n", public_wave);
    abort();
}

static int inf_supply_profile_anchor_index(int public_wave) {
    int n = (int)(sizeof(INF_SUPPLY_PROFILE_ANCHORS) /
        sizeof(INF_SUPPLY_PROFILE_ANCHORS[0]));
    for (int i = 0; i < n; i++) {
        if (INF_SUPPLY_PROFILE_ANCHORS[i].public_wave == public_wave)
            return i;
    }
    return -1;
}

static int inf_profiled_supply_count(int full_doses, float profile_fraction, float scale) {
    assert(full_doses >= 0);

    float effective_fraction = 1.0f - scale * (1.0f - profile_fraction);
    int doses = (int)((float)full_doses * effective_fraction + 0.5f);
    if (doses < 0) doses = 0;
    if (doses > full_doses) doses = full_doses;
    return doses;
}

static int inf_clamp_supply_doses(int doses, int full_doses) {
    if (doses < 0) return 0;
    if (doses > full_doses) return full_doses;
    return doses;
}

static int inf_round_signed_float_to_int(float value) {
    return value >= 0.0f ? (int)(value + 0.5f) : (int)(value - 0.5f);
}

static int inf_curriculum_supply_mode_applies(int mode, int public_wave) {
    inf_require_valid_curriculum_supply_mode("curriculum supply mode", mode);
    switch (mode) {
        case INF_CURRICULUM_SUPPLY_MODE_OFF:
            return 0;
        case INF_CURRICULUM_SUPPLY_MODE_ALL:
            return 1;
        case INF_CURRICULUM_SUPPLY_MODE_ZUK:
            return public_wave >= INF_NUM_WAVES;
        case INF_CURRICULUM_SUPPLY_MODE_PRE_ZUK:
            return public_wave < INF_NUM_WAVES;
        default:
            abort();
    }
}

static void inf_apply_curriculum_supply_variation(
    InfernoState* s,
    const InfernoContext* ctx,
    InfSupplyDoses full
) {
    if (!ctx->config.curriculum_agent) return;

    int public_wave = s->start_wave + 1;
    int jitter_applies = inf_curriculum_supply_mode_applies(
        ctx->config.curriculum_supply_jitter_mode, public_wave);
    float shared_jitter = ctx->config.curriculum_supply_shared_jitter;
    float brew_jitter = ctx->config.curriculum_supply_brew_jitter;
    float restore_jitter = ctx->config.curriculum_supply_restore_jitter;

    if (jitter_applies &&
            (shared_jitter > 0.0f || brew_jitter > 0.0f || restore_jitter > 0.0f)) {
        float shared = (2.0f * encounter_rand_float(&s->rng_state) - 1.0f) *
            shared_jitter;
        float brew_delta = shared;
        float restore_delta = shared;
        if (brew_jitter > 0.0f) {
            brew_delta += (2.0f * encounter_rand_float(&s->rng_state) - 1.0f) *
                brew_jitter;
        }
        if (restore_jitter > 0.0f) {
            restore_delta += (2.0f * encounter_rand_float(&s->rng_state) - 1.0f) *
                restore_jitter;
        }

        s->player.brew_doses = inf_clamp_supply_doses(
            s->player.brew_doses +
                inf_round_signed_float_to_int((float)full.brew_doses * brew_delta),
            full.brew_doses);
        s->player.restore_doses = inf_clamp_supply_doses(
            s->player.restore_doses +
                inf_round_signed_float_to_int((float)full.restore_doses * restore_delta),
            full.restore_doses);
    }

    int no_brew_applies = inf_curriculum_supply_mode_applies(
        ctx->config.curriculum_no_brew_mode, public_wave);
    if (no_brew_applies &&
            ctx->config.curriculum_no_brew_frac > 0.0f &&
            encounter_rand_float(&s->rng_state) < ctx->config.curriculum_no_brew_frac) {
        s->player.brew_doses = 0;
    }
}

static InfSupplyDoses inf_supplies_for_start_wave(InfSupplyDoses full,
                                                  int internal_start_wave,
                                                  float scale) {
    inf_require_valid_supply_scale(scale);

    int public_wave = internal_start_wave + 1;

    InfSupplyFractions fractions = inf_supply_profile_fractions(public_wave);
    return (InfSupplyDoses){
        .brew_doses = inf_profiled_supply_count(full.brew_doses,
            fractions.brew_fraction, scale),
        .restore_doses = inf_profiled_supply_count(full.restore_doses,
            fractions.restore_fraction, scale),
        .bastion_doses = inf_profiled_supply_count(full.bastion_doses,
            fractions.bastion_fraction, scale),
        .stamina_doses = inf_profiled_supply_count(full.stamina_doses,
            fractions.stamina_fraction, scale),
    };
}

static float inf_supply_milestone_surplus_reward(
    InfernoState* s,
    const InfernoContext* ctx,
    int public_wave
) {
    if (ctx->config.supply_milestone_brew_reward_coeff <= 0.0f &&
            ctx->config.supply_milestone_restore_reward_coeff <= 0.0f) {
        return 0.0f;
    }

    int anchor_idx = inf_supply_profile_anchor_index(public_wave);
    if (anchor_idx <= 0) return 0.0f;

    uint32_t anchor_bit = 1u << (uint32_t)anchor_idx;
    if ((s->supply_milestone_rewarded_mask & anchor_bit) != 0)
        return 0.0f;
    s->supply_milestone_rewarded_mask |= anchor_bit;

    InfSupplyDoses full = inf_full_starting_supplies();
    InfSupplyDoses expected = inf_supplies_for_start_wave(
        full, public_wave - 1, ctx->config.late_start_supply_profile_scale);

    int brew_surplus = s->player.brew_doses - expected.brew_doses;
    int restore_surplus = s->player.restore_doses - expected.restore_doses;
    if (brew_surplus < 0) brew_surplus = 0;
    if (restore_surplus < 0) restore_surplus = 0;

    return
        ctx->config.supply_milestone_brew_reward_coeff *
            (float)brew_surplus / (float)full.brew_doses +
        ctx->config.supply_milestone_restore_reward_coeff *
            (float)restore_surplus / (float)full.restore_doses;
}

static void inf_reset_ctx(EncounterState* state, EncounterContext* context, uint32_t seed) {
    inf_build_npc_stats();
    InfernoState* s = (InfernoState*)state;
    InfernoContext* ctx = (InfernoContext*)context;
    if (!ctx) ctx = inf_legacy_context();
    int saved_start = ctx->config.start_wave;
    uint32_t saved_rng = s->rng_state;
    memset(s, 0, sizeof(InfernoState));
    s->start_wave = saved_start;
    s->rng_state = encounter_resolve_seed(saved_rng, seed);

    /* human click-to-move: no destination after reset */
    s->player_dest_x = -1;
    s->player_dest_y = -1;
    s->player_last_interaction_target_slot = -1;
    s->player_last_interaction_age = 1;

    s->tick_at_le_300 = -1;
    s->tick_at_le_240 = -1;
    s->tick_at_le_150 = -1;
    s->tick_at_zuk_healer_spawn = -1;
    s->tick_at_first_zuk_healer_tag = -1;
    s->tick_at_all_zuk_healers_tagged = -1;
    s->tick_at_all_zuk_healers_dead = -1;
    s->tick_at_first_zuk_hit_after_all_healers_dead = -1;
    s->tick_at_first_zuk_healer_target = -1;
    s->tick_at_first_zuk_healer_attack = -1;
    s->total_zuk_healer_target_ticks = 0;
    s->total_zuk_healer_attack_fires = 0;
    s->total_zuk_healer_cannot_attack_ticks = 0;
    s->total_zuk_healer_cooldown_ticks = 0;
    s->total_zuk_healer_out_of_range_ticks = 0;
    s->total_zuk_healer_attackable_ticks = 0;
    s->total_action_mask_checks = 0;
    for (int h = 0; h < 9; h++) {
        s->zero_valid_action_head_count[h] = 0;
        s->min_valid_action_count_by_head[h] = 1000000;
    }
    s->target_head_valid_healer_count = 0;
    s->target_head_valid_zuk_count = 0;
    s->target_head_valid_set_count = 0;

    /* player */
    s->player.entity_type = ENTITY_PLAYER;
    s->player.base_hitpoints = 99;
    s->player.current_hitpoints = 99;
    s->player.base_prayer = 99;
    s->player.current_prayer = 99;
    s->player.base_attack = 99;
    s->player.base_strength = 99;
    s->player.base_defence = 99;
    s->player.base_ranged = 99;
    s->player.base_magic = 99;
    s->player.current_ranged = 99;
    s->player.current_magic = 99;
    s->player.current_attack = 99;
    s->player.current_strength = 99;
    s->player.current_defence = 99;
    osrs_item_effect_state_init(&s->player.item_effect_state);
    /* start in mage gear */
    s->weapon_set = INF_GEAR_MAGE;
    s->active_loadout_profile = inf_sample_loadout_profile(
        ctx->config.loadout_profile_mode,
        ctx->config.budget_loadout_fraction,
        &s->rng_state);
    const uint8_t* const* loadouts =
        inf_loadouts_for_profile(s->active_loadout_profile);
    encounter_apply_loadout(&s->player, loadouts[INF_GEAR_MAGE], GEAR_MAGE);
    {
        encounter_populate_inventory(&s->player, loadouts, INF_NUM_WEAPON_SETS, NULL);

        /* Ammo slot items (dragon darts, dragon arrows) should NOT appear as
           swappable inventory items — in real OSRS darts live inside the
           blowpipe and arrows inside dizana's quiver. Clear them here; the
           equipment panel still shows the correct ammo for the active weapon. */
        for (int i = 0; i < MAX_ITEMS_PER_SLOT; i++) {
            s->player.inventory[GEAR_SLOT_AMMO][i] = ITEM_NONE;
        }
        s->player.num_items_in_slot[GEAR_SLOT_AMMO] = 0;
    }
    InfSupplyDoses full_supplies = inf_full_starting_supplies();
    InfSupplyDoses start_supplies = inf_supplies_for_start_wave(
        full_supplies, s->start_wave, ctx->config.late_start_supply_profile_scale);
    s->player.brew_doses = start_supplies.brew_doses;
    s->player.restore_doses = start_supplies.restore_doses;
    s->player.bastion_doses = start_supplies.bastion_doses;
    s->player.stamina_doses = start_supplies.stamina_doses;
    inf_apply_curriculum_supply_variation(s, ctx, full_supplies);
    s->stamina_active_ticks = 0;
    s->player.prayer = PRAYER_NONE;
    s->player.autocast_enabled = 1;
    s->player.autocast_defensive = 0;
    s->player.autocast_spell = ENCOUNTER_SPELL_BLOOD;
    osrs_interaction_init(&s->interaction);
    s->player.spec_armed = 0;
    s->player.special_energy = 100;
    s->player.run_energy = 10000;  /* full run energy (OSRS stores as 0-10000) */
    s->last_hit_by_type = -1;

    /* compute loadout stats from item database (replaces old hardcoded INF_WEAPON_STATS).
       mage is kodai + barrage — pure autocast, no invisible bonus.
       ranged loadouts run in rapid stance: -1 to attack_speed. */
    /* offensive prayer is now agent-controlled runtime state (Player.offensive_prayer),
       not baked into the loadout. pass NONE at reset; inf_player_pretick() calls
       encounter_update_loadout_level() whenever the agent toggles a prayer. */
    encounter_compute_loadout_stats(loadouts[INF_GEAR_MAGE], ATTACK_STYLE_MAGIC,
        OFFENSIVE_PRAYER_NONE, 99, FIGHT_STYLE_AUTOCAST, 30, &s->loadout_stats[INF_GEAR_MAGE]);
    encounter_compute_loadout_stats(loadouts[INF_GEAR_LONG_RANGE], ATTACK_STYLE_RANGED,
        OFFENSIVE_PRAYER_NONE, 99, FIGHT_STYLE_RAPID, 0, &s->loadout_stats[INF_GEAR_LONG_RANGE]);
    encounter_compute_loadout_stats(loadouts[INF_GEAR_BP], ATTACK_STYLE_RANGED,
        OFFENSIVE_PRAYER_NONE, 99, FIGHT_STYLE_RAPID, 0, &s->loadout_stats[INF_GEAR_BP]);

    /* spawn position depends on wave */
    int effective_start = inf_runtime_wave_from_start_wave(s->start_wave);
    int is_zuk_wave = (effective_start >= INF_WAVE_ZUK);
    s->player.x = is_zuk_wave ? INF_ZUK_PLAYER_START_X : INF_PLAYER_START_X;
    s->player.y = is_zuk_wave ? INF_ZUK_PLAYER_START_Y : INF_PLAYER_START_Y;
    inf_rebuild_player_collision_flags(s);

    /* pillars: all destroyed at end of wave 66 (index 65), so waves 66+ have none */
    for (int i = 0; i < INF_NUM_PILLARS; i++) {
        s->pillars[i].x = INF_PILLAR_POS[i][0];
        s->pillars[i].y = INF_PILLAR_POS[i][1];
        if (effective_start >= 66) {
            s->pillars[i].hp = 0;
            s->pillars[i].active = 0;
        } else {
            s->pillars[i].hp = INF_PILLAR_HP;
            s->pillars[i].active = 1;
        }
    }
    inf_rebuild_los(s);

    /* dead mob store */
    s->dead_mob_count = 0;

    s->wave = effective_start;
    s->wave_spawn_target = effective_start;
    s->wave_spawn_delay = 0;
    s->wave_ready_delay = INF_START_READY_TICKS;
    inf_spawn_wave(s);
    inf_invalidate_los_cache(s);
}

static void inf_reset(EncounterState* state, uint32_t seed) {
    inf_reset_ctx(state, (EncounterContext*)inf_legacy_context(), seed);
}


/* find a free NPC slot, return index or -1 */
static int inf_find_free_npc(InfernoState* s) {
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        if (!s->npcs[i].active) return i;
    }
    return -1;
}

static int inf_grid_index(int x, int y, int* gx, int* gy) {
    *gx = x - INF_ARENA_MIN_X;
    *gy = y - INF_ARENA_MIN_Y;
    return *gx >= 0 && *gx < INF_ARENA_WIDTH && *gy >= 0 && *gy < INF_ARENA_HEIGHT;
}

static int inf_npc_sets_collision_flag(InfNPCType type) {
    return type != INF_NPC_NIBBLER && type != INF_NPC_ZUK_SHIELD;
}

static int inf_npc_effective_size(const InfNPC* npc) {
    return npc->size > 0 ? npc->size : INF_NPC_STATS[npc->type].size;
}

static void inf_unstamp_npc_collision_footprint(InfernoState* s, int x, int y, int size) {
    for (int dx = 0; dx < size; dx++) {
        for (int dy = 0; dy < size; dy++) {
            int gx, gy;
            if (inf_grid_index(x + dx, y + dy, &gx, &gy)) {
                if (s->npc_collision_flags[gx][gy] > 0)
                    s->npc_collision_flags[gx][gy]--;
            }
        }
    }
}

static void inf_stamp_npc_collision_footprint(InfernoState* s, int x, int y, int size) {
    for (int dx = 0; dx < size; dx++) {
        for (int dy = 0; dy < size; dy++) {
            int gx, gy;
            if (inf_grid_index(x + dx, y + dy, &gx, &gy)) {
                assert(s->npc_collision_flags[gx][gy] < UINT8_MAX);
                s->npc_collision_flags[gx][gy]++;
            }
        }
    }
}

static void inf_clear_player_collision_flags(InfernoState* s) {
    memset(s->player_collision_flags, 0, sizeof(s->player_collision_flags));
}

static void inf_stamp_player_collision_flags(InfernoState* s) {
    int gx, gy;
    if (inf_grid_index(s->player.x, s->player.y, &gx, &gy))
        s->player_collision_flags[gx][gy] = 1;
}

static void inf_rebuild_player_collision_flags(InfernoState* s) {
    inf_clear_player_collision_flags(s);
    inf_stamp_player_collision_flags(s);
}

static void inf_rebuild_entity_collision_flags(InfernoState* s) {
    memset(s->npc_collision_flags, 0, sizeof(s->npc_collision_flags));
    inf_rebuild_player_collision_flags(s);
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        InfNPC* npc = &s->npcs[i];
        if (!npc->active) continue;
        if (!inf_npc_sets_collision_flag(npc->type)) continue;
        inf_stamp_npc_collision_footprint(s, npc->x, npc->y, inf_npc_effective_size(npc));
    }
}

static void inf_update_npc_collision_flags(
    InfernoState* s, int idx, int ox, int oy, int nx, int ny, int sz
) {
    if (idx >= 0 && idx < INF_MAX_NPCS &&
        !inf_npc_sets_collision_flag(s->npcs[idx].type))
        return;
    inf_unstamp_npc_collision_footprint(s, ox, oy, sz);
    inf_stamp_npc_collision_footprint(s, nx, ny, sz);
}

static void inf_deactivate_npc(InfernoState* s, int idx) {
    if (idx < 0 || idx >= INF_MAX_NPCS) return;
    InfNPC* npc = &s->npcs[idx];
    if (npc->active && inf_npc_sets_collision_flag(npc->type))
        inf_unstamp_npc_collision_footprint(s, npc->x, npc->y, inf_npc_effective_size(npc));
    npc->active = 0;
}

/* initialize an NPC at a given slot */
static void inf_init_npc(InfernoState* s, int idx, InfNPCType type, int x, int y) {
    InfNPC* npc = &s->npcs[idx];
    const InfNPCStats* stats = &INF_NPC_STATS[type];
    memset(npc, 0, sizeof(InfNPC));

    s->next_npc_render_id++;
    if (s->next_npc_render_id == 0)
        s->next_npc_render_id = 1;

    npc->type = type;
    npc->hp = stats->hp;
    npc->max_hp = stats->hp;
    npc->size = stats->size;
    npc->render_id = s->next_npc_render_id;
    npc->attack_timer = stats->attack_speed;
    npc->attack_style = stats->default_style;
    npc->jad_attack_style = ATTACK_STYLE_NONE;
    npc->attack_style_this_tick = ATTACK_STYLE_NONE;
    npc->active = 1;
    npc->x = x;
    npc->y = y;
    npc->target_x = x;
    npc->target_y = y;
    npc->attack_visual_target = -1;
    npc->resurrection_visual_target = -1;
    npc->heal_target = -1;
    npc->jad_owner_idx = -1;
    npc->aggro_target = -1;
    npc->blob_scanned_prayer = -1;
    npc->had_los_last_tick = 0;
    npc->stun_timer = stats->stun_on_spawn;

    if (inf_npc_sets_collision_flag(type))
        inf_stamp_npc_collision_footprint(s, x, y, stats->size);
}

static int inf_find_first_active_npc_of_type(const InfernoState* s, InfNPCType type) {
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        if (s->npcs[i].active && s->npcs[i].type == type) return i;
    }
    return -1;
}

static void inf_seed_joseph_zuk_checkpoint(InfernoState* s) {
    if (s->start_wave < INF_START_WAVE_ZUK_JAD) return;

    int zuk_idx = inf_find_first_active_npc_of_type(s, INF_NPC_ZUK);
    int shield_idx = s->zuk.shield_idx;
    if (zuk_idx < 0 || shield_idx < 0 || shield_idx >= INF_MAX_NPCS ||
            !s->npcs[shield_idx].active)
        return;

    InfNPC* zuk = &s->npcs[zuk_idx];
    zuk->stun_timer = 0;
    s->zuk.initial_delay = 0;
    s->zuk.has_paused = 1;
    s->zuk.timer_paused = 0;
    s->zuk.jad_spawned = 1;
    s->zuk.shield_freeze = 0;

    if (s->start_wave == INF_START_WAVE_ZUK_JAD) {
        s->zuk.set_timer = 247;
        s->zuk.enraged = 0;
        s->zuk.healer_spawned = 0;
        zuk->hp = 479;
        zuk->attack_timer = INF_NPC_STATS[INF_NPC_ZUK].attack_speed;
        s->min_zuk_hp_seen = (float)zuk->hp;

        int j_slot = inf_find_free_npc(s);
        if (j_slot >= 0) {
            inf_init_npc(s, j_slot, INF_NPC_JAD, 24, 32);
            s->npcs[j_slot].aggro_target = shield_idx;
            s->npcs[j_slot].stun_timer = 7;
        }
        return;
    }

    s->zuk.set_timer = s->zuk.set_interval;
    s->zuk.enraged = 1;
    s->zuk.healer_spawned = 1;
    zuk->hp = 239;
    zuk->attack_timer = 7;
    s->min_zuk_hp_seen = (float)zuk->hp;

    static const int healer_pos[4][2] = {
        {16, 48}, {20, 48}, {30, 48}, {34, 48}
    };
    for (int h = 0; h < 4; h++) {
        int slot = inf_find_free_npc(s);
        if (slot >= 0) {
            inf_init_npc(s, slot, INF_NPC_HEALER_ZUK,
                healer_pos[h][0], healer_pos[h][1]);
            s->npcs[slot].aggro_target = zuk_idx;
        }
    }
}

static void inf_spawn_wave(InfernoState* s) {
    if (s->wave >= INF_NUM_WAVES) return;

    const InfWaveDef* w = &INF_WAVES[s->wave];

    /* clear all NPCs and pending hits */
    for (int i = 0; i < INF_MAX_NPCS; i++) s->npcs[i].active = 0;
    s->player_pending_hit_count = 0;
    memset(s->npc_collision_flags, 0, sizeof(s->npc_collision_flags));
    inf_rebuild_player_collision_flags(s);

    /* clear dead mob store each wave */
    s->dead_mob_count = 0;

    /* shuffle spawn positions */
    inf_shuffle_spawns(s);

    /* pick random active pillar for nibblers this wave */
    {
        int active_pillars[INF_NUM_PILLARS];
        int num_active = 0;
        for (int p = 0; p < INF_NUM_PILLARS; p++) {
            if (s->pillars[p].active) active_pillars[num_active++] = p;
        }
        s->nibbler_target_pillar = (num_active > 0)
            ? active_pillars[encounter_rand_int(&s->rng_state, num_active)] : -1;
    }

    /* waves 67-69 have no pillars — ref InfernoRegion.ts:359
       (`this.wave < 67 || this.wave >= 70` means pillars exist outside this range).
       clear on spawn so mid-episode transitions also collapse any survivors. */
    if (s->wave >= 66) {
        int pillars_changed = 0;
        for (int p = 0; p < INF_NUM_PILLARS; p++) {
            if (s->pillars[p].active) {
                s->pillars[p].active = 0;
                s->pillars[p].hp = 0;
                pillars_changed = 1;
            }
        }
        if (pillars_changed) inf_rebuild_los(s);
    }

    /* wave 67 (index 66): single jad at fixed position. ref InfernoRegion.ts:441-451.
       our coord system is Y-flipped vs reference (pillars confirm ours_y = 57 - ref_y).
       ref: player (18, 25), jad (23, 27), stun=1, attackSpeed=8, healers=5. */
    if (s->wave == 66) {
        s->player.x = 18;
        s->player.y = 32;  /* 57 - 25 */
        inf_rebuild_player_collision_flags(s);
        int slot = inf_find_free_npc(s);
        if (slot >= 0) {
            inf_init_npc(s, slot, INF_NPC_JAD, 23, 30);  /* 57 - 27 = 30 */
            s->npcs[slot].stun_timer = 1;
            s->npcs[slot].attack_timer = 8;
        }
        return;
    }

    /* wave 68 (index 67): three jads at fixed positions with staggered first attacks.
       ref InfernoRegion.ts:452-479. stunTimers [1,4,7] shuffled across the 3 jads.
       This sim decrements attack_timer during stun, so the initial attack timer
       includes the stun offset to preserve the reference first-attack cadence.
       ref: player (25, 27), jads (18, 24), (28, 24), (23, 35). attackSpeed=9, healers=3. */
    if (s->wave == 67) {
        s->player.x = 25;
        s->player.y = 30;  /* 57 - 27 */
        inf_rebuild_player_collision_flags(s);
        /* shuffle [1, 4, 7] via Fisher-Yates */
        int stuns[3] = { 1, 4, 7 };
        for (int i = 2; i > 0; i--) {
            int j = encounter_rand_int(&s->rng_state, i + 1);
            int tmp = stuns[i]; stuns[i] = stuns[j]; stuns[j] = tmp;
        }
        static const int JAD_POS[3][2] = {
            {18, 33},  /* 57 - 24 */
            {28, 33},  /* 57 - 24 */
            {23, 22},  /* 57 - 35 */
        };
        for (int i = 0; i < 3; i++) {
            int slot = inf_find_free_npc(s);
            if (slot < 0) break;
            inf_init_npc(s, slot, INF_NPC_JAD, JAD_POS[i][0], JAD_POS[i][1]);
            s->npcs[slot].stun_timer = stuns[i];
            s->npcs[slot].attack_timer =
                inf_initial_attack_timer_after_stun(9, stuns[i]);
        }
        return;
    }

    /* zuk wave (wave 69, index 68) is special */
    if (s->wave == 68) {
        /* spawn Zuk — fixed position, cannot move */
        int zuk_idx = inf_find_free_npc(s);
        if (zuk_idx >= 0) {
            inf_init_npc(s, zuk_idx, INF_NPC_ZUK, INF_ZUK_X, INF_ZUK_Y);
            s->min_zuk_hp_seen = (float)s->npcs[zuk_idx].hp;
            /* InfernoTrainer: stunned=8, attackDelay=14. stun counts down first,
               then attackDelay ticks down to 0 before first attack fires. */
            s->npcs[zuk_idx].stun_timer = 8;
            s->npcs[zuk_idx].attack_timer = 14;
        }

        /* spawn shield */
        int shield_idx = inf_find_free_npc(s);
        if (shield_idx >= 0) {
            inf_init_npc(s, shield_idx, INF_NPC_ZUK_SHIELD, INF_ZUK_SHIELD_X, INF_ZUK_SHIELD_Y);
            s->zuk.shield_idx = shield_idx;
            s->zuk.shield_dir = (encounter_rand_int(&s->rng_state, 2) == 0) ? 1 : -1;
            s->zuk.shield_freeze = 1;  /* 1-tick freeze on spawn */
        }

        /* zuk state */
        s->zuk.initial_delay = 14;
        s->zuk.set_timer = 72;
        s->zuk.set_interval = 350;
        s->zuk.enraged = 0;
        s->zuk.healer_spawned = 0;
        s->zuk.jad_spawned = 0;
        s->zuk.timer_paused = 0;
        s->zuk.has_paused = 0;

        /* player starts at zuk position */
        s->player.x = INF_ZUK_PLAYER_START_X;
        s->player.y = INF_ZUK_PLAYER_START_Y;
        inf_rebuild_player_collision_flags(s);
        inf_seed_joseph_zuk_checkpoint(s);
        return;
    }

    /* regular waves: spawn NPCs at shuffled positions */
    int spawn_idx = 0;
    for (int i = 0; i < w->count && i < INF_MAX_NPCS; i++) {
        InfNPCType type = (InfNPCType)w->types[i];
        int slot = inf_find_free_npc(s);
        if (slot < 0) break;

        int sx, sy;
        if (type == INF_NPC_NIBBLER) {
            /* nibblers spawn near pillars with small random offset */
            sx = INF_NIBBLER_SPAWN_X + encounter_rand_int(&s->rng_state, 3) - 1;
            sy = INF_NIBBLER_SPAWN_Y + encounter_rand_int(&s->rng_state, 3) - 1;
        } else {
            int pi = s->spawn_order[spawn_idx % INF_NUM_SPAWN_POS];
            sx = INF_SPAWN_POS[pi][0];
            sy = INF_SPAWN_POS[pi][1];
            spawn_idx++;
        }

        inf_init_npc(s, slot, type, sx, sy);
    }
}
