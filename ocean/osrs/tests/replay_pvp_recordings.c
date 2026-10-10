#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../osrs_env.h"
#include "../osrs_health_bar.h"
#include "recorded_pvp_windows.h"

typedef struct {
    OsrsPvpHitEvent* hits;
    size_t count;
    int actor_swap;
    int mismatched_bars;
    int first_bar_tick;
    int first_bar_target;
    int first_bar_expected;
    int first_bar_actual;
} ReplayTrace;

typedef struct {
    const char* constraint;
    int tick;
    int target;
    int has_expected;
    int expected_tick;
    int expected_damage;
    int has_actual;
    int actual_tick;
    int actual_damage;
} FirstFailure;

typedef enum {
    REPLAY_CANDIDATES_ONLY,
    REPLAY_FIRST_FAILURE_JSONL,
    REPLAY_FIRST_FAILURE_SILENT,
} ReplayOutputMode;

static void check_health_bars(const OsrsEnv* env, const RecordedPvpWindow* window,
    int tick, int actor_swap, ReplayTrace* trace) {
    if (!trace) return;
    for (size_t i = 0; i < window->health_bar_count; i++) {
        const RecordedHealthBar* bar = &window->health_bars[i];
        if (bar->tick != tick) continue;
        const Player* player = &env->players[bar->target ^ actor_swap];
        int actual = osrs_health_bar_ratio(player->current_hitpoints,
            player->base_hitpoints, bar->scale);
        if (actual != bar->ratio) {
            trace->mismatched_bars++;
            if (trace->first_bar_tick < 0) {
                trace->first_bar_tick = tick;
                trace->first_bar_target = bar->target;
                trace->first_bar_expected = bar->ratio;
                trace->first_bar_actual = actual;
            }
        }
    }
}

static void collect_hit(void* context, const OsrsPvpHitEvent* event) {
    ReplayTrace* trace = context;
    OsrsPvpHitEvent* hits = realloc(trace->hits, (trace->count + 1) * sizeof(*hits));
    assert(hits);
    trace->hits = hits;
    trace->hits[trace->count] = *event;
    trace->hits[trace->count].source ^= trace->actor_swap;
    trace->hits[trace->count].target ^= trace->actor_swap;
    trace->count++;
}

static void emit_conditioned_attacks(OsrsEnv* env, const RecordedPvpWindow* window,
    int actor_swap, RecordedAttackPhase phase, int source) {
    for (size_t i = 0; i < window->attack_count; i++) {
        const ConditionedAttack* attack = &window->attacks[i];
        if (attack->tick != env->tick || attack->phase != phase) continue;
        if (phase == RECORDED_AFTER_SOURCE_PASS && attack->source != source) continue;
        int actor = attack->source ^ actor_swap;
        queue_hit(env->tick, actor, 1 - actor, &env->players[actor],
            &env->players[1 - actor], attack->damage, attack->style, 0,
            0, attack->damage > 0, 0, 0, 0, 0, 0);
    }
}

static void replay(OsrsEnv* env, const RecordedPvpWindow* window, const int hp[2],
    int first_actor, int actor_swap, ReplayTrace* trace) {
    memset(env, 0, sizeof(*env));
    env->rng_state = 12345;
    for (int actor = 0; actor < 2; actor++) {
        Player* player = &env->players[actor ^ actor_swap];
        init_player(player);
        memset(player->equipped, ITEM_NONE, sizeof(player->equipped));
        player->current_hitpoints = hp[actor];
        player->prayer = PRAYER_NONE;
        player->veng_active = window->vengeance[actor];
        if (window->recoil[actor]) player->equipped[GEAR_SLOT_RING] = ITEM_RING_OF_RECOIL;
        osrs_refresh_player_equipment(player);
    }
    if (trace) {
        trace->actor_swap = actor_swap;
        trace->first_bar_tick = -1;
        env->pvp_runtime.hit_observer = collect_hit;
        env->pvp_runtime.hit_observer_context = trace;
    }
    check_health_bars(env, window, window->start_tick - 1, actor_swap, trace);
    for (env->tick = window->start_tick; env->tick <= window->end_tick; env->tick++) {
        emit_conditioned_attacks(env, window, actor_swap, RECORDED_BEFORE_PASSES, -1);
        for (int pass = 0; pass < 2; pass++) {
            int actor = first_actor ^ pass;
            pvp_process_incoming_hits(env, actor ^ actor_swap);
            emit_conditioned_attacks(env, window, actor_swap, RECORDED_AFTER_SOURCE_PASS, actor);
        }
        check_health_bars(env, window, env->tick, actor_swap, trace);
    }
}

