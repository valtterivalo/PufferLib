/**
 * Where does colosseum env-step wall time actually go?
 *
 * Uses the env's own zone instrumentation (colosseum_profile.h) rather than any
 * reimplementation. Sweeps start_wave so the state distribution covers the whole
 * run instead of only wave 1.
 *
 * CAVEAT THIS PROBE MAKES VISIBLE: actions here are uniform random. The gear and
 * weapon-choice caches are signature-keyed memos, and uniform random actions
 * defeat them by construction, which is exactly how the 2026-07-22 profile
 * invented a fake 87% hot spot. The gear/weapon hit rates are printed so that
 * artifact is measurable rather than hidden. Forecast cost is state-driven, not
 * action-driven, so it is the number this probe can speak to honestly.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ocean/osrs_colosseum/colosseum_profile.h"
#include "ocean/osrs/encounters/encounter_colosseum.h"

static uint64_t sm(uint64_t* s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

int main(int argc, char** argv) {
    int eps_per_wave = argc > 1 ? atoi(argv[1]) : 40;
    int forecast_on  = argc > 2 ? atoi(argv[2]) : 1;
    int threat_on    = argc > 3 ? atoi(argv[3]) : 1;
    int churn_pct    = argc > 4 ? atoi(argv[4]) : 100;  /* %% of ticks that touch inventory heads */

    if (!colosseum_profile_enabled()) {
        fprintf(stderr, "set PUFFER_COLOSSEUM_PROFILE=1\n");
        return 1;
    }

    int n = colosseum_env_profile_count();
    for (int i = 0; i < n; i++) colosseum_env_profile_read_reset_ms(i);

    uint64_t arng = 424242u;
    long steps = 0;
    double t0 = colosseum_profile_now_ms();

    for (int wave = 0; wave < 12; wave++) {
        for (int ep = 0; ep < eps_per_wave; ep++) {
            ColosseumContext ctx;
            static ColosseumState s;
            int actions[COLO_NUM_ACTION_HEADS];
            static float obs[COLO_NUM_OBS];

            col_init_context_typed(&ctx);
            ctx.config.start_wave = wave;
            ctx.config.step_out_forecast_obs_enabled = forecast_on;
            ctx.config.threat_field_obs_enabled = threat_on;
            ctx.config.forecast_horizon = 4;
            memset(&s, 0, sizeof(s));
            col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 55u + ep * 31u + wave);

            for (int t = 0; t < 400 && !s.episode_over; t++) {
                for (int h = 0; h < COLO_NUM_ACTION_HEADS; h++)
                    actions[h] = (int)(sm(&arng) % (uint64_t)COLO_ACTION_DIMS[h]);
                /* a trained policy rarely reshuffles gear; pin the inventory-click
                   heads to no-op on (100 - churn_pct)%% of ticks so the signature
                   memos behave the way they do under a real policy */
                if ((int)(sm(&arng) % 100) >= churn_pct) {
                    for (int g = 0; g < NUM_GEAR_SLOTS; g++)
                        actions[COLO_HEAD_EQUIP_SLOT(g)] = 0;
                    actions[COLO_HEAD_EAT] = 0;
                    actions[COLO_HEAD_DRINK] = 0;
                    actions[COLO_HEAD_SPEC] = 0;
                }
                col_step_ctx((EncounterState*)&s, (EncounterContext*)&ctx, actions);
                col_write_obs_ctx((EncounterState*)&s, (EncounterContext*)&ctx, obs);
                steps++;
            }
        }
    }
    double wall = colosseum_profile_now_ms() - t0;

    double ms[COLO_PROF_COUNT];
    for (int i = 0; i < n; i++) ms[i] = colosseum_env_profile_read_reset_ms(i);

    printf("forecast_obs=%d threat_field=%d   steps=%ld   wall=%.1f ms   %.2f us/step   %.0f steps/s\n\n",
        forecast_on, threat_on, steps, wall, 1000.0 * wall / steps, steps / (wall / 1000.0));

    double total = ms[COLO_PROF_C_STEP_TOTAL];
    if (total <= 0) total = wall;

    printf("%-26s%12s%10s%12s\n", "zone", "ms", "%step", "us/step");
    for (int i = 0; i < n; i++) {
        if (ms[i] <= 0.0) continue;
        const char* nm = colosseum_env_profile_name(i);
        if (strncmp(nm, "best_gear_", 10) == 0) continue;
        if (strncmp(nm, "weapon_choice_", 14) == 0) continue;
        printf("%-26s%12.1f%9.1f%%%12.3f", nm, ms[i], 100.0 * ms[i] / total,
            1000.0 * ms[i] / steps);
        printf("\n");
    }

    printf("\n=== memo hit rates (uniform random actions DEFEAT these; see header) ===\n");
    double gr = ms[COLO_PROF_BEST_GEAR_REQUESTS], gh = ms[COLO_PROF_BEST_GEAR_HITS];
    double wr = ms[COLO_PROF_WEAPON_CHOICE_REQUESTS], wh = ms[COLO_PROF_WEAPON_CHOICE_HITS];
    printf("  best_gear     requests %.0f  hits %.0f  -> %.1f%% hit\n",
        gr, gh, gr > 0 ? 100.0 * gh / gr : 0.0);
    printf("  weapon_choice requests %.0f  hits %.0f  -> %.1f%% hit\n",
        wr, wh, wr > 0 ? 100.0 * wh / wr : 0.0);
    return 0;
}
