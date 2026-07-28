/**
 * For every tick where the player stands on a tile a Sol hazard will hit, ask whether
 * ANY primary action lands on a tile that is safe from every Sol hazard.
 *
 * Uses the real col_build_primary_action_forecast_ctx landings and the real
 * col_sol_*_action_hit_tick predicates. Nothing about movement or hazard shape is
 * reimplemented here.
 *
 * "escapable" = at least one valid primary action whose landing tile has hit_tick 0
 * for AOE, crystal laser and molten sand simultaneously.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ocean/osrs/encounters/encounter_colosseum.h"

static uint64_t sm(uint64_t* s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

typedef struct {
    long exposed;        /* ticks standing on a doomed tile */
    long escapable;      /* at least one fully safe landing existed */
    long lead_hist[12];  /* hit_tick when exposed */
    long lead_escapable[12];
    long min_lead_seen;
} HazardStat;

static const char* HAZ_NAME[COLO_SOL_HAZARD_SOURCE_COUNT] = {
    "aoe", "crystal_laser", "molten_sand"
};

static int hazard_hit_tick(
    const ColosseumState* s, int which, int x, int y
) {
    if (which == COLO_SOL_HAZARD_AOE)           return col_sol_aoe_action_hit_tick(s, x, y);
    if (which == COLO_SOL_HAZARD_CRYSTAL_LASER) return col_sol_laser_action_hit_tick(s, x, y);
    return col_sol_molten_action_hit_tick(s, x, y);
}

static int tile_fully_safe(const ColosseumState* s, int x, int y) {
    for (int h = 0; h < COLO_SOL_HAZARD_SOURCE_COUNT; h++)
        if (hazard_hit_tick(s, h, x, y) > 0) return 0;
    return 1;
}

int main(int argc, char** argv) {
    int episodes = argc > 1 ? atoi(argv[1]) : 400;
    HazardStat stat[COLO_SOL_HAZARD_SOURCE_COUNT];
    memset(stat, 0, sizeof(stat));
    for (int h = 0; h < COLO_SOL_HAZARD_SOURCE_COUNT; h++) stat[h].min_lead_seen = 999;

    long any_exposed = 0, any_escapable = 0, sol_ticks = 0;
    long reachable_tiles_total = 0, exposed_samples = 0;
    uint64_t arng = 20260728u;

    for (int ep = 0; ep < episodes; ep++) {
        ColosseumContext ctx;
        static ColosseumState s;
        int actions[COLO_NUM_ACTION_HEADS];

        col_init_context_typed(&ctx);
        ctx.config.start_wave = COLO_WAVE_BOSS;
        ctx.config.step_out_forecast_obs_enabled = 1;
        ctx.config.forecast_horizon = 4;
        ctx.config.invuln_mode = 1;  /* survive the whole fight so every volley is sampled */
        memset(&s, 0, sizeof(s));
        col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 31337u + ep * 7u);

        for (int t = 0; t < 2000 && !s.episode_over; t++) {
            for (int h = 0; h < COLO_NUM_ACTION_HEADS; h++)
                actions[h] = (int)(sm(&arng) % (uint64_t)COLO_ACTION_DIMS[h]);
            col_step_ctx((EncounterState*)&s, (EncounterContext*)&ctx, actions);
            if (s.episode_over) break;
            /* random actions never damage Sol, so phases never advance and no crystals
               or molten sand ever spawn. Drain Sol directly to walk the whole fight. */
            for (int n = 0; n < COLO_MAX_NPCS; n++) {
                ColoNPC* npc = &s.npcs[n];
                if (!npc->active || npc->type != COLO_SOL_HEREDIT) continue;
                if (npc->hp > 2) npc->hp -= 2;
            }
            if (!col_sol_clamp_active(&s)) continue;
            sol_ticks++;

            int px = s.player.x, py = s.player.y;
            int cur[COLO_SOL_HAZARD_SOURCE_COUNT];
            int exposed_now = 0;
            for (int h = 0; h < COLO_SOL_HAZARD_SOURCE_COUNT; h++) {
                cur[h] = hazard_hit_tick(&s, h, px, py);
                if (cur[h] > 0) exposed_now = 1;
            }
            if (!exposed_now) continue;

            ColoPrimaryActionForecast fc = {0};
            col_build_primary_action_forecast_ctx(&s, &ctx, &fc);

            int safe_actions = 0;
            for (int a = 0; a < COLO_PRIMARY_DIM; a++) {
                if (!fc.landings[a].valid) continue;
                if (tile_fully_safe(&s, fc.landings[a].land_x, fc.landings[a].land_y))
                    safe_actions++;
            }
            any_exposed++;
            exposed_samples++;
            reachable_tiles_total += safe_actions;
            if (safe_actions > 0) any_escapable++;

            for (int h = 0; h < COLO_SOL_HAZARD_SOURCE_COUNT; h++) {
                if (cur[h] <= 0) continue;
                stat[h].exposed++;
                int lead = cur[h] < 11 ? cur[h] : 11;
                stat[h].lead_hist[lead]++;
                if (cur[h] < stat[h].min_lead_seen) stat[h].min_lead_seen = cur[h];
                if (safe_actions > 0) {
                    stat[h].escapable++;
                    stat[h].lead_escapable[lead]++;
                }
            }
        }
    }

    printf("episodes %d   sol ticks %ld   exposed ticks %ld (%.2f%% of sol ticks)\n\n",
        episodes, sol_ticks, any_exposed, sol_ticks ? 100.0*any_exposed/sol_ticks : 0.0);

    printf("%-16s%12s%12s%12s%10s\n", "hazard", "exposed", "escapable", "escape%", "min lead");
    for (int h = 0; h < COLO_SOL_HAZARD_SOURCE_COUNT; h++) {
        if (!stat[h].exposed) { printf("%-16s%12s\n", HAZ_NAME[h], "(never)"); continue; }
        printf("%-16s%12ld%12ld%11.2f%%%10ld\n", HAZ_NAME[h], stat[h].exposed,
            stat[h].escapable, 100.0*stat[h].escapable/stat[h].exposed, stat[h].min_lead_seen);
    }

    printf("\nANY hazard: exposed %ld  escapable %ld  -> %.3f%% escapable\n",
        any_exposed, any_escapable, any_exposed ? 100.0*any_escapable/any_exposed : 0.0);
    printf("mean fully-safe primary actions available when exposed: %.2f of %d\n",
        exposed_samples ? (double)reachable_tiles_total/exposed_samples : 0.0, COLO_PRIMARY_DIM);

    printf("\nlead-time distribution when exposed (hit_tick -> exposed / escapable)\n");
    for (int h = 0; h < COLO_SOL_HAZARD_SOURCE_COUNT; h++) {
        if (!stat[h].exposed) continue;
        printf("  %-14s", HAZ_NAME[h]);
        for (int l = 1; l < 9; l++)
            if (stat[h].lead_hist[l])
                printf(" t%d:%ld/%ld", l, stat[h].lead_hist[l], stat[h].lead_escapable[l]);
        printf("\n");
    }
    return 0;
}