static int matches(const RecordedPvpWindow* window, const ReplayTrace* trace,
    const OsrsEnv* env) {
    if (trace->count != window->hit_count || trace->mismatched_bars) return 0;
    for (int actor = 0; actor < 2; actor++) {
        size_t observed = 0;
        for (size_t simulated = 0; simulated < trace->count; simulated++) {
            const OsrsPvpHitEvent* hit = &trace->hits[simulated];
            if (hit->target != actor) continue;
            while (observed < window->hit_count && window->hits[observed].target != actor) observed++;
            if (observed == window->hit_count) return 0;
            const RecordedHitsplat* expected = &window->hits[observed++];
            if (hit->tick != expected->tick || hit->damage != expected->damage) return 0;
        }
    }
    return window->dead_actor < 0 ||
        env->players[window->dead_actor ^ trace->actor_swap].current_hitpoints == 0;
}

static FirstFailure first_hit_failure(const RecordedPvpWindow* window,
    const ReplayTrace* trace) {
    FirstFailure first = {0};
    first.tick = window->end_tick;
    first.target = -1;
    for (int target = 0; target < 2; target++) {
        size_t expected_index = 0;
        size_t actual_index = 0;
        for (;;) {
            while (expected_index < window->hit_count &&
                window->hits[expected_index].target != target) expected_index++;
            while (actual_index < trace->count &&
                trace->hits[actual_index].target != target) actual_index++;
            int has_expected = expected_index < window->hit_count;
            int has_actual = actual_index < trace->count;
            if (!has_expected && !has_actual) break;
            if (has_expected && has_actual &&
                window->hits[expected_index].tick == trace->hits[actual_index].tick &&
                window->hits[expected_index].damage == trace->hits[actual_index].damage) {
                expected_index++;
                actual_index++;
                continue;
            }
            int tick = has_expected ? window->hits[expected_index].tick : trace->hits[actual_index].tick;
            if (has_actual && trace->hits[actual_index].tick < tick)
                tick = trace->hits[actual_index].tick;
            if (!first.constraint || tick < first.tick) {
                first = (FirstFailure){
                    .constraint = "hitsplat",
                    .tick = tick,
                    .target = target,
                    .has_expected = has_expected,
                    .expected_tick = has_expected ? window->hits[expected_index].tick : 0,
                    .expected_damage = has_expected ? window->hits[expected_index].damage : 0,
                    .has_actual = has_actual,
                    .actual_tick = has_actual ? trace->hits[actual_index].tick : 0,
                    .actual_damage = has_actual ? trace->hits[actual_index].damage : 0,
                };
            }
            break;
        }
    }
    return first;
}

