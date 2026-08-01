#line 76 "encounter_colosseum.h"

#define COLO_STYLE_MASK_MELEE  1
#define COLO_STYLE_MASK_RANGED 2
#define COLO_STYLE_MASK_MAGIC  4

typedef struct {
    const ColosseumState* state;
    uint8_t (*npc_flags)[COLO_ARENA_HEIGHT];
    int player_x;
    int player_y;
    int sol_clamp_active;
} ColoForecastMoveCtx;

typedef struct {
    int held;
    int dist;
    int los;
    int npc_x;
    int npc_y;
    int land_x;
    int land_y;
} ColoForecastMoveResult;

typedef struct {
    int valid;
    int land_x;
    int land_y;
} ColoForecastLanding;

typedef struct {
    int x;
    int y;
    int size;
    int move_timer;
} ColoBeeActionForecast;

typedef struct {
    int found;
    int npc_slot;
    int x;
    int y;
    int size;
    int ticks;
    int distance;
} ColoVolatilityBlastPreview;

typedef struct {
    int bee_count;
    ColoBeeActionForecast bees[COLO_MAX_BEE_SWARMS];
    ColoVolatilityBlastPreview volatility;
} ColoPrimaryEnvHazardBasis;

typedef struct {
    ColoForecastLanding landings[COLO_PRIMARY_DIM];
    ColoPrimaryEnvHazardBasis hazard_basis;
} ColoPrimaryActionForecast;

typedef struct {
    int first_attack_tick;
    int first_style_mask;
    int max_hit;
    int ranged_magic_same_tick;
} ColoForecastObsSummary;

static inline ColoVolatilityBlastPreview col_volatility_blast_preview_none(void) {
    return (ColoVolatilityBlastPreview){
        .found = 0,
        .npc_slot = -1,
        .x = 0,
        .y = 0,
        .size = 0,
        .ticks = 0,
        .distance = 1 << 30,
    };
}

static ColoVolatilityBlastPreview col_soonest_queued_volatility_blast(
    const ColosseumState* s
) {
    ColoVolatilityBlastPreview best = col_volatility_blast_preview_none();
    for (int n = 0; n < COLO_MAX_NPCS; n++) {
        const ColoNPC* npc = &s->npcs[n];
        if (!col_npc_is_live_enemy(npc)) continue;
        for (int h = 0; h < npc->pending_hits.count; h++) {
            const EncounterPendingHit* hit = &npc->pending_hits.hits[h];
            if (!hit->active) continue;
            int ticks = hit->ticks_remaining < 0 ? 0 : hit->ticks_remaining;
            int damage = 0;
            for (int j = 0; j < npc->pending_hits.count; j++) {
                const EncounterPendingHit* other = &npc->pending_hits.hits[j];
                if (!other->active) continue;
                int other_ticks =
                    other->ticks_remaining < 0 ? 0 : other->ticks_remaining;
                if (other_ticks <= ticks) damage += other->damage;
            }
            if (damage < npc->hp) continue;
            int size = col_npc_effective_size(npc);
            int center_x = npc->x + size / 2;
            int center_y = npc->y + size / 2;
            int dx = abs(center_x - s->player.x);
            int dy = abs(center_y - s->player.y);
            int distance = dx > dy ? dx : dy;
            if (best.found &&
                    (ticks > best.ticks ||
                     (ticks == best.ticks && distance >= best.distance)))
                continue;
            best = (ColoVolatilityBlastPreview){
                .found = 1,
                .npc_slot = n,
                .x = npc->x,
                .y = npc->y,
                .size = size,
                .ticks = ticks,
                .distance = distance,
            };
        }
    }
    return best;
}

static int col_forecast_style_mask(AttackStyle style) {
    if (style == ATTACK_STYLE_MELEE) return COLO_STYLE_MASK_MELEE;
    if (style == ATTACK_STYLE_RANGED) return COLO_STYLE_MASK_RANGED;
    if (style == ATTACK_STYLE_MAGIC) return COLO_STYLE_MASK_MAGIC;
    return 0;
}

static int col_forecast_style_max_hit(ColoNpcType type, AttackStyle style) {
    if (type == COLO_MANTICORE) return col_manticore_orb_max_hit(style);
    return COLO_NPC_STATS[type].max_hit;
}

