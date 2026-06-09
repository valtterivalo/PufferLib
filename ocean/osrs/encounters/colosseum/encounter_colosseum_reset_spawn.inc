#line 76 "encounter_colosseum.h"
/**
 * @file encounter_colosseum_reset_spawn.inc
 * @brief Episode setup + wave spawning, mirroring inferno reset_spawn.
 */

static int col_grid_index(int x, int y, int* gx, int* gy) {
    *gx = x - COLO_ARENA_MIN_X;
    *gy = y - COLO_ARENA_MIN_Y;
    return *gx >= 0 && *gx < COLO_ARENA_WIDTH && *gy >= 0 && *gy < COLO_ARENA_HEIGHT;
}

static int col_npc_effective_size(const ColoNPC* npc) {
    return npc->size > 0 ? npc->size : COLO_NPC_STATS[npc->type].size;
}

static void col_stamp_npc_collision_footprint(ColosseumState* s, int x, int y, int size, uint8_t v) {
    for (int dx = 0; dx < size; dx++) {
        for (int dy = 0; dy < size; dy++) {
            int gx, gy;
            if (col_grid_index(x + dx, y + dy, &gx, &gy))
                s->npc_collision_flags[gx][gy] = v;
        }
    }
}

static void col_rebuild_player_collision_flags(ColosseumState* s) {
    memset(s->player_collision_flags, 0, sizeof(s->player_collision_flags));
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        ColoNPC* npc = &s->npcs[i];
        if (!npc->active) continue;
        int size = col_npc_effective_size(npc);
        for (int dx = 0; dx < size; dx++) {
            for (int dy = 0; dy < size; dy++) {
                int gx, gy;
                if (col_grid_index(npc->x + dx, npc->y + dy, &gx, &gy))
                    s->player_collision_flags[gx][gy] = 1;
            }
        }
    }
}

/** find a free NPC slot, return index or -1. */
static int col_find_free_npc(ColosseumState* s) {
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        if (!s->npcs[i].active) return i;
    }
    return -1;
}

static void col_deactivate_npc(ColosseumState* s, int idx) {
    ColoNPC* npc = &s->npcs[idx];
    int size = col_npc_effective_size(npc);
    col_stamp_npc_collision_footprint(s, npc->x, npc->y, size, 0);
    npc->active = 0;
}

/** initialize a fresh NPC of `type` at (x, y) from the placeholder stat table. */
static void col_init_npc(ColosseumState* s, int idx, ColoNpcType type, int x, int y) {
    ColoNPC* npc = &s->npcs[idx];
    memset(npc, 0, sizeof(*npc));
    npc->type = type;
    colo_npc_init_type_state(npc);
    const ColoNpcStats* st = &COLO_NPC_STATS[type];
    npc->x = x;
    npc->y = y;
    npc->hp = st->hp;
    npc->max_hp = st->hp;
    npc->size = st->size;
    npc->render_id = ++s->next_npc_render_id;
    npc->attack_timer = st->attack_speed;
    npc->attack_style = st->default_style;
    npc->active = 1;
    npc->target_x = x;
    npc->target_y = y;
    npc->stun_timer = st->stun_on_spawn;
    npc->aggro_target = -1;
    if (col_npc_effective_size(npc) > 0)
        col_stamp_npc_collision_footprint(s, x, y, col_npc_effective_size(npc), 1);
}

/** spawn the current wave's NPCs at shuffled spawn positions. */
static void col_spawn_wave(ColosseumState* s) {
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        if (s->npcs[i].active) col_deactivate_npc(s, i);
    }

    if (s->wave < 0 || s->wave >= COLO_NUM_WAVES) return;
    const ColoWaveDef* def = &COLO_WAVES[s->wave];

    for (int i = 0; i < COLO_NUM_SPAWN_POS; i++) s->spawn_order[i] = i;
    encounter_shuffle(s->spawn_order, COLO_NUM_SPAWN_POS, &s->rng_state);

    for (int i = 0; i < def->count; i++) {
        int slot = col_find_free_npc(s);
        if (slot < 0) break;
        ColoNpcType type = (ColoNpcType)def->types[i];
        int x, y;
        if (type == COLO_SOL_HEREDIT) {
            x = COLO_SOL_HEREDIT_X;
            y = COLO_SOL_HEREDIT_Y;
        } else {
            int pos = s->spawn_order[i % COLO_NUM_SPAWN_POS];
            x = COLO_SPAWN_POS[pos][0];
            y = COLO_SPAWN_POS[pos][1];
        }
        col_init_npc(s, slot, type, x, y);
    }

    s->reinforcement_timer = COLO_REINFORCEMENT_TICKS;

    if (s->wave == COLO_WAVE_BOSS) col_sol_begin_boss_arena(s);
}

