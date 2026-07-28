/**
 * @file probe_colo_warband_move_parity.c
 * @brief Diff the sim warband mover against the step-out forecast warband mover on a
 *        single tick, calling both real implementations on the same state and
 *        bucketing every divergence by the gate that produced it.
 *
 * BUILD: cc -std=c11 -O2 -I. -o /tmp/probe_wb ocean/osrs/tests/probe_colo_warband_move_parity.c -lm
 * RUN:   /tmp/probe_wb <policy>   policy: idle | random
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ocean/osrs/encounters/encounter_colosseum.h"

static ColosseumContext g_ctx;
static ColosseumState g_state;
static ColosseumState g_snapshot;
static ColosseumState g_sim;
static ColoForecastNpcLocal g_locals[COLO_MAX_NPCS];
static uint8_t g_npc_flags[COLO_ARENA_WIDTH][COLO_ARENA_HEIGHT];
static int g_slots[COLO_MAX_NPCS];
static float g_obs[COLO_NUM_OBS];

enum {
    BUCKET_TARGET_DIFFERS,
    BUCKET_SIM_BFS_NOT_FOUND,
    BUCKET_SIM_BFS_ZERO_STEP,
    BUCKET_FC_ATTACK_TIMER_GATE,
    BUCKET_STEP_COUNT_DIFFERS,
    BUCKET_PATH_DIFFERS,
    BUCKET_UNCLASSIFIED,
    BUCKET_COUNT,
};
static const char* BUCKET_NAME[BUCKET_COUNT] = {
    "target differs (pick_target fallback)",
    "same target, sim BFS not found",
    "same target, sim BFS found but zero step",
    "forecast attack_timer > attack_speed gate",
    "same target+path, step count differs",
    "same target, different path taken",
    "unclassified",
};

static long g_samples;
static long g_dest_agree;
static long g_moved_agree;
static long g_fc_moved_sim_not;
static long g_sim_moved_fc_not;
static long g_bucket_fc_moved[BUCKET_COUNT];
static long g_bucket_sim_moved[BUCKET_COUNT];
static long g_traces_printed;

typedef struct {
    int target_x, target_y;
    int formation_blocked;
    int bfs_found[COLO_WARBAND_TILES_PER_TICK];
    int bfs_dx[COLO_WARBAND_TILES_PER_TICK];
    int bfs_dy[COLO_WARBAND_TILES_PER_TICK];
    int probe_steps;
} SimTrace;

/** Re-derive the sim mover's internal decisions read-only, without mutating state. */
static SimTrace sim_trace(ColosseumState* s, int idx) {
    SimTrace t;
    memset(&t, 0, sizeof(t));
    ColoNPC* npc = &s->npcs[idx];
    ColoNpcPathCtx pc = col_npc_path_ctx_begin(s, 1);
    pc.ignore_npcs = 1;
    int pref = colo_npc_warband(npc)->formation_dir;
    int fx = s->player.x + COLO_WARBAND_FORM_OFFSET[pref][0];
    int fy = s->player.y + COLO_WARBAND_FORM_OFFSET[pref][1];
    t.formation_blocked = col_npc_path_blocked(&pc, fx, fy) ? 1 : 0;
    col_warband_pick_target(s, &pc, npc, &t.target_x, &t.target_y);

    int x = npc->x, y = npc->y;
    for (int k = 0; k < COLO_WARBAND_TILES_PER_TICK; k++) {
        t.bfs_found[k] = -1;
        if (x == t.target_x && y == t.target_y) break;
        int tx = t.target_x, ty = t.target_y;
        if (tx < COLO_ARENA_MIN_X) tx = COLO_ARENA_MIN_X;
        if (tx > COLO_ARENA_MAX_X) tx = COLO_ARENA_MAX_X;
        if (ty < COLO_ARENA_MIN_Y) ty = COLO_ARENA_MIN_Y;
        if (ty > COLO_ARENA_MAX_Y) ty = COLO_ARENA_MAX_Y;
        PathResult pr = pathfind_step_arena(NULL, 0, x, y, tx, ty,
            col_npc_path_blocked, &pc,
            COLO_ARENA_MIN_X, COLO_ARENA_MIN_Y, COLO_ARENA_WIDTH, COLO_ARENA_HEIGHT);
        t.bfs_found[k] = pr.found ? 1 : 0;
        t.bfs_dx[k] = pr.next_dx;
        t.bfs_dy[k] = pr.next_dy;
        if (!col_npc_path_step_with_ctx(&pc, &x, &y, t.target_x, t.target_y)) break;
        t.probe_steps++;
    }
    return t;
}