static FirstFailure first_failure(const RecordedPvpWindow* window,
    const ReplayTrace* trace, const OsrsEnv* env) {
    FirstFailure first = first_hit_failure(window, trace);
    if (trace->first_bar_tick >= 0 && (!first.constraint ||
        trace->first_bar_tick < first.tick)) {
        first = (FirstFailure){
            .constraint = "health_bar",
            .tick = trace->first_bar_tick,
            .target = trace->first_bar_target,
            .has_expected = 1,
            .expected_tick = trace->first_bar_tick,
            .expected_damage = trace->first_bar_expected,
            .has_actual = 1,
            .actual_tick = trace->first_bar_tick,
            .actual_damage = trace->first_bar_actual,
        };
    }
    if (window->dead_actor >= 0) {
        int actual_hp = env->players[window->dead_actor ^ trace->actor_swap].current_hitpoints;
        if (actual_hp != 0 && (!first.constraint || window->end_tick < first.tick)) {
            first = (FirstFailure){
                .constraint = "death",
                .tick = window->end_tick,
                .target = window->dead_actor,
                .has_expected = 1,
                .expected_tick = window->end_tick,
                .expected_damage = 0,
                .has_actual = 1,
                .actual_tick = window->end_tick,
                .actual_damage = actual_hp,
            };
        }
    }
    return first;
}

static void print_window_summary(const RecordedPvpWindow* window, int total,
    int compatible, int latest_rejection_tick) {
    printf("{\"record_type\":\"window_summary\",\"schema_version\":1,"
        "\"window\":\"%s\",\"archive\":\"%s\","
        "\"source_sha256\":\"%s\",\"assumptions\":\"%s\","
        "\"start_tick\":%d,\"end_tick\":%d,\"dead_actor\":%d,"
        "\"total_hypotheses\":%d,\"survivors\":%d,\"contradicted\":%d,"
        "\"expected_candidate_count\":%d,\"candidate_count_matches_expectation\":%s,"
        "\"hp_bounds\":[[%d,%d],[%d,%d]],\"first_processed_actor_values\":[0,1],"
        "\"actor_mapping\":[0,1],"
        "\"constraint_ordering\":\"earliest game tick; no cross-actor callback sequence\","
        "\"global_exhaustion_tick\":",
        window->name, window->archive, window->source_sha256, window->assumptions,
        window->start_tick, window->end_tick, window->dead_actor,
        total, compatible, total - compatible, window->expected_candidates,
        compatible == window->expected_candidates ? "true" : "false",
        window->hp_min[0], window->hp_max[0], window->hp_min[1], window->hp_max[1]);
    if (compatible == 0) printf("%d", latest_rejection_tick);
    else printf("null");
    printf(",\"recorded_hits\":[");
    for (size_t i = 0; i < window->hit_count; i++) {
        const RecordedHitsplat* hit = &window->hits[i];
        printf("%s{\"tick\":%d,\"target\":%d,\"damage\":%d}",
            i ? "," : "", hit->tick, hit->target, hit->damage);
    }
    printf("],\"health_bars\":[");
    for (size_t i = 0; i < window->health_bar_count; i++) {
        const RecordedHealthBar* bar = &window->health_bars[i];
        printf("%s{\"tick\":%d,\"target\":%d,\"ratio\":%d,\"scale\":%d}",
            i ? "," : "", bar->tick, bar->target, bar->ratio, bar->scale);
    }
    puts("],\"unverified_inputs\":[\"human commands\",\"scene collision map\","
        "\"server PID\",\"exact attack rolls\",\"exact initial hidden state\","
        "\"same-tick cross-actor callback order\"]}");
}

static void print_hypothesis(const RecordedPvpWindow* window, const int hp[2],
    int first_actor, const FirstFailure* failure) {
    printf("{\"record_type\":\"hypothesis\",\"schema_version\":1,"
        "\"window\":\"%s\",\"archive\":\"%s\","
        "\"source_sha256\":\"%s\",\"assumptions\":\"%s\","
        "\"hp\":[%d,%d],\"first_processed_actor\":%d,\"status\":\"%s\"",
        window->name, window->archive, window->source_sha256, window->assumptions,
        hp[0], hp[1], first_actor, failure->constraint ? "contradicted" : "compatible");
    if (failure->constraint) {
        printf(",\"first_failure\":{\"constraint\":\"%s\",\"tick\":%d,\"target\":%d,\"expected\":",
            failure->constraint, failure->tick, failure->target);
        if (failure->has_expected)
            printf("{\"tick\":%d,\"value\":%d}", failure->expected_tick, failure->expected_damage);
        else printf("null");
        printf(",\"actual\":");
        if (failure->has_actual)
            printf("{\"tick\":%d,\"value\":%d}", failure->actual_tick, failure->actual_damage);
        else printf("null");
        printf("}");
    }
    puts("}");
}