static int col_step_out_forecast_action_valid(const ColosseumState* s, int action_idx) {
    if (action_idx == 0) return 1;
    if (s->modifiers.draft_pending) return 0;
    int nx = s->player.x + ENCOUNTER_MOVE_TARGET_DX[action_idx];
    int ny = s->player.y + ENCOUNTER_MOVE_TARGET_DY[action_idx];
    return col_player_walkable((void*)s, nx, ny);
}

static ColoForecastLanding col_step_out_forecast_action_landing_ctx(
    const ColosseumState* s,
    int action_idx
) {
    ColoForecastLanding landing = {
        .valid = col_step_out_forecast_action_valid(s, action_idx),
        .land_x = s->player.x,
        .land_y = s->player.y,
    };
    if (action_idx == 0) return landing;

    int target_dx = ENCOUNTER_MOVE_TARGET_DX[action_idx];
    int target_dy = ENCOUNTER_MOVE_TARGET_DY[action_idx];
    int tx = landing.land_x + target_dx;
    int ty = landing.land_y + target_dy;
    int abs_dx = target_dx < 0 ? -target_dx : target_dx;
    int abs_dy = target_dy < 0 ? -target_dy : target_dy;
    int max_steps = abs_dx > abs_dy ? abs_dx : abs_dy;

    for (int step = 0; step < max_steps; step++) {
        if (landing.land_x == tx && landing.land_y == ty) break;
        int dx = 0;
        int dy = 0;
        if (tx > landing.land_x) dx = 1;
        else if (tx < landing.land_x) dx = -1;
        if (ty > landing.land_y) dy = 1;
        else if (ty < landing.land_y) dy = -1;

        int moved = 0;
        if (dx != 0 && dy != 0 &&
                col_player_walkable((void*)s, landing.land_x + dx, landing.land_y + dy) &&
                col_player_walkable((void*)s, landing.land_x + dx, landing.land_y) &&
                col_player_walkable((void*)s, landing.land_x, landing.land_y + dy)) {
            landing.land_x += dx;
            landing.land_y += dy;
            moved = 1;
        } else if (dx != 0 &&
                col_player_walkable((void*)s, landing.land_x + dx, landing.land_y)) {
            landing.land_x += dx;
            moved = 1;
        } else if (dy != 0 &&
                col_player_walkable((void*)s, landing.land_x, landing.land_y + dy)) {
            landing.land_y += dy;
            moved = 1;
        }
        if (!moved) break;
    }

    return landing;
}

static int col_primary_action_valid(
    const ColosseumState* s,
    int primary_action
) {
    if (primary_action < 0 || primary_action >= COLO_PRIMARY_DIM) {
        fprintf(stderr, "colosseum primary action out of range: %d\n", primary_action);
        abort();
    }
    if (primary_action == 0) return 1;
    if (s->modifiers.draft_pending && !s->modifiers.draft_free_movement) return 0;
    if (primary_action < ENCOUNTER_MOVE_ACTIONS) {
        int nx = s->player.x + ENCOUNTER_MOVE_TARGET_DX[primary_action];
        int ny = s->player.y + ENCOUNTER_MOVE_TARGET_DY[primary_action];
        return col_player_walkable((void*)s, nx, ny);
    }

    int obs_slot = col_primary_obs_slot_from_action(primary_action);
    int npc_slot = col_obs_slot_to_npc(s, obs_slot);
    return npc_slot >= 0 && col_npc_is_live_target(&s->npcs[npc_slot]);
}

static ColoForecastLanding col_primary_action_landing_ctx(
    ColosseumState* s,
    ColosseumContext* ctx,
    int primary_action
) {
    ColoForecastLanding landing = {
        .valid = col_primary_action_valid(s, primary_action),
        .land_x = s->player.x,
        .land_y = s->player.y,
    };
    if (!landing.valid ||
            (s->modifiers.draft_pending && !s->modifiers.draft_free_movement)) {
        return landing;
    }

    Player player = s->player;
    OsrsInteraction interaction = s->interaction;
    int player_dest_x = s->player_dest_x;
    int player_dest_y = s->player_dest_y;
    ColoWalkCtx walk_ctx;
    OsrsLosQuery los_query;
    ColoPrimaryStepPlan primary_plan = col_primary_step_plan(
        s, ctx, &player, &interaction, primary_action,
        &player_dest_x, &player_dest_y, &walk_ctx, &los_query);
    osrs_encounter_player_step(&primary_plan.input);
    landing.land_x = player.x;
    landing.land_y = player.y;
    return landing;
}

#define COLO_ATTACK_ROUTE_CACHE_CAPACITY 256

