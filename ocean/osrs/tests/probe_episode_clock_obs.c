/**
 * The episode clock must be additive: with the flag off the observation has to be
 * byte-identical to the flag-on one everywhere except the single clock slot, and
 * that slot has to carry tick/episode_max_ticks. Anything else means the insert
 * shifted a neighbouring block.
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

static void run(int clock_on, int wave, unsigned seed, int steps,
        float* obs_out, ColosseumState* s_out) {
    ColosseumContext ctx;
    static ColosseumState s;
    int actions[COLO_NUM_ACTION_HEADS];
    uint64_t arng = 777u;

    col_init_context_typed(&ctx);
    ctx.config.start_wave = wave;
    ctx.config.episode_clock_obs_enabled = clock_on;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, seed);

    for (int t = 0; t < steps && !s.episode_over; t++) {
        for (int h = 0; h < COLO_NUM_ACTION_HEADS; h++)
            actions[h] = (int)(sm(&arng) % (uint64_t)COLO_ACTION_DIMS[h]);
        col_step_ctx((EncounterState*)&s, (EncounterContext*)&ctx, actions);
    }
    col_write_obs_ctx((EncounterState*)&s, (EncounterContext*)&ctx, obs_out);
    *s_out = s;
}

int main(void) {
    static float on[COLO_NUM_OBS], off[COLO_NUM_OBS];
    ColosseumState s_on, s_off;
    int slot = COLO_OBS_AFTER_WAVE - 1;
    int failures = 0;

    printf("COLO_NUM_OBS=%d  clock slot=%d  (COLO_OBS_AFTER_WAVE=%d)\n\n",
        COLO_NUM_OBS, slot, COLO_OBS_AFTER_WAVE);

    for (int wave = 0; wave < 12; wave++) {
        for (int rep = 0; rep < 6; rep++) {
            unsigned seed = 4242u + wave * 97u + rep * 13u;
            int steps = 40 + rep * 133;
            run(1, wave, seed, steps, on, &s_on);
            run(0, wave, seed, steps, off, &s_off);

            int diffs = 0, first_bad = -1;
            for (int i = 0; i < COLO_NUM_OBS; i++) {
                if (on[i] == off[i]) continue;
                diffs++;
                if (i != slot && first_bad < 0) first_bad = i;
            }
            if (first_bad >= 0) {
                printf("wave %2d rep %d: obs differs at index %d, NOT the clock slot\n",
                    wave, rep, first_bad);
                failures++;
                continue;
            }
            if (off[slot] != 0.0f) {
                printf("wave %2d rep %d: clock slot is %.6f with the flag OFF\n",
                    wave, rep, off[slot]);
                failures++;
                continue;
            }
            float want = (float)s_on.tick / (float)s_on.episode_max_ticks;
            if (want > 1.0f) want = 1.0f;
            if (on[slot] < want - 1e-6f || on[slot] > want + 1e-6f) {
                printf("wave %2d rep %d: clock=%.6f want %.6f (tick %d / %d)\n",
                    wave, rep, on[slot], want, s_on.tick, s_on.episode_max_ticks);
                failures++;
                continue;
            }
            if (s_on.tick != s_off.tick || s_on.player.current_hitpoints != s_off.player.current_hitpoints) {
                printf("wave %2d rep %d: SIM diverged, tick %d vs %d, hp %d vs %d\n",
                    wave, rep, s_on.tick, s_off.tick, s_on.player.current_hitpoints, s_off.player.current_hitpoints);
                failures++;
            }
        }
    }

    run(1, 0, 31337u, 900, on, &s_on);
    printf("late-episode sample: tick %d / %d -> clock %.6f\n",
        s_on.tick, s_on.episode_max_ticks, on[slot]);

    printf("\n%s\n", failures ? "FAIL" : "all 72 samples: only the clock slot differs, value exact, sim identical");
    return failures ? 1 : 0;
}