static void print_candidate(const RecordedPvpWindow* window, const ReplayTrace* trace,
    const int hp[2], int first_actor) {
    printf("{\"window\":\"%s\",\"archive\":\"%s\",\"source_sha256\":\"%s\","
        "\"assumptions\":\"%s\",\"candidate_hp\":[%d,%d],\"candidate_first_actor\":%d,"
        "\"simulated_hits\":[", window->name, window->archive, window->source_sha256,
        window->assumptions, hp[0], hp[1], first_actor);
    for (size_t i = 0; i < trace->count; i++) {
        const OsrsPvpHitEvent* hit = &trace->hits[i];
        printf("%s{\"tick\":%d,\"source\":%d,\"target\":%d,\"kind\":%d,"
            "\"damage\":%d,\"hp_lost\":%d}", i ? "," : "", hit->tick,
            hit->source, hit->target, hit->kind, hit->damage, hit->hitpoints_lost);
    }
    printf("],\"health_bars\":[");
    for (size_t i = 0; i < window->health_bar_count; i++) {
        const RecordedHealthBar* bar = &window->health_bars[i];
        printf("%s{\"tick\":%d,\"target\":%d,\"ratio\":%d,\"scale\":%d}",
            i ? "," : "", bar->tick, bar->target, bar->ratio, bar->scale);
    }
    puts("]}");
}

static int enumerate(const RecordedPvpWindow* window, int actor_swap, int output,
    ReplayOutputMode output_mode, int* total_out, int* latest_rejection_tick_out) {
    OsrsEnv* env = malloc(sizeof(*env));
    OsrsEnv* unobserved = malloc(sizeof(*unobserved));
    assert(env && unobserved);
    int count = 0;
    int total = 0;
    int latest_rejection_tick = window->start_tick - 1;
    for (int hp0 = window->hp_min[0]; hp0 <= window->hp_max[0]; hp0++) {
        for (int hp1 = window->hp_min[1]; hp1 <= window->hp_max[1]; hp1++) {
            int hp[2] = {hp0, hp1};
            for (int first = 0; first < 2; first++) {
                ReplayTrace trace = {0};
                replay(env, window, hp, first, actor_swap, &trace);
                replay(unobserved, window, hp, first, actor_swap, NULL);
                env->pvp_runtime.hit_observer = NULL;
                env->pvp_runtime.hit_observer_context = NULL;
                assert(memcmp(env, unobserved, sizeof(*env)) == 0);
                int compatible = matches(window, &trace, env);
                FirstFailure failure = output_mode != REPLAY_CANDIDATES_ONLY
                    ? first_failure(window, &trace, env) : (FirstFailure){0};
                if (output_mode != REPLAY_CANDIDATES_ONLY)
                    assert(compatible == (failure.constraint == NULL));
                ReplayTrace swapped = {0};
                replay(unobserved, window, hp, first, 1 - actor_swap, &swapped);
                assert(matches(window, &swapped, unobserved) == compatible);
                free(swapped.hits);
                if (compatible) {
                    count++;
                    if (output) print_candidate(window, &trace, hp, first);
                } else if (output_mode != REPLAY_CANDIDATES_ONLY) {
                    if (failure.tick > latest_rejection_tick)
                        latest_rejection_tick = failure.tick;
                }
                total++;
                if (output_mode == REPLAY_FIRST_FAILURE_JSONL)
                    print_hypothesis(window, hp, first, &failure);
                free(trace.hits);
            }
        }
    }
    free(unobserved);
    free(env);
    if (total_out) *total_out = total;
    if (latest_rejection_tick_out) *latest_rejection_tick_out = latest_rejection_tick;
    return count;
}