typedef struct {
    const CollisionMap* collision_map;
    int world_offset_x;
    int world_offset_y;
    int source_x;
    int source_y;
    int sol_clamp_active;
    int boss_arena_min_x;
    int boss_arena_min_y;
    int boss_arena_max_x;
    int boss_arena_max_y;
} ColoAttackRouteCacheKey;

typedef struct {
    int valid;
    ColoAttackRouteCacheKey key;
    EncounterArenaAttackRouteField field;
} ColoAttackRouteCacheEntry;

static int col_attack_route_cache_key_equal(
    ColoAttackRouteCacheKey a,
    ColoAttackRouteCacheKey b
) {
    return a.collision_map == b.collision_map &&
        a.world_offset_x == b.world_offset_x &&
        a.world_offset_y == b.world_offset_y &&
        a.source_x == b.source_x &&
        a.source_y == b.source_y &&
        a.sol_clamp_active == b.sol_clamp_active &&
        a.boss_arena_min_x == b.boss_arena_min_x &&
        a.boss_arena_min_y == b.boss_arena_min_y &&
        a.boss_arena_max_x == b.boss_arena_max_x &&
        a.boss_arena_max_y == b.boss_arena_max_y;
}

static EncounterArenaAttackRouteField* col_cached_attack_route_field(
    ColosseumState* s,
    ColosseumContext* ctx,
    ColoWalkCtx* walk_ctx
) {
    static OSRS_THREAD_LOCAL ColoAttackRouteCacheEntry
        cache[COLO_ATTACK_ROUTE_CACHE_CAPACITY];
    static OSRS_THREAD_LOCAL uint32_t next_replacement = 0;
    int clamp = col_sol_clamp_active(s);
    ColoAttackRouteCacheKey key = {
        .collision_map = ctx->collision_map,
        .world_offset_x = ctx->world_offset_x,
        .world_offset_y = ctx->world_offset_y,
        .source_x = s->player.x,
        .source_y = s->player.y,
        .sol_clamp_active = clamp,
        .boss_arena_min_x = clamp ? s->sol.boss_arena_min_x : 0,
        .boss_arena_min_y = clamp ? s->sol.boss_arena_min_y : 0,
        .boss_arena_max_x = clamp ? s->sol.boss_arena_max_x : 0,
        .boss_arena_max_y = clamp ? s->sol.boss_arena_max_y : 0,
    };
    ColoAttackRouteCacheEntry* entry = NULL;
    for (int i = 0; i < COLO_ATTACK_ROUTE_CACHE_CAPACITY; i++) {
        if (cache[i].valid &&
                col_attack_route_cache_key_equal(cache[i].key, key)) {
            entry = &cache[i];
            break;
        }
    }
    if (!entry) {
        entry = &cache[next_replacement % COLO_ATTACK_ROUTE_CACHE_CAPACITY];
        next_replacement++;
        entry->valid = 1;
        entry->key = key;
        encounter_build_arena_attack_route_field(
            &entry->field,
            ctx->collision_map,
            ctx->world_offset_x,
            ctx->world_offset_y,
            s->player.x,
            s->player.y,
            col_tile_walkable,
            walk_ctx,
            col_pathfind_blocked,
            walk_ctx,
            COLO_ARENA_MIN_X,
            COLO_ARENA_MIN_Y,
            COLO_ARENA_WIDTH,
            COLO_ARENA_HEIGHT);
    }
    entry->field.collision_map = ctx->collision_map;
    entry->field.is_walkable = col_tile_walkable;
    entry->field.walkable_ctx = walk_ctx;
    entry->field.extra_blocked = col_pathfind_blocked;
    entry->field.blocked_ctx = walk_ctx;
    return &entry->field;
}

