/* Wall-clock cost of one colosseum env step, with no profiler marks compiled in.
 *
 * Exists to reconcile two numbers that disagree by 5x: the in-env profiler reports ~24400
 * ns/env-step, an earlier campaign recorded 4570. Both cannot be right, and every optimisation
 * decision downstream is priced against whichever one is.
 *
 * Covers exactly what puf_step covers: encounter step, observation write, action mask write.
 *
 * Equip heads are PINNED. The env memoises on a loadout signature, and uniform random actions
 * churn every equip head each tick, which is a genuine signature change any correct memo must
 * miss -- that artifact once produced a bogus "87% of env time is gear/DPT" conclusion.
 *
 * Build with -std=gnu11: plain c11 hides clock_gettime on glibc and _POSIX_C_SOURCE hides
 * snprintf on macOS. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ocean/osrs/encounters/encounter_colosseum.h"

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static inline uint64_t splitmix64(uint64_t* s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static void fill_actions(const ColosseumState* s, uint64_t* rng,
        int actions[COLO_NUM_ACTION_HEADS]) {
    for (int h = 0; h < COLO_NUM_ACTION_HEADS; h++) {
        int equip = h >= COLO_HEAD_EQUIP_BASE && h < COLO_HEAD_EQUIP_BASE + NUM_GEAR_SLOTS;
        actions[h] = equip ? 0 : (int)(splitmix64(rng) % (uint64_t)COLO_ACTION_DIMS[h]);
    }
    if (s->modifiers.draft_pending) {
        actions[COLO_HEAD_PRIMARY] = 0;
        actions[COLO_HEAD_MODIFIER_SELECT] =
            1 + (int)(splitmix64(rng) % COLO_MODIFIER_DRAFT_OPTIONS);
    }
}

typedef struct { double step, obs, mask, total; long n; } Bench;

static Bench run(int warmup_steps, int measured_steps) {
    ColosseumContext ctx;
    ColosseumState s;
    static float obs[COLO_NUM_OBS];
    static float mask[COLO_ACTION_MASK_SIZE];
    int actions[COLO_NUM_ACTION_HEADS];
    Bench b = {0};

    col_init_context_typed(&ctx);
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 12345u);
    uint64_t rng = 0xBEEF1234ULL;

    for (int i = 0; i < warmup_steps + measured_steps; i++) {
        int measuring = i >= warmup_steps;
        if (s.episode_over) {
            col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, (uint32_t)(1000 + i));
        }
        fill_actions(&s, &rng, actions);

        double t0 = now_ns();
        col_step_ctx((EncounterState*)&s, (EncounterContext*)&ctx, actions);
        double t1 = now_ns();
        col_write_obs_ctx((EncounterState*)&s, (EncounterContext*)&ctx, obs);
        double t2 = now_ns();
        col_write_mask_ctx((EncounterState*)&s, (EncounterContext*)&ctx, mask);
        double t3 = now_ns();

        if (measuring) {
            b.step += t1 - t0;
            b.obs  += t2 - t1;
            b.mask += t3 - t2;
            b.total += t3 - t0;
            b.n++;
        }
    }
    return b;
}

int main(void) {
    /* One untimed pass so the static arena/LOS tables are built before anything is measured. */
    run(200, 1);

    Bench b = run(2000, 200000);
    printf("colosseum step benchmark, no profiler marks, equip heads pinned\n");
    printf("  measured steps        %ld\n", b.n);
    printf("  col_step_ctx          %8.1f ns/step\n", b.step / b.n);
    printf("  col_write_obs_ctx     %8.1f ns/step\n", b.obs / b.n);
    printf("  col_write_mask_ctx    %8.1f ns/step\n", b.mask / b.n);
    printf("  TOTAL                 %8.1f ns/step\n", b.total / b.n);
    printf("\n  obs is %d floats, mask is %d floats\n",
        COLO_NUM_OBS, COLO_ACTION_MASK_SIZE);
    return 0;
}
