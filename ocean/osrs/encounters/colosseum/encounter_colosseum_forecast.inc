#line 76 "encounter_colosseum.h"
/**
 * @file encounter_colosseum_forecast.inc
 * @brief Read-only step-out prayer forecast for candidate player movement.
 */

#define COLO_STYLE_MASK_MELEE  1
#define COLO_STYLE_MASK_RANGED 2
#define COLO_STYLE_MASK_MAGIC  4

typedef struct {
    const ColosseumState* state;
    uint8_t (*npc_flags)[COLO_ARENA_HEIGHT];
    int player_x;
    int player_y;
} ColoForecastMoveCtx;

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

static void col_step_out_forecast_record_style_mask(
    ColoStepOutForecastAction* action,
    int tick_idx,
    ColoNpcType type,
    int style_mask,
    int max_hit
) {
    if (tick_idx < 0 || tick_idx >= COLO_STEP_OUT_FORECAST_HORIZON) return;
    ColoStepOutForecastTick* tick = &action->ticks[tick_idx];
    int styles = 0;
    if (style_mask & COLO_STYLE_MASK_MELEE) {
        tick->melee_count++;
        if (max_hit > tick->max_hit) tick->max_hit = max_hit;
        styles++;
    }
    if (style_mask & COLO_STYLE_MASK_RANGED) {
        tick->ranged_count++;
        if (max_hit > tick->max_hit) tick->max_hit = max_hit;
        styles++;
    }
    if (style_mask & COLO_STYLE_MASK_MAGIC) {
        tick->magic_count++;
        if (max_hit > tick->max_hit) tick->max_hit = max_hit;
        styles++;
    }
    if (styles >= 2 || ((tick->melee_count > 0) +
            (tick->ranged_count > 0) + (tick->magic_count > 0)) >= 2) {
        action->same_tick_mixed_style_conflict = 1;
    }
    if ((style_mask & COLO_STYLE_MASK_MELEE) && type == COLO_MANTICORE)
        action->melee_fallback_exposure = 1;
}

static void col_step_out_forecast_record_style(
    ColoStepOutForecastAction* action,
    int tick_idx,
    ColoNpcType type,
    AttackStyle style
) {
    int style_mask = col_forecast_style_mask(style);
    if (style_mask == 0) return;
    col_step_out_forecast_record_style_mask(
        action, tick_idx, type, style_mask,
        col_forecast_style_max_hit(type, style));
}

static int col_step_out_forecast_tick_style_mask(const ColoStepOutForecastTick* tick) {
    int mask = 0;
    if (tick->melee_count > 0) mask |= COLO_STYLE_MASK_MELEE;
    if (tick->ranged_count > 0) mask |= COLO_STYLE_MASK_RANGED;
    if (tick->magic_count > 0) mask |= COLO_STYLE_MASK_MAGIC;
    return mask;
}

static int col_step_out_forecast_tick_has_event(const ColoStepOutForecastTick* tick) {
    return tick->melee_count > 0 ||
        tick->ranged_count > 0 ||
        tick->magic_count > 0;
}

static void col_step_out_forecast_finalize_action(ColoStepOutForecastAction* action) {
    for (int t = 0; t < COLO_STEP_OUT_FORECAST_HORIZON - 1; t++) {
        int ranged_then_magic = action->ticks[t].ranged_count > 0 &&
            action->ticks[t + 1].magic_count > 0;
        int magic_then_ranged = action->ticks[t].magic_count > 0 &&
            action->ticks[t + 1].ranged_count > 0;
        if (ranged_then_magic || magic_then_ranged)
            action->ranged_magic_offtick_opportunity = 1;
    }
}

static int col_step_out_forecast_action_valid(const ColosseumState* s, int action_idx) {
    if (action_idx == 0) return 1;
    if (s->modifiers.draft_pending) return 0;
    int nx = s->player.x + ENCOUNTER_MOVE_TARGET_DX[action_idx];
    int ny = s->player.y + ENCOUNTER_MOVE_TARGET_DY[action_idx];
    return col_player_walkable((void*)s, nx, ny);
}

static void col_step_out_forecast_action_landing_ctx(
    const ColosseumState* s,
    int action_idx,
    Player* moved,
    int* valid
) {
    *moved = s->player;
    *valid = col_step_out_forecast_action_valid(s, action_idx);
    if (action_idx == 0) return;
    encounter_move_to_target(
        moved,
        ENCOUNTER_MOVE_TARGET_DX[action_idx],
        ENCOUNTER_MOVE_TARGET_DY[action_idx],
        col_player_walkable,
        (void*)s);
}