static void col_build_primary_action_forecast_ctx(
    ColosseumState* s,
    ColosseumContext* ctx,
    ColoPrimaryActionForecast* out
) {
    if (!out) abort();
    *out = (ColoPrimaryActionForecast){0};
    ColosseumState phase_state = *s;
    ColosseumContext phase_ctx = *ctx;
    phase_state.profiling_suppressed = 1;
    int phase_actions[COLO_NUM_ACTION_HEADS] = {0};
    ColoPrePlayerPhase phase = col_advance_pre_player_phase_ctx(
        &phase_state, &phase_ctx, phase_actions);

    for (int b = 0; b < COLO_MAX_BEE_SWARMS; b++) {
        const ColoBeeSwarm* bee = &phase_state.bees[b];
        if (bee->phase != COLO_HAZARD_ALIVE ||
                bee->npc_slot < 0 ||
                bee->npc_slot >= COLO_MAX_NPCS) {
            continue;
        }
        const ColoNPC* npc = &phase_state.npcs[bee->npc_slot];
        if (!npc->active || npc->type != COLO_BEE_SWARM) continue;
        out->hazard_basis.bees[out->hazard_basis.bee_count++] =
            (ColoBeeActionForecast){
                .x = npc->x,
                .y = npc->y,
                .size = col_npc_effective_size(npc),
                .move_timer = bee->move_timer,
            };
    }
    out->hazard_basis.volatility =
        col_soonest_queued_volatility_blast(&phase_state);

    for (int primary_action = 0;
            primary_action < COLO_PRIMARY_DIM;
            primary_action++) {
        out->landings[primary_action] = (ColoForecastLanding){
            .valid = col_primary_action_valid(s, primary_action),
            .land_x = s->player.x,
            .land_y = s->player.y,
        };
    }
    if (!phase.player_phase_reached ||
            (phase_state.modifiers.draft_pending &&
             !phase_state.modifiers.draft_free_movement)) {
        return;
    }

    for (int primary_action = 1;
            primary_action < ENCOUNTER_MOVE_ACTIONS;
            primary_action++) {
        if (!out->landings[primary_action].valid) continue;
        out->landings[primary_action] =
            col_primary_action_landing_ctx(
                &phase_state, &phase_ctx, primary_action);
    }

    ColoWalkCtx walk_ctx = {
        .s = &phase_state,
        .ctx = &phase_ctx,
    };
    EncounterArenaAttackRouteField* route_field =
        col_cached_attack_route_field(&phase_state, &phase_ctx, &walk_ctx);
    OsrsLosQuery los_query = col_player_los_query(&phase_state);
    int attack_range = col_player_attack_range(&phase_state);

    if (phase_state.player_dest_x >= 0) {
        out->landings[0] =
            col_primary_action_landing_ctx(&phase_state, &phase_ctx, 0);
    } else if (osrs_interaction_active(&phase_state.interaction)) {
        int target_slot = phase_state.interaction.target_slot;
        if (target_slot >= 0 &&
                target_slot < COLO_MAX_NPCS &&
                col_npc_is_live_target(&phase_state.npcs[target_slot])) {
            ColoNPC* target = &phase_state.npcs[target_slot];
            EncounterAttackRouteLanding route =
                encounter_arena_attack_route_landing(
                    route_field,
                    target->x,
                    target->y,
                    col_npc_effective_size(target),
                    attack_range,
                    &los_query);
            out->landings[0].land_x = route.land_x;
            out->landings[0].land_y = route.land_y;
        }
    }

    for (int primary_action = COLO_PRIMARY_ATTACK_BASE;
            primary_action < COLO_PRIMARY_DIM;
            primary_action++) {
        if (!out->landings[primary_action].valid) continue;
        int obs_slot = col_primary_obs_slot_from_action(primary_action);
        int target_slot = col_obs_slot_to_npc(s, obs_slot);
        if (target_slot < 0 ||
                target_slot >= COLO_MAX_NPCS ||
                !col_npc_is_live_target(&phase_state.npcs[target_slot])) {
            continue;
        }
        ColoNPC* target = &phase_state.npcs[target_slot];
        EncounterAttackRouteLanding route =
            encounter_arena_attack_route_landing(
                route_field,
                target->x,
                target->y,
                col_npc_effective_size(target),
                attack_range,
                &los_query);
        out->landings[primary_action].land_x = route.land_x;
        out->landings[primary_action].land_y = route.land_y;
    }
}

static int col_collect_step_out_forecast_slots(
    const ColosseumState* s,
    int* out
) {
    int n = 0;
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        const ColoNPC* npc = &s->npcs[i];
        if (!npc->active || npc->death_ticks > 0 || npc->hp <= 0) continue;
        if (col_type_is_hazard_entity(npc->type)) continue;
        out[n++] = i;
    }
    return n;
}