static void analyze_tick(int tick, int policy_is_idle) {
    g_snapshot = g_state;
    ColosseumState* s = &g_snapshot;

    int slot_count = col_collect_step_out_forecast_slots(s, g_slots);
    if (slot_count == 0) return;
    col_forecast_local_copy_npc_slots(s, g_locals, g_slots, slot_count);
    col_forecast_local_rebuild_npc_flags(g_locals, g_npc_flags, g_slots, slot_count);

    ColoStepOutForecastAction action;
    memset(&action, 0, sizeof(action));
    col_step_out_forecast_set_action_landing_ctx(s, 0, &action);
    if (!action.valid) return;
    ColoForecastPrecomp pre = col_forecast_precompute(s, g_locals, g_slots, slot_count);

    int start_x[COLO_MAX_NPCS], start_y[COLO_MAX_NPCS];
    int base_attack_timer[COLO_MAX_NPCS], base_attack_speed[COLO_MAX_NPCS];
    for (int k = 0; k < slot_count; k++) {
        int i = g_slots[k];
        start_x[i] = g_locals[i].x;
        start_y[i] = g_locals[i].y;
        base_attack_timer[i] = g_locals[i].attack_timer;
        base_attack_speed[i] = g_locals[i].attack_speed;
    }

    /* forecast tick 0, movement only (the attack pass never displaces an NPC) */
    for (int k = 0; k < slot_count; k++) {
        int i = g_slots[k];
        ColoForecastNpcLocal* npc = &g_locals[i];
        if (npc->stun_timer > 0) npc->stun_timer--;
        if (npc->frozen_ticks > 0) npc->frozen_ticks--;
        if (npc->forecast_behavior == COLO_FORECAST_BEHAVIOR_SOL) {
            if (npc->sol_immobile_ticks > 0) npc->sol_immobile_ticks--;
            if (npc->sol_attack_delay > 0) npc->sol_attack_delay--;
        }
        col_forecast_local_move_npc(
            s, g_locals, g_npc_flags, i, action.land_x, action.land_y,
            pre.sol_clamp_active);
    }

    /* sim tick, movement only, same pre-move decrement as encounter_colosseum_combat.inc */
    g_sim = g_snapshot;
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        ColoNPC* npc = &g_sim.npcs[i];
        if (!npc->active) continue;
        if (npc->death_ticks > 0) continue;
        if (npc->stun_timer > 0) npc->stun_timer--;
        if (npc->frozen_ticks > 0) npc->frozen_ticks--;
        if (col_type_is_hazard_entity(npc->type)) continue;
        col_npc_move_ctx(&g_sim, &g_ctx, i);
    }

    for (int k = 0; k < slot_count; k++) {
        int i = g_slots[k];
        if (!col_type_is_warbander(g_snapshot.npcs[i].type)) continue;
        int sx = start_x[i], sy = start_y[i];
        int fx = g_locals[i].x, fy = g_locals[i].y;
        int mx = g_sim.npcs[i].x, my = g_sim.npcs[i].y;
        int fc_moved = (fx != sx || fy != sy);
        int sim_moved = (mx != sx || my != sy);

        g_samples++;
        if (fx == mx && fy == my) g_dest_agree++;
        if (fc_moved == sim_moved) g_moved_agree++;
        if (fc_moved == sim_moved && fx == mx && fy == my) continue;

        SimTrace st = sim_trace(&g_snapshot, i);
        int form_dir = g_snapshot.npcs[i].type_state.warband.formation_dir;
        int ftx = action.land_x + COLO_WARBAND_FORM_OFFSET[form_dir][0];
        int fty = action.land_y + COLO_WARBAND_FORM_OFFSET[form_dir][1];

        int bucket;
        if (st.target_x != ftx || st.target_y != fty) {
            bucket = BUCKET_TARGET_DIFFERS;
        } else if (st.bfs_found[0] == 0) {
            bucket = BUCKET_SIM_BFS_NOT_FOUND;
        } else if (st.bfs_found[0] == 1 && st.bfs_dx[0] == 0 && st.bfs_dy[0] == 0) {
            bucket = BUCKET_SIM_BFS_ZERO_STEP;
        } else if (base_attack_timer[i] > base_attack_speed[i] && !fc_moved) {
            bucket = BUCKET_FC_ATTACK_TIMER_GATE;
        } else {
            int sim_cheb = 0, fc_cheb = 0;
            int a = mx - sx, b = my - sy;
            sim_cheb = (a < 0 ? -a : a) > (b < 0 ? -b : b) ? (a < 0 ? -a : a) : (b < 0 ? -b : b);
            a = fx - sx; b = fy - sy;
            fc_cheb = (a < 0 ? -a : a) > (b < 0 ? -b : b) ? (a < 0 ? -a : a) : (b < 0 ? -b : b);
            bucket = sim_cheb != fc_cheb ? BUCKET_STEP_COUNT_DIFFERS : BUCKET_PATH_DIFFERS;
        }

        if (fc_moved && !sim_moved) { g_fc_moved_sim_not++; g_bucket_fc_moved[bucket]++; }
        else if (sim_moved && !fc_moved) { g_sim_moved_fc_not++; g_bucket_sim_moved[bucket]++; }

        if (fc_moved && !sim_moved && g_traces_printed < 12) {
            g_traces_printed++;
            printf("--- TRACE tick=%d npc=%d wave=%d type=%d form_dir=%d\n",
                tick, i, g_snapshot.wave, (int)g_snapshot.npcs[i].type, form_dir);
            printf("    player=(%d,%d) land=(%d,%d) npc_start=(%d,%d)\n",
                g_snapshot.player.x, g_snapshot.player.y,
                action.land_x, action.land_y, sx, sy);
            printf("    sim_target=(%d,%d) fc_target=(%d,%d) formation_blocked=%d\n",
                st.target_x, st.target_y, ftx, fty, st.formation_blocked);
            printf("    sim_end=(%d,%d) fc_end=(%d,%d)\n", mx, my, fx, fy);
            printf("    sim_probe_steps=%d bfs[0] found=%d d=(%d,%d) bfs[1] found=%d d=(%d,%d)\n",
                st.probe_steps, st.bfs_found[0], st.bfs_dx[0], st.bfs_dy[0],
                st.bfs_found[1], st.bfs_dx[1], st.bfs_dy[1]);
            printf("    stun=%d frozen=%d attack_timer=%d attack_speed=%d bucket=%s\n",
                g_snapshot.npcs[i].stun_timer, g_snapshot.npcs[i].frozen_ticks,
                base_attack_timer[i], base_attack_speed[i], BUCKET_NAME[bucket]);
            fflush(stdout);
        }
    }
    (void)policy_is_idle;
}

