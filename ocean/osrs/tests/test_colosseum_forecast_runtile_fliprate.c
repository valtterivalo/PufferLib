/**
 * @file test_colosseum_forecast_runtile_fliprate.c
 * @brief B3 differential flip-rate report: STATIC_THREAT run-tile mode vs the FULL
 *        rollout, restricted to the 16 run-tile actions (indices 9-24), per wave,
 *        per forecast feature, with a false-safe vs false-alarm breakdown.
 *
 * A false-safe difference means STATIC_THREAT reported LESS threat than the full
 * rollout (the dangerous direction: the agent under-fears a tile). A false-alarm
 * means STATIC_THREAT reported MORE threat (over-cautious, the safe direction).
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ocean/osrs/encounters/encounter_colosseum.h"

#define FLIP_FEATURES COLO_STEP_OUT_FORECAST_ACTION_FEATURES
#define RUN_TILE_FIRST 9

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
    long false_safe;
    long false_alarm;
} FlipStats;

static void encode_action_features(
    const ColoStepOutForecastAction* action,
    int horizon,
    float out[FLIP_FEATURES]
) {
    int first_attack_tick = 0;
    int first_style_mask = 0;
    int max_hit = 0;
    int ranged_magic_same_tick = 0;
    for (int tick_idx = 0; tick_idx < horizon; tick_idx++) {
        const ColoStepOutForecastTick* tick = &action->ticks[tick_idx];
        int style_mask = col_step_out_forecast_tick_style_mask(tick);
        if (first_attack_tick == 0 && col_step_out_forecast_tick_has_event(tick)) {
            first_attack_tick = tick_idx + 1;
            first_style_mask = style_mask;
        }
        if (tick->max_hit > max_hit) max_hit = tick->max_hit;
        if (tick->ranged_count > 0 && tick->magic_count > 0) ranged_magic_same_tick = 1;
    }
    out[0] = action->valid ? 1.0f : 0.0f;
    out[1] = (float)first_attack_tick / (float)horizon;
    out[2] = (float)first_style_mask / 7.0f;
    out[3] = (float)max_hit / 150.0f;
    out[4] = action->same_tick_mixed_style_conflict ? 1.0f : 0.0f;
    out[5] = ranged_magic_same_tick ? 1.0f : 0.0f;
    out[6] = action->ranged_magic_offtick_opportunity ? 1.0f : 0.0f;
    out[7] = action->melee_fallback_exposure ? 1.0f : 0.0f;
}

static void classify_diff(FlipStats* stats, int feature_idx, float full_v, float approx_v) {
    if (feature_idx == 1) {
        int full_safe = full_v == 0.0f;
        int approx_safe = approx_v == 0.0f;
        if (approx_safe && !full_safe) stats->false_safe++;
        else if (!approx_safe && full_safe) stats->false_alarm++;
        else if (approx_v > full_v) stats->false_safe++;   /* later hit */
        else stats->false_alarm++;
    } else {
        /* every other feature: higher value == more threat / more flagged danger */
        if (approx_v < full_v) stats->false_safe++;
        else stats->false_alarm++;
    }
}

static void accumulate_step(
    FlipStats* stats,
    const ColoStepOutForecast* full,
    const ColoStepOutForecast* approx,
    int horizon
) {
    for (int action_idx = RUN_TILE_FIRST; action_idx < ENCOUNTER_MOVE_ACTIONS; action_idx++) {
        float full_f[FLIP_FEATURES];
        float approx_f[FLIP_FEATURES];
        encode_action_features(&full->actions[action_idx], horizon, full_f);
        encode_action_features(&approx->actions[action_idx], horizon, approx_f);
        for (int f = 0; f < FLIP_FEATURES; f++) {
            stats->cell_total++;
            stats->feature_total[f]++;
            if (full_f[f] == approx_f[f]) continue;
            stats->cell_diff++;
            stats->feature_diff[f]++;
            classify_diff(stats, f, full_f[f], approx_f[f]);
        }
    }
}

static void run_wave(FlipStats* stats, int wave_label, int start_wave,
                     uint32_t seed, int steps, int horizon) {
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
        ColoStepOutForecast approx;
        col_build_step_out_forecast_horizon_mode(
            &s, &full, horizon, COLO_FORECAST_RUN_TILE_FULL);
        col_build_step_out_forecast_horizon_mode(
            &s, &approx, horizon, COLO_FORECAST_RUN_TILE_STATIC_THREAT);
        accumulate_step(stats, &full, &approx, horizon);
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
    printf("\nwave %s (run tiles 9-24): %ld/%ld cells differ (%.3f%%), "
        "false_safe=%ld false_alarm=%ld\n",
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
    int horizon = argc > 1 ? atoi(argv[1]) : COLO_STEP_OUT_FORECAST_HORIZON;
    if (horizon < 1 || horizon > COLO_STEP_OUT_FORECAST_HORIZON) {
        fprintf(stderr, "horizon must be 1..%d\n", COLO_STEP_OUT_FORECAST_HORIZON);
        return 2;
    }

    col_static_los_table_selftest();
    col_static_footprint_table_selftest();

    printf("B3 flip-rate report: STATIC_THREAT vs FULL run-tile rollout, horizon %d\n",
        horizon);

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
            waves[w].steps, horizon);
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