static void col_forecast_local_copy_npc_slots(
    const ColosseumState* s,
    ColoForecastNpcLocal out[COLO_MAX_NPCS],
    const int slots[COLO_MAX_NPCS],
    int slot_count
) {
    for (int slot_idx = 0; slot_idx < slot_count; slot_idx++) {
        int i = slots[slot_idx];
        const ColoNPC* src = &s->npcs[i];
        const ColoNpcStats* stats = &COLO_NPC_STATS[src->type];
        ColoForecastNpcLocal* dst = &out[i];
        *dst = (ColoForecastNpcLocal){0};
        dst->active = src->active;
        dst->type = src->type;
        dst->forecast_behavior = COLO_FORECAST_BEHAVIOR_STANDARD;
        if (col_type_is_warbander(src->type))
            dst->forecast_behavior = COLO_FORECAST_BEHAVIOR_WARBAND;
        else if (src->type == COLO_MANTICORE)
            dst->forecast_behavior = COLO_FORECAST_BEHAVIOR_MANTICORE;
        else if (src->type == COLO_SOL_HEREDIT)
            dst->forecast_behavior = COLO_FORECAST_BEHAVIOR_SOL;
        dst->x = src->x;
        dst->y = src->y;
        dst->size = src->size;
        dst->hp = src->hp;
        dst->attack_range = stats->attack_range;
        dst->attack_speed = stats->attack_speed;
        dst->can_move = stats->can_move;
        dst->default_style = stats->default_style;
        dst->attack_timer = src->attack_timer;
        dst->stun_timer = src->stun_timer;
        dst->frozen_ticks = src->frozen_ticks;
        dst->death_ticks = src->death_ticks;
        if (!src->active) continue;
        if (dst->forecast_behavior == COLO_FORECAST_BEHAVIOR_MANTICORE) {
            dst->cycle_step = src->type_state.manticore.cycle_step;
            for (int orb = 0; orb < 3; orb++) {
                dst->orb_style[orb] = src->type_state.manticore.orb_style[orb];
                dst->fixed_orb_style[orb] =
                    src->type_state.manticore.fixed_orb_style[orb];
            }
        } else if (dst->forecast_behavior == COLO_FORECAST_BEHAVIOR_WARBAND) {
            dst->formation_dir = src->type_state.warband.formation_dir;
        } else if (src->type == COLO_JAVELIN_COLOSSUS) {
            dst->javelin_attack_count = src->type_state.javelin.attack_count;
        } else if (dst->forecast_behavior == COLO_FORECAST_BEHAVIOR_SOL) {
            dst->sol_attack_delay = s->sol.attack_delay;
            dst->sol_immobile_ticks = s->sol.immobile_ticks;
        }
    }
}

static void col_forecast_local_stamp_npc(
    uint8_t npc_flags[COLO_ARENA_WIDTH][COLO_ARENA_HEIGHT],
    const ColoForecastNpcLocal* npc,
    uint8_t value
) {
    if (!npc->active || npc->death_ticks > 0 || npc->hp <= 0) return;
    if (col_type_is_hazard_entity(npc->type)) return;
    for (int dx = 0; dx < npc->size; dx++) {
        for (int dy = 0; dy < npc->size; dy++) {
            int gx, gy;
            if (col_grid_index(npc->x + dx, npc->y + dy, &gx, &gy))
                npc_flags[gx][gy] = value;
        }
    }
}

static void col_forecast_local_rebuild_npc_flags(
    ColoForecastNpcLocal npcs[COLO_MAX_NPCS],
    uint8_t npc_flags[COLO_ARENA_WIDTH][COLO_ARENA_HEIGHT],
    const int slots[COLO_MAX_NPCS],
    int slot_count
) {
    memset(npc_flags, 0, sizeof(uint8_t) * COLO_ARENA_WIDTH * COLO_ARENA_HEIGHT);
    for (int slot_idx = 0; slot_idx < slot_count; slot_idx++) {
        int i = slots[slot_idx];
        col_forecast_local_stamp_npc(npc_flags, &npcs[i], 1);
    }
}

static int col_forecast_local_npc_blocked_ignore_player(void* user, int x, int y, int size) {
    ColoForecastMoveCtx* ctx = (ColoForecastMoveCtx*)user;
    const ColosseumState* s = ctx->state;
    if (col_static_footprint_blocked_lookup(x, y, size)) return 1;
    for (int dx = 0; dx < size; dx++) {
        for (int dy = 0; dy < size; dy++) {
            int tx = x + dx;
            int ty = y + dy;
            if (ctx->sol_clamp_active && !col_in_boss_arena(s, tx, ty)) return 1;
            int gx, gy;
            if (!col_grid_index(tx, ty, &gx, &gy)) return 1;
            if (ctx->npc_flags[gx][gy]) return 1;
        }
    }
    return 0;
}