/** sentinel reinforcement_timer value meaning this wave already reinforced. */
#define COLO_REINFORCE_FIRED (-1)

/**
 * Spawn the current wave's reinforcement species from the gate nearer the player.
 *
 * Gate choice is the smaller |player.y - gate_y| between the north and south
 * gates. Stacks each species one tile apart along x so 3x3 colossi do not share
 * a footprint. Sets reinforcement_timer to COLO_REINFORCE_FIRED so an overrun
 * reinforces exactly once per wave.
 */
static void col_spawn_reinforcements(ColosseumState* s) {
    if (s->wave < 0 || s->wave >= COLO_NUM_WAVES) return;
    const ColoWaveDef* def = &COLO_WAVES[s->wave];
    if (def->reinforce_count <= 0) {
        s->reinforcement_timer = COLO_REINFORCE_FIRED;
        return;
    }

    int dist_north = abs(s->player.y - COLO_GATE_NORTH_Y);
    int dist_south = abs(s->player.y - COLO_GATE_SOUTH_Y);
    int gate_y = (dist_north <= dist_south) ? COLO_GATE_NORTH_Y : COLO_GATE_SOUTH_Y;

    for (int i = 0; i < def->reinforce_count; i++) {
        int slot = col_find_free_npc(s);
        if (slot < 0) break;
        ColoNpcType type = (ColoNpcType)def->reinforce[i];
        int x = COLO_GATE_X + i * COLO_NPC_STATS[type].size;
        if (x + COLO_NPC_STATS[type].size > COLO_ARENA_MAX_X) x = COLO_GATE_X - i;
        col_init_npc(s, slot, type, x, gate_y);
    }

    s->reinforcement_timer = COLO_REINFORCE_FIRED;
}


static void col_reset_ctx(EncounterState* state, EncounterContext* context, uint32_t seed) {
    col_build_npc_stats();
    ColosseumState* s = (ColosseumState*)state;
    ColosseumContext* ctx = context ? (ColosseumContext*)context : col_legacy_context();
    int saved_start = ctx->config.start_wave;
    uint32_t saved_rng = s->rng_state;
    memset(s, 0, sizeof(ColosseumState));
    s->start_wave = saved_start;
    s->rng_state = encounter_resolve_seed(saved_rng, seed);

    s->player_dest_x = -1;
    s->player_dest_y = -1;
    s->last_hit_by_type = -1;
    for (int i = 0; i < COLO_OBS_NPCS; i++) s->current_obs_slots[i] = -1;

    /* player: maxed combat stats + a melee/ranged loadout via shared helpers */
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
    s->player.current_attack = 99;
    s->player.current_strength = 99;
    s->player.current_defence = 99;
    s->player.current_ranged = 99;
    s->player.current_magic = 99;
    osrs_item_effect_state_init(&s->player.item_effect_state);

    s->weapon_set = COLO_GEAR_MELEE;
    encounter_apply_loadout(&s->player, COLO_LOADOUTS[COLO_GEAR_MELEE], GEAR_MELEE);
    encounter_populate_inventory(&s->player, COLO_LOADOUTS, COLO_NUM_WEAPON_SETS, NULL);

    s->player.prayer = PRAYER_NONE;
    s->player.offensive_prayer = OFFENSIVE_PRAYER_NONE;
    osrs_interaction_init(&s->interaction);
    s->player.spec_armed = 0;
    s->player.special_energy = 100;
    s->player.run_energy = 10000;

    encounter_compute_loadout_stats(COLO_LOADOUTS[COLO_GEAR_MELEE], ATTACK_STYLE_MELEE,
        OFFENSIVE_PRAYER_NONE, 99, FIGHT_STYLE_AGGRESSIVE, 0, &s->loadout_stats[COLO_GEAR_MELEE]);
    encounter_compute_loadout_stats(COLO_LOADOUTS[COLO_GEAR_RANGED], ATTACK_STYLE_RANGED,
        OFFENSIVE_PRAYER_NONE, 99, FIGHT_STYLE_RAPID, 0, &s->loadout_stats[COLO_GEAR_RANGED]);

    s->player.x = COLO_PLAYER_START_X;
    s->player.y = COLO_PLAYER_START_Y;
    col_rebuild_player_collision_flags(s);

    int effective_start = s->start_wave;
    if (effective_start < 0) effective_start = 0;
    if (effective_start >= COLO_NUM_WAVES) effective_start = COLO_NUM_WAVES - 1;
    s->wave = effective_start;
    s->wave_spawn_target = effective_start;
    s->wave_spawn_delay = 0;
    s->wave_ready_delay = COLO_START_READY_TICKS;
    s->log.wave_reached = effective_start;
    col_spawn_wave(s);
}

static void col_reset(EncounterState* state, uint32_t seed) {
    col_reset_ctx(state, (EncounterContext*)col_legacy_context(), seed);
}
