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
} ReplayTrace;

static void check_health_bars(const OsrsEnv* env, const RecordedPvpWindow* window,
    int tick, int actor_swap, ReplayTrace* trace) {
    if (!trace) return;
    for (size_t i = 0; i < window->health_bar_count; i++) {
        const RecordedHealthBar* bar = &window->health_bars[i];
        if (bar->tick != tick) continue;
        const Player* player = &env->players[bar->target ^ actor_swap];
        trace->mismatched_bars += osrs_health_bar_ratio(player->current_hitpoints,
            player->base_hitpoints, bar->scale) != bar->ratio;
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

static int enumerate(const RecordedPvpWindow* window, int actor_swap, int output) {
    OsrsEnv* env = malloc(sizeof(*env));
    OsrsEnv* unobserved = malloc(sizeof(*unobserved));
    assert(env && unobserved);
    int count = 0;
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
                ReplayTrace swapped = {0};
                replay(unobserved, window, hp, first, 1 - actor_swap, &swapped);
                assert(matches(window, &swapped, unobserved) == compatible);
                free(swapped.hits);
                if (compatible) {
                    count++;
                    if (output) print_candidate(window, &trace, hp, first);
                }
                free(trace.hits);
            }
        }
    }
    free(unobserved);
    free(env);
    return count;
}

int main(void) {
    for (size_t i = 0; i < sizeof(recorded_pvp_windows) / sizeof(recorded_pvp_windows[0]); i++) {
        const RecordedPvpWindow* window = &recorded_pvp_windows[i];
        int candidates = enumerate(window, 0, 1);
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
        int negative_candidates = enumerate(&impossible, 0, 0);
        if (negative_candidates != 0) {
            fprintf(stderr, "%s: altered recorded hitsplat incorrectly accepted\n", window->name);
            return EXIT_FAILURE;
        }
        memcpy(hits, window->hits, window->hit_count * sizeof(*hits));
        hits[0].tick++;
        negative_candidates = enumerate(&impossible, 0, 0);
        free(hits);
        if (negative_candidates != 0) {
            fprintf(stderr, "%s: shifted hitsplat tick incorrectly accepted\n", window->name);
            return EXIT_FAILURE;
        }
    }
    fprintf(stderr, "Recorded PvP windows PASS: timing, per-actor hit order, HP candidates, actor swaps, negative controls, observer independence\n");
    return 0;
}