static int col_forecast_local_npc_blocked(void* user, int x, int y, int size) {
    ColoForecastMoveCtx* ctx = (ColoForecastMoveCtx*)user;
    if (col_forecast_local_npc_blocked_ignore_player(user, x, y, size)) return 1;
    return ctx->player_x >= x && ctx->player_x < x + size &&
        ctx->player_y >= y && ctx->player_y < y + size;
}

static int col_forecast_local_has_los_to_player(
    const ColosseumState* s,
    const ColoForecastNpcLocal* npc,
    int player_x,
    int player_y
) {
    int sx = player_x;
    int sy = player_y;
    if (sx < npc->x) sx = npc->x;
    if (sx > npc->x + npc->size - 1) sx = npc->x + npc->size - 1;
    if (sy < npc->y) sy = npc->y;
    if (sy > npc->y + npc->size - 1) sy = npc->y + npc->size - 1;
    return col_tiles_have_los(s, sx, sy, player_x, player_y);
}

static int col_forecast_local_dist_to_player(
    const ColoForecastNpcLocal* npc,
    int player_x,
    int player_y
) {
    return encounter_dist_to_npc(player_x, player_y, npc->x, npc->y, npc->size);
}

static int col_forecast_local_melee_adjacent(
    const ColoForecastNpcLocal* npc,
    int player_x,
    int player_y
) {
    return entity_has_line_of_sight(
        NULL, 0, npc->x, npc->y, npc->size, player_x, player_y, 1, 1);
}

static int col_forecast_npc_threat_distance_impossible(
    const ColoForecastNpcLocal* npc,
    int player_x,
    int player_y,
    int horizon
) {
    if (npc->forecast_behavior == COLO_FORECAST_BEHAVIOR_SOL) return 1;
    int attack_reach = npc->attack_range;
    int steps_per_tick = npc->can_move ? 1 : 0;
    if (npc->forecast_behavior == COLO_FORECAST_BEHAVIOR_WARBAND) {
        attack_reach = 1;
        steps_per_tick = COLO_WARBAND_TILES_PER_TICK;
    }
    int dist = col_forecast_local_dist_to_player(npc, player_x, player_y);
    return dist > attack_reach + steps_per_tick * horizon;
}

static int col_forecast_warband_adjacent(
    const ColoForecastNpcLocal* npc,
    int player_x,
    int player_y
) {
    int dx = npc->x > player_x ? npc->x - player_x : player_x - npc->x;
    int dy = npc->y > player_y ? npc->y - player_y : player_y - npc->y;
    return dx + dy == 1;
}

static int col_forecast_move_result_matches(
    const ColoForecastMoveResult* move,
    const ColoForecastNpcLocal* npc,
    int player_x,
    int player_y
) {
    return move && move->held &&
        move->npc_x == npc->x && move->npc_y == npc->y &&
        move->land_x == player_x && move->land_y == player_y;
}

/** Mirror of the sim's warband blocked predicate (col_npc_path_blocked with
    ignore_npcs at size 1): the player tile, the static mask and the boss clamp,
    but not other NPC bodies. */
static int col_forecast_warband_target_blocked(
    const ColoForecastMoveCtx* ctx, int x, int y) {
    if (ctx->player_x == x && ctx->player_y == y) return 1;
    if (col_static_blocked(x, y)) return 1;
    return ctx->sol_clamp_active && !col_in_boss_arena(ctx->state, x, y);
}

/** Forecast mirror of col_warband_pick_target: the formation tile when free, else
    the nearest free player-adjacent tile, cardinals before diagonals. */
static void col_forecast_warband_pick_target(
    const ColoForecastMoveCtx* ctx, const ColoForecastNpcLocal* npc,
    int px, int py, int* tx, int* ty) {
    int pref = npc->formation_dir;
    int fx = px + COLO_WARBAND_FORM_OFFSET[pref][0];
    int fy = py + COLO_WARBAND_FORM_OFFSET[pref][1];
    *tx = fx;
    *ty = fy;
    if (!col_forecast_warband_target_blocked(ctx, fx, fy)) return;

    static const int ADJ[8][2] = {
        {0, 1}, {1, 0}, {-1, 0}, {0, -1},
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1} };
    int best_key = 1 << 30;
    for (int a = 0; a < 8; a++) {
        int x = px + ADJ[a][0], y = py + ADJ[a][1];
        if (col_forecast_warband_target_blocked(ctx, x, y)) continue;
        int dx = npc->x > x ? npc->x - x : x - npc->x;
        int dy = npc->y > y ? npc->y - y : y - npc->y;
        int cheb = dx > dy ? dx : dy;
        int key = (a >= 4 ? 4096 : 0) + cheb * 16 + a;
        if (key < best_key) { best_key = key; *tx = x; *ty = y; }
    }
}

