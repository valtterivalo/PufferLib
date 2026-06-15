/**
 * @file test_colosseum_forecast_horizon_fliprate.c
 * @brief B1 differential flip-rate report: full step-out forecast obs block at
 *        horizon 4 vs a shorter horizon, per wave, per forecast feature.
 *
 * Drives wave rollouts with a deterministic action trace (the same generator the
 * exactness gate uses) and, at every captured step, encodes the 8-feature
 * forecast block for each of the 25 move actions at both horizons. Reports the
 * fraction of (action, feature) cells that differ and which features move most.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ocean/osrs/encounters/encounter_colosseum.h"

#define FLIP_FEATURES COLO_STEP_OUT_FORECAST_ACTION_FEATURES

static const char* const FEATURE_NAMES[FLIP_FEATURES] = {
    "valid",
    "first_attack_tick",
    "first_style_mask",
    "max_hit",
    "same_tick_mixed_style_conflict",
    "ranged_magic_same_tick",
    "ranged_magic_offtick_opportunity",
    "melee_fallback_exposure",
};

typedef struct {
    long cell_total;
    long cell_diff;
    long feature_total[FLIP_FEATURES];
    long feature_diff[FLIP_FEATURES];
    long false_safe;   /* horizon-short reported a SAFER value than full (less threat) */
    long false_alarm;  /* horizon-short reported a MORE threatening value than full */
} FlipStats;

static void encode_forecast_block(
    const ColoStepOutForecast* forecast,
    int horizon,
    float out[ENCOUNTER_MOVE_ACTIONS * FLIP_FEATURES]
) {
    int i = 0;
    for (int action_idx = 0; action_idx < ENCOUNTER_MOVE_ACTIONS; action_idx++) {
        const ColoStepOutForecastAction* action = &forecast->actions[action_idx];
        int first_attack_tick = 0;
        int first_style_mask = 0;
        int max_hit = 0;
        int ranged_magic_same_tick = 0;
        for (int tick_idx = 0; tick_idx < horizon; tick_idx++) {
            const ColoStepOutForecastTick* tick = &action->ticks[tick_idx];
            int style_mask = col_step_out_forecast_tick_style_mask(tick);
            if (first_attack_tick == 0 &&
                    col_step_out_forecast_tick_has_event(tick)) {
                first_attack_tick = tick_idx + 1;
                first_style_mask = style_mask;
            }
            if (tick->max_hit > max_hit) max_hit = tick->max_hit;
            if (tick->ranged_count > 0 && tick->magic_count > 0)
                ranged_magic_same_tick = 1;
        }
        out[i++] = action->valid ? 1.0f : 0.0f;
        out[i++] = (float)first_attack_tick / (float)horizon;
        out[i++] = (float)first_style_mask / 7.0f;
        out[i++] = (float)max_hit / 150.0f;
        out[i++] = action->same_tick_mixed_style_conflict ? 1.0f : 0.0f;
        out[i++] = ranged_magic_same_tick ? 1.0f : 0.0f;
        out[i++] = action->ranged_magic_offtick_opportunity ? 1.0f : 0.0f;
        out[i++] = action->melee_fallback_exposure ? 1.0f : 0.0f;
    }
}

/** "Threat magnitude" of a feature value for the false-safe direction. Higher =
    more dangerous from the agent's view. first_attack_tick is inverted: a smaller
    nonzero tick (sooner hit) is more dangerous, and 0 (no hit) is safest. */
static int feature_threat_higher_is_worse(int feature_idx) {
    return feature_idx != 1;
}

static void accumulate_step(
    FlipStats* stats,
    const ColoStepOutForecast* full,
    const ColoStepOutForecast* shortf,
    int short_horizon
) {
    float full_block[ENCOUNTER_MOVE_ACTIONS * FLIP_FEATURES];
    float short_block[ENCOUNTER_MOVE_ACTIONS * FLIP_FEATURES];
    encode_forecast_block(full, COLO_STEP_OUT_FORECAST_HORIZON, full_block);
    encode_forecast_block(shortf, short_horizon, short_block);

    int n = ENCOUNTER_MOVE_ACTIONS * FLIP_FEATURES;
    for (int idx = 0; idx < n; idx++) {
        int feature_idx = idx % FLIP_FEATURES;
        stats->cell_total++;
        stats->feature_total[feature_idx]++;
        if (full_block[idx] == short_block[idx]) continue;
        stats->cell_diff++;
        stats->feature_diff[feature_idx]++;

        float full_v = full_block[idx];
        float short_v = short_block[idx];
        if (feature_idx == 1) {
            /* first_attack_tick: 0 means no hit (safest). a smaller nonzero
               normalized tick means a sooner hit (more dangerous). */
            int full_safe = full_v == 0.0f;
            int short_safe = short_v == 0.0f;
            if (short_safe && !full_safe) stats->false_safe++;
            else if (!short_safe && full_safe) stats->false_alarm++;
            else if (short_v > full_v) stats->false_safe++;  /* later hit reported */
            else stats->false_alarm++;
        } else if (feature_threat_higher_is_worse(feature_idx)) {
            if (short_v < full_v) stats->false_safe++;
            else stats->false_alarm++;
        }
    }
}

