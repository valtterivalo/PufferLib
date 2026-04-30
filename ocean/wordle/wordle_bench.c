/* Microbench for wordle c_step. Breaks the per-step cost into:
 *   - feedback + apply_constraints (cheap bookkeeping)
 *   - wordle_recount_candidates    (scans the word list)
 *   - compute_observations         (writes 1655-byte obs)
 *
 * Each "skip-X" variant runs the same workload as c_step but with X turned
 * into a no-op, then we report the wallclock delta as the cost of X.
 *
 * Build:
 *   clang -O2 -DNDEBUG -DPLATFORM_DESKTOP -I./raylib-5.5_macos/include -I./src \
 *         ocean/wordle/wordle_bench.c -o wordle_bench \
 *         raylib-5.5_macos/lib/libraylib.a -lm \
 *         -framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL
 */

#include "wordle.h"
#include <stdio.h>
#include <time.h>
#include <stdint.h>

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

typedef enum { BENCH_FULL = 0, BENCH_NO_RECOUNT = 1, BENCH_NO_OBS = 2, BENCH_NO_BOTH = 3 } BenchMode;

static void bench_step(Wordle* env, BenchMode mode) {
    int action = (int)env->actions[0];

    bool is_repeat = false;
    for (int g = 0; g < env->turn; g++) {
        if (env->guess_ids[g] == action) { is_repeat = true; break; }
    }

    const unsigned char* guess = WORDLE_WORDS[action];
    unsigned char fb[WORDLE_WORD_LEN];
    wordle_compute_feedback(WORDLE_WORDS[env->target_id], guess, fb);

    int slot = env->turn;
    memcpy(env->guesses[slot], guess, WORDLE_WORD_LEN);
    memcpy(env->feedback[slot], fb, WORDLE_WORD_LEN);
    env->guess_ids[slot] = action;

    int greens = 0, yellows = 0;
    for (int i = 0; i < WORDLE_WORD_LEN; i++) {
        if (fb[i] == WORDLE_FB_GREEN) greens++;
        else if (fb[i] == WORDLE_FB_YELLOW) yellows++;
    }
    env->total_greens += greens;
    env->total_yellows += yellows;

    wordle_apply_constraints(env, guess, fb);
    if (mode == BENCH_FULL || mode == BENCH_NO_OBS) {
        wordle_recount_candidates(env);
    }

    if (is_repeat) env->repeats_in_episode++;

    env->turn++;
    bool solved = (greens == WORDLE_WORD_LEN);
    bool out_of_guesses = (env->turn >= WORDLE_MAX_GUESSES);
    env->rewards[0] = solved ? 1.0f : 0.0f;
    env->terminals[0] = (solved || out_of_guesses) ? 1.0f : 0.0f;

    if (env->terminals[0]) {
        c_reset(env);
        return;
    }

    if (mode == BENCH_FULL || mode == BENCH_NO_RECOUNT) {
        compute_observations(env);
    }
}

static double run_loop(Wordle* env, int steps, BenchMode mode) {
    unsigned int seed = 12345u;
    c_reset(env);
    double t0 = now_ns();
    for (int i = 0; i < steps; i++) {
        env->actions[0] = (float)(rand_r(&seed) % WORDLE_NUM_WORDS);
        bench_step(env, mode);
    }
    double t1 = now_ns();
    return (t1 - t0) / (double)steps;
}

int main(int argc, char** argv) {
    int steps = (argc >= 2) ? atoi(argv[1]) : 500000;

    Wordle env = (Wordle){0};
    env.num_agents = 1;
    env.reward_win = 1.0f;
    env.rng = 42;

    float observations[WORDLE_OBS_TOTAL];
    float actions[1] = { 0 };
    float rewards[1] = { 0 };
    float terminals[1] = { 0 };
    env.observations = observations;
    env.actions = actions;
    env.rewards = rewards;
    env.terminals = terminals;

    printf("WORDLE c_step microbench\n");
    printf("steps:     %d\n", steps);
    printf("OBS_SIZE:  %d\n", WORDLE_OBS_SIZE);
    printf("NUM_WORDS: %d\n", WORDLE_NUM_WORDS);
    printf("---\n");

    run_loop(&env, steps / 10, BENCH_FULL);

    double full       = run_loop(&env, steps, BENCH_FULL);
    double no_recount = run_loop(&env, steps, BENCH_NO_RECOUNT);
    double no_obs     = run_loop(&env, steps, BENCH_NO_OBS);
    double no_both    = run_loop(&env, steps, BENCH_NO_BOTH);

    double cost_recount = full - no_recount;
    double cost_obs     = full - no_obs;
    double cost_other   = no_both;

    printf("%-32s  %10.1f ns/step  (%5.1f%%)\n", "full c_step",       full,       100.0);
    printf("%-32s  %10.1f ns/step  (%5.1f%%)\n", "  recount_candidates", cost_recount, 100.0 * cost_recount / full);
    printf("%-32s  %10.1f ns/step  (%5.1f%%)\n", "  compute_observations", cost_obs,     100.0 * cost_obs     / full);
    printf("%-32s  %10.1f ns/step  (%5.1f%%)\n", "  feedback + apply + misc", cost_other,   100.0 * cost_other   / full);
    printf("---\n");
    printf("steps/sec single-threaded: %.0f\n", 1e9 / full);
    return 0;
}