static ColoForecastMoveResult col_forecast_local_move_npc(
    const ColosseumState* s,
    ColoForecastNpcLocal npcs[COLO_MAX_NPCS],
    uint8_t npc_flags[COLO_ARENA_WIDTH][COLO_ARENA_HEIGHT],
    int idx,
    int player_x,
    int player_y,
    int sol_clamp_active
) {
    ColoForecastMoveResult result = {0};
    ColoForecastNpcLocal* npc = &npcs[idx];
    if (npc->stun_timer > 0 || npc->frozen_ticks > 0) return result;
    if (!npc->can_move) return result;
    if (npc->forecast_behavior == COLO_FORECAST_BEHAVIOR_SOL &&
            npc->sol_immobile_ticks > 0)
        return result;

    int ranged_hold = npc->attack_range > 1 &&
        npc->forecast_behavior != COLO_FORECAST_BEHAVIOR_SOL;
    if (ranged_hold) {
        int dist = col_forecast_local_dist_to_player(npc, player_x, player_y);
        int los = 0;
        if (dist >= 1 && dist <= npc->attack_range &&
                (los = col_forecast_local_has_los_to_player(
                    s, npc, player_x, player_y))) {
            result.held = 1;
            result.dist = dist;
            result.los = los;
            result.npc_x = npc->x;
            result.npc_y = npc->y;
            result.land_x = player_x;
            result.land_y = player_y;
            return result;
        }
    }
    if (npc->attack_timer > npc->attack_speed) return result;

    ColoForecastMoveCtx move_ctx;
    move_ctx.state = s;
    move_ctx.npc_flags = npc_flags;
    move_ctx.player_x = player_x;
    move_ctx.player_y = player_y;
    move_ctx.sol_clamp_active = sol_clamp_active;

    col_forecast_local_stamp_npc(npc_flags, npc, 0);
    int tx = player_x;
    int ty = player_y;
    EncounterNpcStepPolicy step_policy = npc->attack_range > 1
        ? ENCOUNTER_NPC_STEP_OSRS_AGGRO_TARGET
        : ENCOUNTER_NPC_STEP_OSRS_AGGRO_STOP_AT_MELEE;
    encounter_npc_blocked_fn blocked = col_forecast_local_npc_blocked_ignore_player;
    int steps = 1;
    uint32_t rng = s->rng_state;
    if (npc->forecast_behavior == COLO_FORECAST_BEHAVIOR_WARBAND) {
        col_forecast_warband_pick_target(&move_ctx, npc, player_x, player_y, &tx, &ty);
        step_policy = ENCOUNTER_NPC_STEP_TRAVEL_TARGET;
        blocked = col_forecast_local_npc_blocked;
        steps = COLO_WARBAND_TILES_PER_TICK;
    }
    if (npc->forecast_behavior == COLO_FORECAST_BEHAVIOR_SOL) {
        step_policy = ENCOUNTER_NPC_STEP_STOP_AT_MELEE;
        blocked = col_forecast_local_npc_blocked;
        steps = 1;
    }

    for (int step = 0; step < steps; step++) {
        if (!encounter_npc_step_toward_policy(
                &npc->x, &npc->y, tx, ty, npc->size, 1,
                step_policy, blocked, &move_ctx, NULL, &rng)) {
            break;
        }
    }

    col_forecast_local_stamp_npc(npc_flags, npc, 1);
    return result;
}

static int col_forecast_warband_phase(const ColosseumState* s, int tick_idx) {
    if (s->warband_cycle_anchor < 0) return -1;
    int phase = (s->tick + 1 + tick_idx - s->warband_cycle_anchor) %
        COLO_WARBAND_CYCLE_TICKS;
    if (phase < 0) phase += COLO_WARBAND_CYCLE_TICKS;
    return phase;
}

typedef struct {
    int warband_phase[COLO_STEP_OUT_FORECAST_HORIZON];
    int multi_manticore_possible;
    int sol_clamp_active;
} ColoForecastPrecomp;