static Player col_step_out_forecast_set_action_landing_ctx(
    const ColosseumState* s,
    int action_idx,
    ColoStepOutForecastAction* action
) {
    Player moved;
    int valid = 0;
    col_step_out_forecast_action_landing_ctx(s, action_idx, &moved, &valid);
    action->valid = valid;
    action->land_x = moved.x;
    action->land_y = moved.y;
    return moved;
}

static int col_step_out_forecast_duplicate_landing(
    const ColoStepOutForecast* forecast,
    int action_idx
) {
    const ColoStepOutForecastAction* action = &forecast->actions[action_idx];
    for (int prior_idx = 0; prior_idx < action_idx; prior_idx++) {
        const ColoStepOutForecastAction* prior = &forecast->actions[prior_idx];
        if (prior->valid &&
                prior->land_x == action->land_x &&
                prior->land_y == action->land_y) {
            return prior_idx;
        }
    }
    return -1;
}

static int col_step_out_forecast_prepare_unique_action_ctx(
    const ColosseumState* s,
    ColoStepOutForecast* out,
    int action_idx,
    int forecast_slot_count
) {
    ColoStepOutForecastAction* action = &out->actions[action_idx];
    Player moved = col_step_out_forecast_set_action_landing_ctx(
        s, action_idx, action);
    if (!action->valid || forecast_slot_count == 0) return 0;

    int duplicate_idx = col_step_out_forecast_duplicate_landing(out, action_idx);
    if (duplicate_idx >= 0) {
        int valid = action->valid;
        *action = out->actions[duplicate_idx];
        action->valid = valid;
        action->land_x = moved.x;
        action->land_y = moved.y;
        return 0;
    }
    return 1;
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

static void col_forecast_local_copy_npcs(
    const ColosseumState* s,
    ColoForecastNpcLocal out[COLO_MAX_NPCS]
) {
    memset(out, 0, sizeof(ColoForecastNpcLocal) * COLO_MAX_NPCS);
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        const ColoNPC* src = &s->npcs[i];
        ColoForecastNpcLocal* dst = &out[i];
        dst->active = src->active;
        dst->type = src->type;
        dst->x = src->x;
        dst->y = src->y;
        dst->size = src->size;
        dst->hp = src->hp;
        dst->attack_timer = src->attack_timer;
        dst->stun_timer = src->stun_timer;
        dst->frozen_ticks = src->frozen_ticks;
        dst->death_ticks = src->death_ticks;
        if (!src->active) continue;
        if (src->type == COLO_MANTICORE) {
            dst->cycle_step = src->type_state.manticore.cycle_step;
            for (int orb = 0; orb < 3; orb++)
                dst->orb_style[orb] = src->type_state.manticore.orb_style[orb];
        } else if (col_type_is_warbander(src->type)) {
            dst->formation_dir = src->type_state.warband.formation_dir;
        } else if (src->type == COLO_JAVELIN_COLOSSUS) {
            dst->javelin_attack_count = src->type_state.javelin.attack_count;
        } else if (src->type == COLO_SOL_HEREDIT) {
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
    uint8_t npc_flags[COLO_ARENA_WIDTH][COLO_ARENA_HEIGHT]
) {
    memset(npc_flags, 0, sizeof(uint8_t) * COLO_ARENA_WIDTH * COLO_ARENA_HEIGHT);
    for (int i = 0; i < COLO_MAX_NPCS; i++)
        col_forecast_local_stamp_npc(npc_flags, &npcs[i], 1);
}

static int col_forecast_local_npc_blocked(void* user, int x, int y, int size) {
    ColoForecastMoveCtx* ctx = (ColoForecastMoveCtx*)user;
    const ColosseumState* s = ctx->state;
    for (int dx = 0; dx < size; dx++) {
        for (int dy = 0; dy < size; dy++) {
            int tx = x + dx;
            int ty = y + dy;
            if (col_static_blocked(tx, ty)) return 1;
            if (col_sol_clamp_active(s) && !col_in_boss_arena(s, tx, ty)) return 1;
            int gx, gy;
            if (!col_grid_index(tx, ty, &gx, &gy)) return 1;
            if (ctx->npc_flags[gx][gy]) return 1;
        }
    }
    return 0;
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

static int col_forecast_warband_adjacent(
    const ColoForecastNpcLocal* npc,
    int player_x,
    int player_y
) {
    int dx = npc->x > player_x ? npc->x - player_x : player_x - npc->x;
    int dy = npc->y > player_y ? npc->y - player_y : player_y - npc->y;
    return dx + dy == 1;
}

/**
 * Forecast movement uses the same local collision-grid shape as Inferno mode 3.
 * Warband and Sol use greedy local stepping instead of the live BFS routefinder.
 */
static void col_forecast_local_move_npc(
    const ColosseumState* s,
    ColoForecastNpcLocal npcs[COLO_MAX_NPCS],
    uint8_t npc_flags[COLO_ARENA_WIDTH][COLO_ARENA_HEIGHT],
    int idx,
    ColoStepOutForecastAction* action
) {
    (void)npcs;
    ColoForecastNpcLocal* npc = &npcs[idx];
    if (!npc->active || npc->death_ticks > 0 || npc->hp <= 0) return;
    if (npc->stun_timer > 0 || npc->frozen_ticks > 0) return;
    if (col_type_is_hazard_entity(npc->type)) return;
    const ColoNpcStats* stats = &COLO_NPC_STATS[npc->type];
    if (!stats->can_move) return;
    if (npc->type == COLO_SOL_HEREDIT && npc->sol_immobile_ticks > 0) return;

    int ranged_hold = stats->attack_range > 1 && npc->type != COLO_SOL_HEREDIT;
    if (ranged_hold) {
        int dist = col_forecast_local_dist_to_player(npc, action->land_x, action->land_y);
        if (dist >= 1 && dist <= stats->attack_range &&
                col_forecast_local_has_los_to_player(
                    s, npc, action->land_x, action->land_y)) {
            return;
        }
    }

    col_forecast_local_stamp_npc(npc_flags, npc, 0);

    int tx = action->land_x;
    int ty = action->land_y;
    int stop_at_melee = stats->attack_range == 1 ? 1 : 0;
    int steps = 1;
    if (col_type_is_warbander(npc->type)) {
        tx = action->land_x + COLO_WARBAND_FORM_OFFSET[npc->formation_dir][0];
        ty = action->land_y + COLO_WARBAND_FORM_OFFSET[npc->formation_dir][1];
        stop_at_melee = 0;
        steps = COLO_WARBAND_TILES_PER_TICK;
    }
    if (npc->type == COLO_SOL_HEREDIT) {
        stop_at_melee = 1;
        steps = 1;
    }

    ColoForecastMoveCtx move_ctx;
    move_ctx.state = s;
    move_ctx.npc_flags = npc_flags;
    move_ctx.player_x = action->land_x;
    move_ctx.player_y = action->land_y;
    for (int step = 0; step < steps; step++) {
        if (!encounter_npc_step_toward(
                &npc->x, &npc->y, tx, ty, npc->size, 1,
                stop_at_melee, col_forecast_local_npc_blocked, &move_ctx)) {
            break;
        }
    }

    col_forecast_local_stamp_npc(npc_flags, npc, 1);
}

static int col_forecast_warband_phase(const ColosseumState* s, int tick_idx) {
    if (s->warband_cycle_anchor < 0) return -1;
    int phase = (s->tick + 1 + tick_idx - s->warband_cycle_anchor) %
        COLO_WARBAND_CYCLE_TICKS;
    if (phase < 0) phase += COLO_WARBAND_CYCLE_TICKS;
    return phase;
}

static int col_forecast_any_other_manticore_mid_barrage(
    ColoForecastNpcLocal npcs[COLO_MAX_NPCS],
    int idx
) {
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        if (i == idx) continue;
        ColoForecastNpcLocal* other = &npcs[i];
        if (!other->active || other->death_ticks > 0 || other->hp <= 0) continue;
        if (other->type != COLO_MANTICORE) continue;
        if (other->cycle_step >= 0) return 1;
    }
    return 0;
}

static void col_forecast_local_fire_manticore_orb(
    ColoForecastNpcLocal* npc,
    ColoStepOutForecastAction* action,
    int tick_idx
) {
    if (npc->cycle_step < 0 || npc->cycle_step >= 3) return;
    AttackStyle style = npc->orb_style[npc->cycle_step];
    col_step_out_forecast_record_style(action, tick_idx, npc->type, style);
    npc->cycle_step++;
    if (npc->cycle_step >= 3) {
        npc->cycle_step = -1;
        npc->orb_style[0] = ATTACK_STYLE_NONE;
        npc->orb_style[1] = ATTACK_STYLE_NONE;
        npc->orb_style[2] = ATTACK_STYLE_NONE;
        npc->attack_timer = COLO_NPC_STATS[npc->type].attack_speed - 2;
    }
}

static void col_forecast_local_attack_manticore(
    const ColosseumState* s,
    ColoForecastNpcLocal npcs[COLO_MAX_NPCS],
    int idx,
    ColoStepOutForecastAction* action,
    int tick_idx
) {
    ColoForecastNpcLocal* npc = &npcs[idx];
    if (npc->cycle_step >= 0) {
        if (npc->stun_timer > 0 || npc->frozen_ticks > 0) return;
        col_forecast_local_fire_manticore_orb(npc, action, tick_idx);
        return;
    }

    if (npc->attack_timer > 0) npc->attack_timer--;
    if (npc->stun_timer > 0 || npc->frozen_ticks > 0) return;

    const ColoNpcStats* stats = &COLO_NPC_STATS[npc->type];
    int dist = col_forecast_local_dist_to_player(npc, action->land_x, action->land_y);
    if (dist < 1 || dist > stats->attack_range) return;
    if (!col_forecast_local_has_los_to_player(s, npc, action->land_x, action->land_y)) return;
    if (npc->orb_style[0] == ATTACK_STYLE_NONE) return;
    if (npc->attack_timer > 0) return;
    if (col_forecast_any_other_manticore_mid_barrage(npcs, idx)) {
        npc->attack_timer = COLO_MANTICORE_STAGGER_TICKS;
        return;
    }

    npc->cycle_step = 0;
    col_forecast_local_fire_manticore_orb(npc, action, tick_idx);
}

static void col_forecast_local_attack_warband(
    const ColosseumState* s,
    ColoForecastNpcLocal* npc,
    ColoStepOutForecastAction* action,
    int tick_idx
) {
    int phase = col_forecast_warband_phase(s, tick_idx);
    if (phase < 0) return;
    if (phase != col_warband_window_offset(npc->type)) return;
    if (npc->stun_timer > 0 || npc->frozen_ticks > 0) return;
    if (!col_forecast_warband_adjacent(npc, action->land_x, action->land_y)) return;
    col_step_out_forecast_record_style(
        action, tick_idx, npc->type, COLO_NPC_STATS[npc->type].default_style);
}

static void col_forecast_local_attack_sol(
    const ColosseumState* s,
    ColoForecastNpcLocal* npc,
    ColoStepOutForecastAction* action,
    int tick_idx
) {
    if (npc->sol_immobile_ticks > 0 || npc->sol_attack_delay > 0) return;
    int dist = col_forecast_local_dist_to_player(npc, action->land_x, action->land_y);
    if (dist == 1) {
        col_step_out_forecast_record_style(
            action, tick_idx, npc->type, ATTACK_STYLE_MELEE);
        npc->sol_immobile_ticks = COLO_SOL_SPEAR_FREEZE;
        npc->sol_attack_delay = COLO_SOL_SPEAR_DELAY;
        return;
    }
    if (dist > 1 && dist <= 15 &&
            col_forecast_local_has_los_to_player(s, npc, action->land_x, action->land_y)) {
        col_step_out_forecast_record_style(
            action, tick_idx, npc->type, ATTACK_STYLE_RANGED);
        npc->sol_immobile_ticks = COLO_SOL_SPEAR_FREEZE;
        npc->sol_attack_delay = COLO_SOL_SPEAR_DELAY;
    }
}

static void col_forecast_local_attack_standard(
    const ColosseumState* s,
    ColoForecastNpcLocal* npc,
    ColoStepOutForecastAction* action,
    int tick_idx
) {
    const ColoNpcStats* stats = &COLO_NPC_STATS[npc->type];
    if (npc->attack_timer > 0) npc->attack_timer--;
    if (npc->stun_timer > 0 || npc->frozen_ticks > 0) return;
    if (npc->attack_timer > 0) return;

    int dist = col_forecast_local_dist_to_player(npc, action->land_x, action->land_y);
    if (npc->type == COLO_MINOTAUR) {
        if (dist == 1) {
            col_step_out_forecast_record_style(
                action, tick_idx, npc->type, ATTACK_STYLE_MELEE);
        }
        npc->attack_timer = stats->attack_speed;
        return;
    }
    if (npc->type == COLO_JAGUAR_WARRIOR) {
        if (dist == 1) {
            col_step_out_forecast_record_style(
                action, tick_idx, npc->type, ATTACK_STYLE_MELEE);
            npc->attack_timer = stats->attack_speed;
        }
        return;
    }

    if (stats->attack_range > 1) {
        if (dist < 1 || dist > stats->attack_range) return;
        if (!col_forecast_local_has_los_to_player(s, npc, action->land_x, action->land_y))
            return;
    } else if (dist != 1) {
        return;
    }

    if (npc->type == COLO_JAVELIN_COLOSSUS) {
        npc->javelin_attack_count++;
        if (npc->javelin_attack_count % 5 != 0) {
            col_step_out_forecast_record_style(
                action, tick_idx, npc->type, ATTACK_STYLE_RANGED);
        }
        npc->attack_timer = stats->attack_speed;
        return;
    }

    if (npc->type == COLO_SERPENT_SHAMAN ||
            npc->type == COLO_SHOCKWAVE_COLOSSUS) {
        col_step_out_forecast_record_style(
            action, tick_idx, npc->type, stats->default_style);
        npc->attack_timer = stats->attack_speed;
    }
}

static void col_forecast_local_attack_npc(
    const ColosseumState* s,
    ColoForecastNpcLocal npcs[COLO_MAX_NPCS],
    int idx,
    ColoStepOutForecastAction* action,
    int tick_idx
) {
    ColoForecastNpcLocal* npc = &npcs[idx];
    if (!npc->active || npc->death_ticks > 0 || npc->hp <= 0) return;
    if (col_type_is_hazard_entity(npc->type)) return;

    if (col_type_is_warbander(npc->type)) {
        col_forecast_local_attack_warband(s, npc, action, tick_idx);
        return;
    }
    if (npc->type == COLO_MANTICORE) {
        col_forecast_local_attack_manticore(s, npcs, idx, action, tick_idx);
        return;
    }
    if (npc->type == COLO_SOL_HEREDIT) {
        col_forecast_local_attack_sol(s, npc, action, tick_idx);
        return;
    }
    col_forecast_local_attack_standard(s, npc, action, tick_idx);
}

static void col_step_out_forecast_readonly_tick(
    const ColosseumState* s,
    ColoForecastNpcLocal npcs[COLO_MAX_NPCS],
    uint8_t npc_flags[COLO_ARENA_WIDTH][COLO_ARENA_HEIGHT],
    ColoStepOutForecastAction* action,
    int tick_idx,
    const int slots[COLO_MAX_NPCS],
    int slot_count
) {
    for (int slot_idx = 0; slot_idx < slot_count; slot_idx++) {
        int i = slots[slot_idx];
        ColoForecastNpcLocal* npc = &npcs[i];
        if (npc->stun_timer > 0) npc->stun_timer--;
        if (npc->frozen_ticks > 0) npc->frozen_ticks--;
        if (npc->type == COLO_SOL_HEREDIT) {
            if (npc->sol_immobile_ticks > 0) npc->sol_immobile_ticks--;
            if (npc->sol_attack_delay > 0) npc->sol_attack_delay--;
        }
        col_forecast_local_move_npc(s, npcs, npc_flags, i, action);
        col_forecast_local_attack_npc(s, npcs, i, action, tick_idx);
    }
}

static void col_build_step_out_forecast_ctx(
    const ColosseumState* s,
    ColoStepOutForecast* out
) {
    memset(out, 0, sizeof(*out));
    int forecast_slots[COLO_MAX_NPCS];
    int forecast_slot_count = col_collect_step_out_forecast_slots(s, forecast_slots);

    ColoForecastNpcLocal base_npcs[COLO_MAX_NPCS];
    uint8_t base_npc_flags[COLO_ARENA_WIDTH][COLO_ARENA_HEIGHT];
    if (forecast_slot_count > 0) {
        col_forecast_local_copy_npcs(s, base_npcs);
        col_forecast_local_rebuild_npc_flags(base_npcs, base_npc_flags);
    }

    for (int action_idx = 0; action_idx < ENCOUNTER_MOVE_ACTIONS; action_idx++) {
        if (!col_step_out_forecast_prepare_unique_action_ctx(
                s, out, action_idx, forecast_slot_count)) {
            continue;
        }
        ColoStepOutForecastAction* action = &out->actions[action_idx];
        ColoForecastNpcLocal npcs[COLO_MAX_NPCS];
        uint8_t npc_flags[COLO_ARENA_WIDTH][COLO_ARENA_HEIGHT];
        memcpy(npcs, base_npcs, sizeof(base_npcs));
        memcpy(npc_flags, base_npc_flags, sizeof(base_npc_flags));
        for (int tick_idx = 0; tick_idx < COLO_STEP_OUT_FORECAST_HORIZON; tick_idx++) {
            col_step_out_forecast_readonly_tick(
                s, npcs, npc_flags, action, tick_idx,
                forecast_slots, forecast_slot_count);
        }
        col_step_out_forecast_finalize_action(action);
    }
}
