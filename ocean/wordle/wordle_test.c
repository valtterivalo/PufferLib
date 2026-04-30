/* Property tests for wordle env.
 *
 * Build:
 *   clang -O2 -DNDEBUG -DPLATFORM_DESKTOP -I./raylib-5.5_macos/include -I./src \
 *         ocean/wordle/wordle_test.c -o wordle_test \
 *         raylib-5.5_macos/lib/libraylib.a -lm \
 *         -framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL
 *
 * Run:
 *   ./wordle_test
 *
 * Exits 0 on success, 1 on any failure. Each test prints PASS/FAIL with sample count.
 */

#include "wordle.h"
#include <stdio.h>

static int g_failures = 0;

#define TEST_FAIL(fmt, ...) do { \
    fprintf(stderr, "FAIL %s: " fmt "\n", __func__, ##__VA_ARGS__); \
    g_failures++; \
    return; \
} while (0)

#define TEST_PASS(samples) printf("PASS %s (%d samples)\n", __func__, samples)

static unsigned int test_seed = 0xC0FFEE;

static int rand_word_id(void) {
    return (int)(rand_r(&test_seed) % WORDLE_NUM_WORDS);
}

/* ------------------------------------------------------------------ */
/* Pure feedback properties                                            */
/* ------------------------------------------------------------------ */

static void test_feedback_self_all_green(void) {
    unsigned char fb[WORDLE_WORD_LEN];
    for (int wid = 0; wid < WORDLE_NUM_WORDS; wid++) {
        wordle_compute_feedback(WORDLE_WORDS[wid], WORDLE_WORDS[wid], fb);
        for (int i = 0; i < WORDLE_WORD_LEN; i++) {
            if (fb[i] != WORDLE_FB_GREEN) {
                TEST_FAIL("wid=%d pos=%d expected GREEN got %d", wid, i, fb[i]);
            }
        }
    }
    TEST_PASS(WORDLE_NUM_WORDS);
}

static void test_feedback_letter_count_invariant(void) {
    int samples = 5000;
    unsigned char fb[WORDLE_WORD_LEN];
    for (int s = 0; s < samples; s++) {
        const unsigned char* t = WORDLE_WORDS[rand_word_id()];
        const unsigned char* g = WORDLE_WORDS[rand_word_id()];
        wordle_compute_feedback(t, g, fb);

        int t_count[WORDLE_ALPHABET] = {0};
        int g_count[WORDLE_ALPHABET] = {0};
        int colored[WORDLE_ALPHABET] = {0};
        for (int i = 0; i < WORDLE_WORD_LEN; i++) {
            t_count[t[i]]++;
            g_count[g[i]]++;
            if (fb[i] == WORDLE_FB_GREEN || fb[i] == WORDLE_FB_YELLOW) {
                colored[g[i]]++;
            }
        }
        for (int c = 0; c < WORDLE_ALPHABET; c++) {
            int expected = t_count[c] < g_count[c] ? t_count[c] : g_count[c];
            if (colored[c] != expected) {
                TEST_FAIL("letter %c: colored=%d expected=%d (t_count=%d g_count=%d)",
                          'a' + c, colored[c], expected, t_count[c], g_count[c]);
            }
        }
    }
    TEST_PASS(samples);
}

static void test_feedback_disjoint_all_gray(void) {
    int samples = 0;
    int matched = 0;
    unsigned char fb[WORDLE_WORD_LEN];
    while (matched < 200 && samples < 100000) {
        samples++;
        const unsigned char* t = WORDLE_WORDS[rand_word_id()];
        const unsigned char* g = WORDLE_WORDS[rand_word_id()];
        bool t_has[WORDLE_ALPHABET] = {false};
        for (int i = 0; i < WORDLE_WORD_LEN; i++) t_has[t[i]] = true;
        bool overlap = false;
        for (int i = 0; i < WORDLE_WORD_LEN; i++) {
            if (t_has[g[i]]) { overlap = true; break; }
        }
        if (overlap) continue;
        matched++;
        wordle_compute_feedback(t, g, fb);
        for (int i = 0; i < WORDLE_WORD_LEN; i++) {
            if (fb[i] != WORDLE_FB_GRAY) {
                TEST_FAIL("disjoint t/g but pos %d is %d not GRAY", i, fb[i]);
            }
        }
    }
    TEST_PASS(matched);
}