static ColoForecastPrecomp col_forecast_precompute(
    const ColosseumState* s,
    ColoForecastNpcLocal base_npcs[COLO_MAX_NPCS],
    const int slots[COLO_MAX_NPCS],
    int slot_count
) {
    ColoForecastPrecomp pre;
    for (int tick_idx = 0; tick_idx < COLO_STEP_OUT_FORECAST_HORIZON; tick_idx++)
        pre.warband_phase[tick_idx] = col_forecast_warband_phase(s, tick_idx);
    pre.sol_clamp_active = col_sol_clamp_active(s);
    int manticore_count = 0;
    for (int slot_idx = 0; slot_idx < slot_count; slot_idx++) {
        int i = slots[slot_idx];
        const ColoForecastNpcLocal* npc = &base_npcs[i];
        if (npc->forecast_behavior == COLO_FORECAST_BEHAVIOR_MANTICORE)
            manticore_count++;
    }
    pre.multi_manticore_possible = manticore_count >= 2 ? 1 : 0;
    return pre;
}

static void col_forecast_manticore_delay_ready_peers(
    const ColosseumState* s,
    ColoForecastNpcLocal npcs[COLO_MAX_NPCS],
    const int slots[COLO_MAX_NPCS],
    int slot_count,
    int idx,
    int player_x,
    int player_y
) {
    for (int slot_idx = 0; slot_idx < slot_count; slot_idx++) {
        int i = slots[slot_idx];
        if (i == idx) continue;
        ColoForecastNpcLocal* other = &npcs[i];
        if (other->forecast_behavior != COLO_FORECAST_BEHAVIOR_MANTICORE)
            continue;
        if (other->attack_timer > 1) continue;
        if (other->cycle_step > 0) continue;
        if (other->stun_timer > 0 || other->frozen_ticks > 0) continue;
        int dist = col_forecast_local_dist_to_player(
            other, player_x, player_y);
        if (dist < 1 || dist > other->attack_range) continue;
        if (!col_forecast_local_has_los_to_player(
                s, other, player_x, player_y)) continue;
        other->attack_timer = COLO_MANTICORE_STAGGER_TICKS + 1;
    }
}

static void col_forecast_local_copy_slot_set(
    ColoForecastNpcLocal dst[COLO_MAX_NPCS],
    const ColoForecastNpcLocal src[COLO_MAX_NPCS],
    const int slots[COLO_MAX_NPCS],
    int slot_count
) {
    for (int slot_idx = 0; slot_idx < slot_count; slot_idx++) {
        int i = slots[slot_idx];
        dst[i] = src[i];
    }
}

typedef struct {
    int pillar;
    int tier;
    ColoSolarflareOrb orb;
} ColoForecastSolarflare;

static ColoForecastSolarflare col_forecast_solarflare_precompute(const ColosseumState* s) {
    ColoForecastSolarflare pre = { .pillar = -1, .tier = 0, .orb = {0} };
    if (!s->solarflare.active) return pre;
    int tier = col_mod_tier(s, COLO_MOD_SOLARFLARE);
    if (tier == 0) return pre;
    int best_pillar = -1;
    int best_distance = 1 << 30;
    for (int p = 0; p < COLO_NUM_PILLARS; p++) {
        int x, y;
        col_solarflare_tile(s, p, s->solarflare.step, &x, &y);
        int dx = abs(x - s->player.x);
        int dy = abs(y - s->player.y);
        int distance = dx > dy ? dx : dy;
        if (distance < best_distance) {
            best_distance = distance;
            best_pillar = p;
        }
    }
    pre.pillar = best_pillar;
    pre.tier = tier;
    pre.orb = s->solarflare;
    return pre;
}

static int col_forecast_solarflare_contact_tick(
    const ColosseumState* s,
    const ColoForecastSolarflare* pre,
    int land_x,
    int land_y,
    int horizon
) {
    if (pre->pillar < 0) return -1;
    ColoSolarflareOrb orb = pre->orb;
    for (int tick_idx = 0; tick_idx < horizon; tick_idx++) {
        col_solarflare_advance_orb(&orb, pre->tier);
        int x, y;
        col_solarflare_tile(s, pre->pillar, orb.step, &x, &y);
        int player_x = tick_idx == 0 ? s->player.x : land_x;
        int player_y = tick_idx == 0 ? s->player.y : land_y;
        if (x == player_x && y == player_y) return tick_idx;
    }
    return -1;
}