static void run_wave(FlipStats* stats, int wave_label, int start_wave,
                     uint32_t seed, int steps, int short_horizon) {
    ColosseumState s;
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ctx.config.start_wave = start_wave;
    ctx.config.step_out_forecast_obs_enabled = 1;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, seed);

    uint64_t rng = ((uint64_t)wave_label << 40) ^ seed ^ 0x53A91C4D12ULL;
    int actions[COLO_NUM_ACTION_HEADS];

    for (int step = 0; step <= steps; step++) {
        ColoStepOutForecast full;
        ColoStepOutForecast shortf;
        col_build_step_out_forecast_horizon(&s, &full, COLO_STEP_OUT_FORECAST_HORIZON);
        col_build_step_out_forecast_horizon(&s, &shortf, short_horizon);
        accumulate_step(stats, &full, &shortf, short_horizon);
        if (step == steps || s.episode_over) break;

        for (int head = 0; head < COLO_NUM_ACTION_HEADS; head++) {
            rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
            actions[head] = (int)((rng >> 33) % (uint64_t)COLO_ACTION_DIMS[head]);
        }
        if (s.modifiers.draft_pending) {
            actions[COLO_HEAD_MOVE] = 0;
            actions[COLO_HEAD_TARGET] = 0;
            rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
            actions[COLO_HEAD_MODIFIER_SELECT] =
                1 + (int)((rng >> 33) % COLO_MODIFIER_DRAFT_OPTIONS);
        }
        col_step_ctx((EncounterState*)&s, (EncounterContext*)&ctx, actions);
    }
}

static void report_wave(const char* label, const FlipStats* stats) {
    double cell_rate = stats->cell_total > 0
        ? 100.0 * (double)stats->cell_diff / (double)stats->cell_total : 0.0;
    printf("\nwave %s: %ld/%ld cells differ (%.3f%%), false_safe=%ld false_alarm=%ld\n",
        label, stats->cell_diff, stats->cell_total, cell_rate,
        stats->false_safe, stats->false_alarm);
    for (int f = 0; f < FLIP_FEATURES; f++) {
        double rate = stats->feature_total[f] > 0
            ? 100.0 * (double)stats->feature_diff[f] / (double)stats->feature_total[f]
            : 0.0;
        printf("  %-34s %6ld/%-6ld %7.3f%%\n",
            FEATURE_NAMES[f], stats->feature_diff[f], stats->feature_total[f], rate);
    }
}

int main(int argc, char** argv) {
    int short_horizon = argc > 1 ? atoi(argv[1]) : 3;
    if (short_horizon < 1 || short_horizon >= COLO_STEP_OUT_FORECAST_HORIZON) {
        fprintf(stderr, "short horizon must be in 1..%d, got %d\n",
            COLO_STEP_OUT_FORECAST_HORIZON - 1, short_horizon);
        return 2;
    }

    col_static_los_table_selftest();
    col_static_footprint_table_selftest();

    printf("B1 flip-rate report: horizon %d vs %d (full)\n",
        short_horizon, COLO_STEP_OUT_FORECAST_HORIZON);

    struct { const char* label; int start_wave; uint32_t seed; int steps; } waves[] = {
        { "1",  0,  0xC010001u, 60 },
        { "4",  3,  0xC010004u, 60 },
        { "8",  7,  0xC010008u, 60 },
        { "12", 11, 0xC010012u, 60 },
    };

    FlipStats overall = {0};
    for (size_t w = 0; w < sizeof(waves) / sizeof(*waves); w++) {
        FlipStats stats = {0};
        run_wave(&stats, (int)w + 1, waves[w].start_wave, waves[w].seed,
            waves[w].steps, short_horizon);
        report_wave(waves[w].label, &stats);
        overall.cell_total += stats.cell_total;
        overall.cell_diff += stats.cell_diff;
        overall.false_safe += stats.false_safe;
        overall.false_alarm += stats.false_alarm;
        for (int f = 0; f < FLIP_FEATURES; f++) {
            overall.feature_total[f] += stats.feature_total[f];
            overall.feature_diff[f] += stats.feature_diff[f];
        }
    }
    report_wave("ALL", &overall);
    return 0;
}