static const RecordedPvpWindow* find_window(const char* name) {
    for (size_t i = 0; i < sizeof(recorded_pvp_windows) / sizeof(recorded_pvp_windows[0]); i++)
        if (strcmp(recorded_pvp_windows[i].name, name) == 0) return &recorded_pvp_windows[i];
    return NULL;
}

static int run_first_divergence(const RecordedPvpWindow* window) {
    int total, latest_rejection_tick;
    int compatible = enumerate(window, 0, 0, REPLAY_FIRST_FAILURE_JSONL,
        &total, &latest_rejection_tick);
    print_window_summary(window, total, compatible, latest_rejection_tick);
    return 0;
}

static void check_first_failure_contract(const RecordedPvpWindow* window) {
    OsrsEnv* env = malloc(sizeof(*env));
    assert(env);
    RecordedHitsplat* changed_hits = malloc(window->hit_count * sizeof(*changed_hits));
    assert(changed_hits);
    int found = 0;
    for (int hp0 = window->hp_min[0]; hp0 <= window->hp_max[0] && !found; hp0++) {
        for (int hp1 = window->hp_min[1]; hp1 <= window->hp_max[1] && !found; hp1++) {
            int hp[2] = {hp0, hp1};
            for (int first_actor = 0; first_actor < 2 && !found; first_actor++) {
                ReplayTrace trace = {0};
                replay(env, window, hp, first_actor, 0, &trace);
                if (matches(window, &trace, env)) {
                    RecordedPvpWindow changed = *window;
                    memcpy(changed_hits, window->hits,
                        window->hit_count * sizeof(*changed_hits));
                    changed_hits[0].damage++;
                    changed.hits = changed_hits;
                    FirstFailure failure = first_failure(&changed, &trace, env);
                    assert(strcmp(failure.constraint, "hitsplat") == 0);
                    assert(failure.tick == window->hits[0].tick);
                    assert(failure.target == window->hits[0].target);
                    assert(failure.expected_damage == window->hits[0].damage + 1);
                    assert(failure.actual_damage == window->hits[0].damage);

                    changed_hits[0] = window->hits[0];
                    changed_hits[0].tick++;
                    failure = first_failure(&changed, &trace, env);
                    assert(strcmp(failure.constraint, "hitsplat") == 0);
                    assert(failure.tick == window->hits[0].tick);
                    assert(failure.expected_tick == window->hits[0].tick + 1);
                    assert(failure.actual_tick == window->hits[0].tick);
                    found = 1;
                }
                free(trace.hits);
            }
        }
    }
    assert(found);
    free(changed_hits);
    free(env);
}

static int latest_failure_tick(const RecordedPvpWindow* window) {
    OsrsEnv* env = malloc(sizeof(*env));
    assert(env);
    int latest = window->start_tick - 1;
    for (int hp0 = window->hp_min[0]; hp0 <= window->hp_max[0]; hp0++) {
        for (int hp1 = window->hp_min[1]; hp1 <= window->hp_max[1]; hp1++) {
            int hp[2] = {hp0, hp1};
            for (int first_actor = 0; first_actor < 2; first_actor++) {
                ReplayTrace trace = {0};
                replay(env, window, hp, first_actor, 0, &trace);
                FirstFailure failure = first_failure(window, &trace, env);
                assert(failure.constraint);
                if (failure.tick > latest) latest = failure.tick;
                free(trace.hits);
            }
        }
    }
    free(env);
    return latest;
}