/* ------------------------------------------------------------------ */
/* Constraint and candidate-set properties                             */
/* ------------------------------------------------------------------ */

static void test_target_always_satisfies(void) {
    Wordle env = (Wordle){0};
    float observations[WORDLE_OBS_TOTAL];
    float actions[1] = {0}, rewards[1] = {0}, terminals[1] = {0};
    env.num_agents = 1;
    env.observations = observations;
    env.actions = actions;
    env.rewards = rewards;
    env.terminals = terminals;
    env.rng = test_seed;

    int episodes = 500;
    for (int ep = 0; ep < episodes; ep++) {
        c_reset(&env);
        int target = env.target_id;
        for (int t = 0; t < WORDLE_MAX_GUESSES; t++) {
            int action;
            do { action = rand_word_id(); } while (action == target);
            env.actions[0] = (float)action;
            c_step(&env);
            if (env.terminals[0] != 0.0f) break;
            if (!wordle_word_satisfies_constraints(&env, target)) {
                TEST_FAIL("episode %d turn %d: target %d failed its own constraints", ep, t, target);
            }
        }
    }
    TEST_PASS(episodes);
}

static void test_recount_monotone_and_idempotent(void) {
    Wordle env = (Wordle){0};
    float observations[WORDLE_OBS_TOTAL];
    float actions[1] = {0}, rewards[1] = {0}, terminals[1] = {0};
    env.num_agents = 1;
    env.observations = observations;
    env.actions = actions;
    env.rewards = rewards;
    env.terminals = terminals;
    env.rng = test_seed + 1;

    int episodes = 500;
    for (int ep = 0; ep < episodes; ep++) {
        c_reset(&env);
        int prev = env.candidate_count;
        for (int t = 0; t < WORDLE_MAX_GUESSES; t++) {
            env.actions[0] = (float)rand_word_id();
            c_step(&env);
            if (env.terminals[0] != 0.0f) break;
            int after = env.candidate_count;
            if (after > prev) {
                TEST_FAIL("episode %d turn %d: count rose %d -> %d", ep, t, prev, after);
            }
            int repeat = wordle_recount_candidates(&env);
            if (repeat != after) {
                TEST_FAIL("episode %d turn %d: recount not idempotent (%d != %d)",
                          ep, t, repeat, after);
            }
            prev = after;
        }
    }
    TEST_PASS(episodes);
}

/* ------------------------------------------------------------------ */
/* Episode lifecycle                                                   */
/* ------------------------------------------------------------------ */

static void test_solve_yields_reward_one(void) {
    Wordle env = (Wordle){0};
    float observations[WORDLE_OBS_TOTAL];
    float actions[1] = {0}, rewards[1] = {0}, terminals[1] = {0};
    env.num_agents = 1;
    env.observations = observations;
    env.actions = actions;
    env.rewards = rewards;
    env.terminals = terminals;
    env.reward_win = 1.0f;
    env.rng = test_seed + 2;

    int episodes = 200;
    for (int ep = 0; ep < episodes; ep++) {
        c_reset(&env);
        env.actions[0] = (float)env.target_id;
        c_step(&env);
        if (env.rewards[0] != 1.0f || env.terminals[0] != 1.0f) {
            TEST_FAIL("episode %d: target guess gave reward=%f terminal=%f",
                      ep, (double)env.rewards[0], (double)env.terminals[0]);
        }
    }
    TEST_PASS(episodes);
}