static int first_walkable_move(const ColosseumState* s, uint32_t* rng) {
    int cands[ENCOUNTER_MOVE_ACTIONS], n = 0;
    for (int a = 1; a < ENCOUNTER_MOVE_ACTIONS; a++) {
        int nx = s->player.x + ENCOUNTER_MOVE_TARGET_DX[a];
        int ny = s->player.y + ENCOUNTER_MOVE_TARGET_DY[a];
        if (col_player_walkable((void*)s, nx, ny)) cands[n++] = a;
    }
    if (n == 0) return 0;
    *rng = *rng * 1664525u + 1013904223u;
    return cands[(*rng >> 16) % (uint32_t)n];
}

static int head_dim(int head) {
    if (head == COLO_HEAD_PRIMARY) return COLO_PRIMARY_DIM;
    if (head == COLO_HEAD_PRAYER || head == COLO_HEAD_OFFENSIVE) return COLO_OVERHEAD_DIM;
    if (head >= COLO_HEAD_EQUIP_BASE && head <= COLO_HEAD_DRINK) return COLO_INV_CLICK_DIM;
    if (head == COLO_HEAD_SPEC) return COLO_SPEC_DIM;
    if (head == COLO_HEAD_MODIFIER_SELECT) return COLO_MODIFIER_DRAFT_OPTIONS + 1;
    if (head == COLO_HEAD_GRAPPLE_PARRY) return 6;
    if (head == COLO_HEAD_SPELL) return COLO_SPELL_DIM;
    return 1;
}