static void check_zero_survivor_exhaustion(const RecordedPvpWindow* source) {
    RecordedPvpWindow impossible = *source;
    RecordedHitsplat* hits = malloc(source->hit_count * sizeof(*hits));
    assert(hits);
    memcpy(hits, source->hits, source->hit_count * sizeof(*hits));
    hits[0].damage++;
    impossible.hits = hits;
    int total, latest_rejection_tick;
    int survivors = enumerate(&impossible, 0, 0, REPLAY_FIRST_FAILURE_SILENT,
        &total, &latest_rejection_tick);
    assert(total == (source->hp_max[0] - source->hp_min[0] + 1) *
        (source->hp_max[1] - source->hp_min[1] + 1) * 2);
    assert(survivors == 0);
    assert(latest_rejection_tick == latest_failure_tick(&impossible));
    free(hits);
}

int main(int argc, char** argv) {
    int first_divergence = 0;
    const RecordedPvpWindow* selected = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--first-divergence") == 0) {
            first_divergence = 1;
        } else if (strcmp(argv[i], "--window") == 0 && i + 1 < argc) {
            selected = find_window(argv[++i]);
            if (!selected) {
                fprintf(stderr, "unknown replay window: %s\n", argv[i]);
                return EXIT_FAILURE;
            }
        } else {
            fprintf(stderr, "usage: %s [--first-divergence] [--window NAME]\n", argv[0]);
            return EXIT_FAILURE;
        }
    }
    if (first_divergence) {
        if (selected) return run_first_divergence(selected);
        for (size_t i = 0; i < sizeof(recorded_pvp_windows) / sizeof(recorded_pvp_windows[0]); i++)
            run_first_divergence(&recorded_pvp_windows[i]);
        return 0;
    }
    if (selected) {
        int candidates = enumerate(selected, 0, 1, REPLAY_CANDIDATES_ONLY, NULL, NULL);
        if (candidates != selected->expected_candidates) {
            fprintf(stderr, "%s: expected %d compatible candidates, found %d\n",
                selected->name, selected->expected_candidates, candidates);
            return EXIT_FAILURE;
        }
        fprintf(stderr, "%s PASS: %d compatible candidates\n", selected->name, candidates);
        return 0;
    }
    for (size_t i = 0; i < sizeof(recorded_pvp_windows) / sizeof(recorded_pvp_windows[0]); i++) {
        const RecordedPvpWindow* window = &recorded_pvp_windows[i];
        check_first_failure_contract(window);
        int candidates = enumerate(window, 0, 1, REPLAY_CANDIDATES_ONLY, NULL, NULL);
        if (candidates != window->expected_candidates) {
            fprintf(stderr, "%s: expected %d compatible candidates, found %d\n",
                window->name, window->expected_candidates, candidates);
            return EXIT_FAILURE;
        }
        RecordedPvpWindow impossible = *window;
        RecordedHitsplat* hits = malloc(window->hit_count * sizeof(*hits));
        assert(hits);
        memcpy(hits, window->hits, window->hit_count * sizeof(*hits));
        hits[0].damage += 100;
        impossible.hits = hits;
        int total, latest_rejection_tick;
        int negative_candidates = enumerate(&impossible, 0, 0, REPLAY_FIRST_FAILURE_SILENT,
            &total, &latest_rejection_tick);
        if (negative_candidates != 0 || total == 0 || latest_rejection_tick < window->start_tick - 1) {
            fprintf(stderr, "%s: altered recorded hitsplat incorrectly accepted\n", window->name);
            return EXIT_FAILURE;
        }
        memcpy(hits, window->hits, window->hit_count * sizeof(*hits));
        hits[0].tick++;
        negative_candidates = enumerate(&impossible, 0, 0, REPLAY_FIRST_FAILURE_SILENT,
            &total, &latest_rejection_tick);
        free(hits);
        if (negative_candidates != 0 || total == 0 || latest_rejection_tick < window->start_tick - 1) {
            fprintf(stderr, "%s: shifted hitsplat tick incorrectly accepted\n", window->name);
            return EXIT_FAILURE;
        }
    }
    check_zero_survivor_exhaustion(&recorded_pvp_windows[1]);
    fprintf(stderr, "Recorded PvP windows PASS: timing, per-actor hit order, HP candidates, actor swaps, negative controls, observer independence\n");
    return 0;
}