static void test_max_guesses_terminates(void) {
    Wordle env = (Wordle){0};
    float observations[WORDLE_OBS_TOTAL];
    float actions[1] = {0}, rewards[1] = {0}, terminals[1] = {0};
    env.num_agents = 1;
    env.observations = observations;
    env.actions = actions;
    env.rewards = rewards;
    env.terminals = terminals;
    env.rng = test_seed + 3;

    int episodes = 500;
    for (int ep = 0; ep < episodes; ep++) {
        c_reset(&env);
        int target = env.target_id;
        bool terminated = false;
        for (int t = 0; t < WORDLE_MAX_GUESSES; t++) {
            int action;
            do { action = rand_word_id(); } while (action == target);
            env.actions[0] = (float)action;
            c_step(&env);
            if (env.terminals[0] != 0.0f) {
                terminated = true;
                if (env.rewards[0] != 0.0f) {
                    TEST_FAIL("episode %d: non-target terminal had reward %f",
                              ep, (double)env.rewards[0]);
                }
                break;
            }
        }
        if (!terminated) {
            TEST_FAIL("episode %d: did not terminate after %d non-target guesses",
                      ep, WORDLE_MAX_GUESSES);
        }
    }
    TEST_PASS(episodes);
}

/* ------------------------------------------------------------------ */
/* Observation invariants                                              */
/* ------------------------------------------------------------------ */

static void test_obs_size_consistent(void) {
    int sum = WORDLE_OBS_GUESS_LETTERS + WORDLE_OBS_FEEDBACK + WORDLE_OBS_TURN
            + WORDLE_OBS_GREEN_POS + WORDLE_OBS_FORBIDDEN_POS
            + WORDLE_OBS_MIN_COUNT + WORDLE_OBS_MAX_COUNT + WORDLE_OBS_REMAINING;
    if (sum != WORDLE_OBS_SIZE) {
        TEST_FAIL("section sum %d != WORDLE_OBS_SIZE %d", sum, WORDLE_OBS_SIZE);
    }
    TEST_PASS(1);
}

static void test_obs_one_hot_invariants(void) {
    Wordle env = (Wordle){0};
    float observations[WORDLE_OBS_TOTAL];
    float actions[1] = {0}, rewards[1] = {0}, terminals[1] = {0};
    env.num_agents = 1;
    env.observations = observations;
    env.actions = actions;
    env.rewards = rewards;
    env.terminals = terminals;
    env.rng = test_seed + 4;

    int episodes = 200;
    int checked = 0;
    for (int ep = 0; ep < episodes; ep++) {
        c_reset(&env);
        for (int t = 0; t <= WORDLE_MAX_GUESSES; t++) {
            int off = 0;
            for (int g = 0; g < WORDLE_MAX_GUESSES; g++) {
                for (int p = 0; p < WORDLE_WORD_LEN; p++) {
                    int slot_off = off + (g * WORDLE_WORD_LEN + p) * (WORDLE_ALPHABET + 1);
                    int set = 0;
                    for (int b = 0; b <= WORDLE_ALPHABET; b++) set += observations[slot_off + b];
                    if (set != 1) TEST_FAIL("guess letter slot g=%d p=%d has %d bits", g, p, set);
                }
            }
            off += WORDLE_OBS_GUESS_LETTERS;
            for (int g = 0; g < WORDLE_MAX_GUESSES; g++) {
                for (int p = 0; p < WORDLE_WORD_LEN; p++) {
                    int slot_off = off + (g * WORDLE_WORD_LEN + p) * WORDLE_NUM_FB;
                    int set = 0;
                    for (int b = 0; b < WORDLE_NUM_FB; b++) set += observations[slot_off + b];
                    if (set != 1) TEST_FAIL("feedback slot g=%d p=%d has %d bits", g, p, set);
                }
            }
            off += WORDLE_OBS_FEEDBACK;
            int turn_set = 0;
            for (int b = 0; b < WORDLE_MAX_GUESSES + 1; b++) turn_set += observations[off + b];
            if (turn_set != 1) TEST_FAIL("turn one-hot has %d bits at t=%d", turn_set, t);

            checked++;
            if (env.terminals[0] != 0.0f) break;
            env.actions[0] = (float)rand_word_id();
            c_step(&env);
        }
    }
    TEST_PASS(checked);
}