int main(int argc, char** argv) {
    const char* policy = argc > 1 ? argv[1] : "random";
    int policy_is_idle = strcmp(policy, "idle") == 0;
    int policy_is_uniform = strcmp(policy, "uniform") == 0;
    uint32_t rng = 0xC01055EUL;

    col_init_context_typed(&g_ctx);
    g_ctx.config.start_wave = 0;
    g_ctx.config.loadout_profile_mode = COLO_LOADOUT_PROFILE_MODE_MIXED;
    g_ctx.config.beginner_loadout_fraction = 0.5f;
    g_ctx.config.step_out_forecast_obs_enabled = 1;

    int episodes = 0;
    for (int ep = 0; ep < 12; ep++) {
        memset(&g_state, 0, sizeof(g_state));
        col_reset_ctx((EncounterState*)&g_state, (EncounterContext*)&g_ctx,
            0x1234u + (uint32_t)ep * 7919u);
        episodes++;
        for (int tick = 0; tick < 400 && !g_state.episode_over; tick++) {
            col_write_obs_ctx(
                (EncounterState*)&g_state, (EncounterContext*)&g_ctx, g_obs);
            int actions[COLO_NUM_ACTION_HEADS];
            for (int h = 0; h < COLO_NUM_ACTION_HEADS; h++) actions[h] = 0;
            if (g_state.modifiers.draft_pending) {
                int opt = 0;
                for (int o = 0; o < COLO_MODIFIER_DRAFT_OPTIONS; o++)
                    if (g_state.modifiers.draft_options[o] >= 0) { opt = o; break; }
                actions[COLO_HEAD_MODIFIER_SELECT] = opt + 1;
            } else {
                analyze_tick(tick, policy_is_idle);
                if (policy_is_uniform) {
                    for (int h = 0; h < COLO_NUM_ACTION_HEADS; h++) {
                        int dim = head_dim(h);
                        if (dim <= 1) continue;
                        rng = rng * 1664525u + 1013904223u;
                        actions[h] = (int)((rng >> 16) % (uint32_t)dim);
                    }
                } else if (!policy_is_idle) {
                    rng = rng * 1664525u + 1013904223u;
                    if ((rng >> 16) % 3u != 0u)
                        actions[COLO_HEAD_PRIMARY] =
                            first_walkable_move(&g_state, &rng);
                }
            }
            col_step_ctx((EncounterState*)&g_state, (EncounterContext*)&g_ctx, actions);
        }
    }

    printf("\n=== policy=%s episodes=%d ===\n", policy, episodes);
    printf("samples=%ld  destination agree=%.1f%%  moved-or-not agree=%.1f%%\n",
        g_samples,
        g_samples ? 100.0 * (double)g_dest_agree / (double)g_samples : 0.0,
        g_samples ? 100.0 * (double)g_moved_agree / (double)g_samples : 0.0);
    printf("forecast moved / sim did not: %ld\n", g_fc_moved_sim_not);
    printf("sim moved / forecast did not: %ld\n", g_sim_moved_fc_not);
    printf("\nbucket breakdown (fc_moved_sim_not | sim_moved_fc_not):\n");
    for (int b = 0; b < BUCKET_COUNT; b++)
        printf("  %-44s %5ld | %5ld\n",
            BUCKET_NAME[b], g_bucket_fc_moved[b], g_bucket_sim_moved[b]);
    return 0;
}
