/* Random-in-candidates baseline for wordle.
 *
 * Plays N episodes where each turn picks uniformly from the current
 * candidate set (constraint-satisfying words). Reports win rate, avg
 * episode length, avg score. This is the "what does masking get you for
 * free" lower bound.
 *
 * Build:
 *   clang -O2 -DNDEBUG -DPLATFORM_DESKTOP -I./raylib-5.5_macos/include -I./src \
 *         ocean/wordle/wordle_baseline.c -o wordle_baseline \
 *         raylib-5.5_macos/lib/libraylib.a -lm \
 *         -framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL
 *
 * Run:
 *   ./wordle_baseline           # default 100000 episodes
 *   ./wordle_baseline 50000     # custom episode count
 */

#include "wordle.h"
#include <stdio.h>
#include <time.h>

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

int main(int argc, char** argv) {
    int episodes = (argc >= 2) ? atoi(argv[1]) : 100000;

    Wordle env = (Wordle){0};
    float observations[WORDLE_OBS_TOTAL];
    float actions[1] = {0}, rewards[1] = {0}, terminals[1] = {0};
    env.num_agents = 1;
    env.observations = observations;
    env.actions = actions;
    env.rewards = rewards;
    env.terminals = terminals;
    env.reward_win = 1.0f;
    env.rng = 0xBA5E11;

    unsigned int play_seed = 0xBABEB011;

    int wins = 0;
    long total_turns = 0;
    float total_score = 0.0f;

    int turn_hist[WORDLE_MAX_GUESSES + 1] = {0};

    double t0 = now_ns();
    for (int ep = 0; ep < episodes; ep++) {
        c_reset(&env);
        bool solved = false;
        int final_turn = 0;
        for (int t = 0; t < WORDLE_MAX_GUESSES; t++) {
            int n = env.candidate_count;
            int pick_idx = (int)(rand_r(&play_seed) % (unsigned int)n);
            int word_id = env.candidate_list[pick_idx];
            env.actions[0] = (float)word_id;

            int target_was = env.target_id;
            c_step(&env);

            if (env.terminals[0] != 0.0f) {
                final_turn = (env.target_id != target_was) ? 0 : 0;
                /* env auto-resets on terminal; final_turn was the turn count
                 * just before reset. Use the score we just emitted. */
                if (word_id == target_was) {
                    solved = true;
                }
                final_turn = t + 1;
                break;
            }
        }

        if (solved) {
            wins++;
            float score = (float)(WORDLE_MAX_GUESSES + 1 - final_turn);
            total_score += score;
            turn_hist[final_turn]++;
        } else {
            turn_hist[0]++;
        }
        total_turns += final_turn;
    }
    double t1 = now_ns();

    double win_rate = (double)wins / (double)episodes;
    double avg_turns = (double)total_turns / (double)episodes;
    double avg_score = (double)total_score / (double)episodes;

    printf("=== random-in-candidates baseline ===\n");
    printf("episodes:        %d\n", episodes);
    printf("wall time:       %.2f s\n", (t1 - t0) / 1e9);
    printf("eps/sec:         %.0f\n", episodes / ((t1 - t0) / 1e9));
    printf("---\n");
    printf("win_rate:        %.4f  (%d / %d)\n", win_rate, wins, episodes);
    printf("avg episode len: %.3f\n", avg_turns);
    printf("avg score:       %.3f\n", avg_score);
    printf("---\n");
    printf("solve distribution:\n");
    printf("  fail (no solve in 6): %d  (%.2f%%)\n",
           turn_hist[0], 100.0 * turn_hist[0] / episodes);
    for (int t = 1; t <= WORDLE_MAX_GUESSES; t++) {
        printf("  solved on turn %d:     %d  (%.2f%%)\n",
               t, turn_hist[t], 100.0 * turn_hist[t] / episodes);
    }
    return 0;
}