static void test_obs_matches_recompute(void) {
    Wordle env = (Wordle){0};
    float observations[WORDLE_OBS_TOTAL];
    float shadow[WORDLE_OBS_TOTAL];
    float actions[1] = {0}, rewards[1] = {0}, terminals[1] = {0};
    env.num_agents = 1;
    env.observations = observations;
    env.actions = actions;
    env.rewards = rewards;
    env.terminals = terminals;
    env.rng = test_seed + 5;

    int episodes = 500;
    int checked = 0;
    for (int ep = 0; ep < episodes; ep++) {
        c_reset(&env);
        for (int t = 0; t < WORDLE_MAX_GUESSES; t++) {
            float* saved = env.observations;
            env.observations = shadow;
            compute_observations(&env);
            env.observations = saved;
            for (int i = 0; i < WORDLE_OBS_TOTAL; i++) {
                if (observations[i] != shadow[i]) {
                    TEST_FAIL("ep %d turn %d: obs[%d]=%f shadow[%d]=%f",
                              ep, t, i, (double)observations[i], i, (double)shadow[i]);
                }
            }
            checked++;
            env.actions[0] = (float)rand_word_id();
            c_step(&env);
            if (env.terminals[0] != 0.0f) break;
        }
    }
    TEST_PASS(checked);
}

static void test_mask_matches_candidate_set(void) {
    Wordle env = (Wordle){0};
    float observations[WORDLE_OBS_TOTAL];
    float actions[1] = {0}, rewards[1] = {0}, terminals[1] = {0};
    env.num_agents = 1;
    env.observations = observations;
    env.actions = actions;
    env.rewards = rewards;
    env.terminals = terminals;
    env.rng = test_seed + 6;

    int episodes = 300;
    int checked = 0;
    for (int ep = 0; ep < episodes; ep++) {
        c_reset(&env);
        for (int t = 0; t < WORDLE_MAX_GUESSES; t++) {
            float* mask = observations + WORDLE_OBS_SIZE;
            int set_count = 0;
            for (int w = 0; w < WORDLE_NUM_WORDS; w++) {
                bool is_set = mask[w] >= 0.5f;
                bool should_be = wordle_word_satisfies_constraints(&env, w);
                if (is_set != should_be) {
                    TEST_FAIL("ep %d turn %d word %d: mask=%f satisfies=%d",
                              ep, t, w, (double)mask[w], (int)should_be);
                }
                if (is_set) set_count++;
            }
            if (set_count != env.candidate_count) {
                TEST_FAIL("ep %d turn %d: mask sum %d != candidate_count %d",
                          ep, t, set_count, env.candidate_count);
            }
            checked++;
            env.actions[0] = (float)rand_word_id();
            c_step(&env);
            if (env.terminals[0] != 0.0f) break;
        }
    }
    TEST_PASS(checked);
}

int main(void) {
    test_feedback_self_all_green();
    test_feedback_letter_count_invariant();
    test_feedback_disjoint_all_gray();
    test_target_always_satisfies();
    test_recount_monotone_and_idempotent();
    test_solve_yields_reward_one();
    test_max_guesses_terminates();
    test_obs_size_consistent();
    test_obs_one_hot_invariants();
    test_obs_matches_recompute();
    test_mask_matches_candidate_set();

    if (g_failures > 0) {
        fprintf(stderr, "\n%d FAILURES\n", g_failures);
        return 1;
    }
    printf("\nALL TESTS PASSED\n");
    return 0;
}
