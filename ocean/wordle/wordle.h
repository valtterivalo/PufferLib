/* Wordle env for PufferLib Ocean.
 *
 * Single-agent. One hidden 5-letter target chosen from a fixed word list.
 * The agent has MAX_GUESSES turns to guess the target. After each guess the
 * env reveals per-position feedback (green/yellow/gray) using the standard
 * Wordle duplicate-letter rule.
 *
 * Action: Discrete(WORDLE_NUM_WORDS) - index into the precompiled word table.
 * Observation: ByteTensor of OBS_SIZE binary features. Layout in compute_observations.
 * Reward: -step + info_gain + win - loss - repeat (see hyperparams).
 *
 * The full state of the game (history, derived constraints, candidate set)
 * is recomputed incrementally on each step so that compute_observations and
 * the renderer can read it cheaply.
 */

#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <stdbool.h>
#include "raylib.h"

#include "wordle_words.h"

#define WORDLE_MAX_GUESSES 6
#define WORDLE_ALPHABET 26
#define WORDLE_NUM_FB 4
#define WORDLE_FB_UNKNOWN 0
#define WORDLE_FB_GRAY 1
#define WORDLE_FB_YELLOW 2
#define WORDLE_FB_GREEN 3

#define WORDLE_LETTER_UNKNOWN 0
#define WORDLE_LETTER_ABSENT 1
#define WORDLE_LETTER_PRESENT 2
#define WORDLE_LETTER_EXACT 3
#define WORDLE_NUM_LETTER_STATES 4

#define WORDLE_BUCKETS 16
#define WORDLE_MAX_COUNT_UNKNOWN (WORDLE_WORD_LEN + 1)

/* Observation layout offsets - keep in sync with WORDLE_OBS_SIZE. */
#define WORDLE_OBS_GUESS_LETTERS  (WORDLE_MAX_GUESSES * WORDLE_WORD_LEN * (WORDLE_ALPHABET + 1))
#define WORDLE_OBS_FEEDBACK       (WORDLE_MAX_GUESSES * WORDLE_WORD_LEN * WORDLE_NUM_FB)
#define WORDLE_OBS_TURN           (WORDLE_MAX_GUESSES + 1)
#define WORDLE_OBS_GREEN_POS      (WORDLE_WORD_LEN * WORDLE_ALPHABET)
#define WORDLE_OBS_FORBIDDEN_POS  (WORDLE_WORD_LEN * WORDLE_ALPHABET)
#define WORDLE_OBS_MIN_COUNT      (WORDLE_ALPHABET * (WORDLE_WORD_LEN + 1))
#define WORDLE_OBS_MAX_COUNT      (WORDLE_ALPHABET * (WORDLE_WORD_LEN + 2))
#define WORDLE_OBS_LETTER_STATE   (WORDLE_ALPHABET * WORDLE_NUM_LETTER_STATES)
#define WORDLE_OBS_REMAINING      (WORDLE_BUCKETS)

#define WORDLE_OBS_SIZE ( \
    WORDLE_OBS_GUESS_LETTERS \
    + WORDLE_OBS_FEEDBACK \
    + WORDLE_OBS_TURN \
    + WORDLE_OBS_GREEN_POS \
    + WORDLE_OBS_FORBIDDEN_POS \
    + WORDLE_OBS_MIN_COUNT \
    + WORDLE_OBS_MAX_COUNT \
    + WORDLE_OBS_LETTER_STATE \
    + WORDLE_OBS_REMAINING \
)

typedef struct Log {
    float perf;
    float score;
    float episode_return;
    float episode_length;
    float win_rate;
    float guesses;
    float repeat_rate;
    float info_bits;
    float final_log2_candidates;
    float mean_greens;
    float mean_yellows;
    float n;
} Log;

typedef struct Client Client;

typedef struct Wordle {
    Log log;
    unsigned char* observations;
    float* actions;
    float* rewards;
    float* terminals;
    int num_agents;

    float reward_step;
    float reward_info;
    float reward_win;
    float reward_fail;
    float reward_repeat;

    int target_id;
    int turn;
    bool solved;

    unsigned char guesses[WORDLE_MAX_GUESSES][WORDLE_WORD_LEN];
    unsigned char feedback[WORDLE_MAX_GUESSES][WORDLE_WORD_LEN];
    int guess_ids[WORDLE_MAX_GUESSES];

    unsigned char green_pos[WORDLE_WORD_LEN][WORDLE_ALPHABET];
    unsigned char forbidden_pos[WORDLE_WORD_LEN][WORDLE_ALPHABET];
    unsigned char min_count[WORDLE_ALPHABET];
    unsigned char max_count[WORDLE_ALPHABET];
    unsigned char letter_state[WORDLE_ALPHABET];

    unsigned char candidate_mask[WORDLE_NUM_WORDS];
    int candidate_count;
    int prev_candidate_count;

    int total_greens;
    int total_yellows;
    int repeats_in_episode;
    float info_bits_total;
    float episode_reward;

    float last_info_bits;
    int last_pattern;
    bool last_was_repeat;

    Client* client;
    unsigned int rng;
} Wordle;

void c_reset(Wordle* env);
void c_step(Wordle* env);
void c_render(Wordle* env);
void c_close(Wordle* env);

static inline float wordle_log2(int x) {
    return (x <= 0) ? 0.0f : log2f((float)x);
}

static inline void wordle_compute_feedback(
    const unsigned char* target,
    const unsigned char* guess,
    unsigned char* fb_out
) {
    int counts[WORDLE_ALPHABET] = {0};
    for (int i = 0; i < WORDLE_WORD_LEN; i++) {
        counts[target[i]]++;
    }
    for (int i = 0; i < WORDLE_WORD_LEN; i++) {
        if (guess[i] == target[i]) {
            fb_out[i] = WORDLE_FB_GREEN;
            counts[guess[i]]--;
        } else {
            fb_out[i] = WORDLE_FB_GRAY;
        }
    }
    for (int i = 0; i < WORDLE_WORD_LEN; i++) {
        if (fb_out[i] == WORDLE_FB_GREEN) continue;
        if (counts[guess[i]] > 0) {
            fb_out[i] = WORDLE_FB_YELLOW;
            counts[guess[i]]--;
        }
    }
}

static inline int wordle_pack_pattern(const unsigned char* fb) {
    int p = 0;
    for (int i = WORDLE_WORD_LEN - 1; i >= 0; i--) {
        unsigned char c = fb[i];
        int t = (c == WORDLE_FB_GREEN) ? 2 : (c == WORDLE_FB_YELLOW) ? 1 : 0;
        p = p * 3 + t;
    }
    return p;
}

static inline void wordle_init_constraints(Wordle* env) {
    memset(env->green_pos, 0, sizeof(env->green_pos));
    memset(env->forbidden_pos, 0, sizeof(env->forbidden_pos));
    memset(env->min_count, 0, sizeof(env->min_count));
    for (int c = 0; c < WORDLE_ALPHABET; c++) {
        env->max_count[c] = WORDLE_MAX_COUNT_UNKNOWN;
        env->letter_state[c] = WORDLE_LETTER_UNKNOWN;
    }
}

static inline void wordle_update_letter_states(Wordle* env) {
    for (int c = 0; c < WORDLE_ALPHABET; c++) {
        unsigned char mn = env->min_count[c];
        unsigned char mx = env->max_count[c];
        if (mx == 0) {
            env->letter_state[c] = WORDLE_LETTER_ABSENT;
        } else if (mn > 0 && mx != WORDLE_MAX_COUNT_UNKNOWN && mn == mx) {
            env->letter_state[c] = WORDLE_LETTER_EXACT;
        } else if (mn > 0) {
            env->letter_state[c] = WORDLE_LETTER_PRESENT;
        } else {
            env->letter_state[c] = WORDLE_LETTER_UNKNOWN;
        }
    }
}

static inline void wordle_apply_constraints(
    Wordle* env,
    const unsigned char* guess,
    const unsigned char* fb
) {
    int colored[WORDLE_ALPHABET] = {0};
    int gray[WORDLE_ALPHABET] = {0};
    for (int i = 0; i < WORDLE_WORD_LEN; i++) {
        unsigned char c = guess[i];
        if (fb[i] == WORDLE_FB_GREEN) {
            env->green_pos[i][c] = 1;
            colored[c]++;
        } else if (fb[i] == WORDLE_FB_YELLOW) {
            env->forbidden_pos[i][c] = 1;
            colored[c]++;
        } else {
            env->forbidden_pos[i][c] = 1;
            gray[c]++;
        }
    }
    for (int c = 0; c < WORDLE_ALPHABET; c++) {
        if (colored[c] > env->min_count[c]) {
            env->min_count[c] = (unsigned char)colored[c];
        }
        if (gray[c] > 0) {
            unsigned char limit = (unsigned char)colored[c];
            if (limit < env->max_count[c] || env->max_count[c] == WORDLE_MAX_COUNT_UNKNOWN) {
                env->max_count[c] = limit;
            }
        }
    }
    wordle_update_letter_states(env);
}

static inline bool wordle_word_satisfies_constraints(const Wordle* env, int word_id) {
    const unsigned char* w = WORDLE_WORDS[word_id];
    for (int i = 0; i < WORDLE_WORD_LEN; i++) {
        unsigned char c = w[i];
        if (env->forbidden_pos[i][c]) return false;
        for (int g = 0; g < WORDLE_ALPHABET; g++) {
            if (env->green_pos[i][g] && g != c) return false;
        }
    }
    int counts[WORDLE_ALPHABET] = {0};
    for (int i = 0; i < WORDLE_WORD_LEN; i++) counts[w[i]]++;
    for (int c = 0; c < WORDLE_ALPHABET; c++) {
        if (counts[c] < env->min_count[c]) return false;
        if (env->max_count[c] != WORDLE_MAX_COUNT_UNKNOWN
            && counts[c] > env->max_count[c]) return false;
    }
    return true;
}

static inline int wordle_recount_candidates(Wordle* env) {
    int n = 0;
    for (int i = 0; i < WORDLE_NUM_WORDS; i++) {
        bool ok = wordle_word_satisfies_constraints(env, i);
        env->candidate_mask[i] = ok ? 1 : 0;
        n += (int)ok;
    }
    env->candidate_count = n;
    return n;
}

static inline int wordle_log_bucket(int n) {
    if (n <= 0) return 0;
    float lg = log2f((float)n);
    int b = (int)lg;
    if (b < 0) b = 0;
    if (b >= WORDLE_BUCKETS) b = WORDLE_BUCKETS - 1;
    return b;
}

void compute_observations(Wordle* env) {
    unsigned char* o = env->observations;
    memset(o, 0, WORDLE_OBS_SIZE);
    int off = 0;

    for (int g = 0; g < WORDLE_MAX_GUESSES; g++) {
        for (int p = 0; p < WORDLE_WORD_LEN; p++) {
            int slot_off = off + (g * WORDLE_WORD_LEN + p) * (WORDLE_ALPHABET + 1);
            if (g < env->turn) {
                o[slot_off + env->guesses[g][p]] = 1;
            } else {
                o[slot_off + WORDLE_ALPHABET] = 1;
            }
        }
    }
    off += WORDLE_OBS_GUESS_LETTERS;

    for (int g = 0; g < WORDLE_MAX_GUESSES; g++) {
        for (int p = 0; p < WORDLE_WORD_LEN; p++) {
            int slot_off = off + (g * WORDLE_WORD_LEN + p) * WORDLE_NUM_FB;
            unsigned char fb = (g < env->turn) ? env->feedback[g][p] : WORDLE_FB_UNKNOWN;
            o[slot_off + fb] = 1;
        }
    }
    off += WORDLE_OBS_FEEDBACK;

    int turn_idx = env->turn;
    if (turn_idx > WORDLE_MAX_GUESSES) turn_idx = WORDLE_MAX_GUESSES;
    o[off + turn_idx] = 1;
    off += WORDLE_OBS_TURN;

    for (int p = 0; p < WORDLE_WORD_LEN; p++) {
        for (int c = 0; c < WORDLE_ALPHABET; c++) {
            o[off + p * WORDLE_ALPHABET + c] = env->green_pos[p][c];
        }
    }
    off += WORDLE_OBS_GREEN_POS;

    for (int p = 0; p < WORDLE_WORD_LEN; p++) {
        for (int c = 0; c < WORDLE_ALPHABET; c++) {
            o[off + p * WORDLE_ALPHABET + c] = env->forbidden_pos[p][c];
        }
    }
    off += WORDLE_OBS_FORBIDDEN_POS;

    for (int c = 0; c < WORDLE_ALPHABET; c++) {
        unsigned char mn = env->min_count[c];
        if (mn > WORDLE_WORD_LEN) mn = WORDLE_WORD_LEN;
        o[off + c * (WORDLE_WORD_LEN + 1) + mn] = 1;
    }
    off += WORDLE_OBS_MIN_COUNT;

    for (int c = 0; c < WORDLE_ALPHABET; c++) {
        unsigned char mx = env->max_count[c];
        int idx = (mx == WORDLE_MAX_COUNT_UNKNOWN) ? (WORDLE_WORD_LEN + 1) : mx;
        o[off + c * (WORDLE_WORD_LEN + 2) + idx] = 1;
    }
    off += WORDLE_OBS_MAX_COUNT;

    for (int c = 0; c < WORDLE_ALPHABET; c++) {
        o[off + c * WORDLE_NUM_LETTER_STATES + env->letter_state[c]] = 1;
    }
    off += WORDLE_OBS_LETTER_STATE;

    int bucket = wordle_log_bucket(env->candidate_count);
    o[off + bucket] = 1;
    off += WORDLE_OBS_REMAINING;

    assert(off == WORDLE_OBS_SIZE);
}

static inline void wordle_pick_target(Wordle* env) {
    env->target_id = (int)(rand_r(&env->rng) % WORDLE_NUM_WORDS);
}

void c_reset(Wordle* env) {
    wordle_pick_target(env);
    env->turn = 0;
    env->solved = false;
    memset(env->guesses, 0, sizeof(env->guesses));
    memset(env->feedback, WORDLE_FB_UNKNOWN, sizeof(env->feedback));
    for (int g = 0; g < WORDLE_MAX_GUESSES; g++) env->guess_ids[g] = -1;

    wordle_init_constraints(env);
    memset(env->candidate_mask, 1, sizeof(env->candidate_mask));
    env->candidate_count = WORDLE_NUM_WORDS;
    env->prev_candidate_count = WORDLE_NUM_WORDS;

    env->total_greens = 0;
    env->total_yellows = 0;
    env->repeats_in_episode = 0;
    env->info_bits_total = 0.0f;
    env->episode_reward = 0.0f;

    env->last_info_bits = 0.0f;
    env->last_pattern = 0;
    env->last_was_repeat = false;

    compute_observations(env);
}

static inline void wordle_log_episode(Wordle* env) {
    int guesses_used = env->turn;
    if (guesses_used <= 0) guesses_used = 1;

    float win = env->solved ? 1.0f : 0.0f;
    float score = env->solved ? (float)(WORDLE_MAX_GUESSES + 1 - env->turn) : 0.0f;
    float repeat_rate = (float)env->repeats_in_episode / (float)guesses_used;
    float mean_greens = (float)env->total_greens / (float)guesses_used;
    float mean_yellows = (float)env->total_yellows / (float)guesses_used;
    float final_log2 = env->solved ? 0.0f : wordle_log2(env->candidate_count);

    env->log.perf += win;
    env->log.score += score;
    env->log.episode_return += env->episode_reward;
    env->log.episode_length += (float)env->turn;
    env->log.win_rate += win;
    env->log.guesses += (float)env->turn;
    env->log.repeat_rate += repeat_rate;
    env->log.info_bits += env->info_bits_total;
    env->log.final_log2_candidates += final_log2;
    env->log.mean_greens += mean_greens;
    env->log.mean_yellows += mean_yellows;
    env->log.n += 1.0f;
}

void c_step(Wordle* env) {
    int action = (int)env->actions[0];
    assert(action >= 0 && action < WORDLE_NUM_WORDS);

    bool is_repeat = false;
    for (int g = 0; g < env->turn; g++) {
        if (env->guess_ids[g] == action) { is_repeat = true; break; }
    }

    const unsigned char* target = WORDLE_WORDS[env->target_id];
    const unsigned char* guess = WORDLE_WORDS[action];
    unsigned char fb[WORDLE_WORD_LEN];
    wordle_compute_feedback(target, guess, fb);

    int slot = env->turn;
    memcpy(env->guesses[slot], guess, WORDLE_WORD_LEN);
    memcpy(env->feedback[slot], fb, WORDLE_WORD_LEN);
    env->guess_ids[slot] = action;

    int greens = 0;
    int yellows = 0;
    for (int i = 0; i < WORDLE_WORD_LEN; i++) {
        if (fb[i] == WORDLE_FB_GREEN) greens++;
        else if (fb[i] == WORDLE_FB_YELLOW) yellows++;
    }
    env->total_greens += greens;
    env->total_yellows += yellows;

    wordle_apply_constraints(env, guess, fb);
    env->prev_candidate_count = env->candidate_count;
    wordle_recount_candidates(env);

    float prev_log = wordle_log2(env->prev_candidate_count);
    float new_log = wordle_log2(env->candidate_count);
    float info_bits = prev_log - new_log;
    if (info_bits < 0.0f) info_bits = 0.0f;
    env->info_bits_total += info_bits;
    env->last_info_bits = info_bits;
    env->last_pattern = wordle_pack_pattern(fb);
    env->last_was_repeat = is_repeat;

    float info_norm = wordle_log2(WORDLE_NUM_WORDS);
    if (info_norm <= 0.0f) info_norm = 1.0f;
    float reward = env->reward_step + env->reward_info * (info_bits / info_norm);
    if (is_repeat) {
        reward += env->reward_repeat;
        env->repeats_in_episode++;
    }

    env->turn++;
    env->solved = (greens == WORDLE_WORD_LEN);
    bool out_of_guesses = (env->turn >= WORDLE_MAX_GUESSES);

    if (env->solved) {
        reward += env->reward_win;
    } else if (out_of_guesses) {
        reward += env->reward_fail;
    }

    env->rewards[0] = reward;
    env->episode_reward += reward;
    env->terminals[0] = (env->solved || out_of_guesses) ? 1.0f : 0.0f;

    if (env->terminals[0]) {
        wordle_log_episode(env);
        c_reset(env);
        return;
    }

    compute_observations(env);
}

/* ------------------------------------------------------------------ */
/* Renderer                                                            */
/* ------------------------------------------------------------------ */

#define WORDLE_RENDER_TILE 64
#define WORDLE_RENDER_GAP 8
#define WORDLE_RENDER_LEFT 32
#define WORDLE_RENDER_TOP 96
#define WORDLE_RENDER_BOARD_W (WORDLE_WORD_LEN * (WORDLE_RENDER_TILE + WORDLE_RENDER_GAP))
#define WORDLE_RENDER_BOARD_H (WORDLE_MAX_GUESSES * (WORDLE_RENDER_TILE + WORDLE_RENDER_GAP))
#define WORDLE_RENDER_PANEL_W 360
#define WORDLE_RENDER_W (WORDLE_RENDER_LEFT * 2 + WORDLE_RENDER_BOARD_W + WORDLE_RENDER_PANEL_W)
#define WORDLE_RENDER_H (WORDLE_RENDER_TOP + WORDLE_RENDER_BOARD_H + 64)

struct Client {
    bool window_open;
    bool reveal_target;
};

static const Color WORDLE_COL_BG       = (Color){ 6, 24, 24, 255 };
static const Color WORDLE_COL_GRAY     = (Color){ 58, 58, 60, 255 };
static const Color WORDLE_COL_GREEN    = (Color){ 83, 141, 78, 255 };
static const Color WORDLE_COL_YELLOW   = (Color){ 181, 159, 59, 255 };
static const Color WORDLE_COL_BLANK    = (Color){ 18, 38, 38, 255 };
static const Color WORDLE_COL_BORDER   = (Color){ 50, 70, 70, 255 };
static const Color WORDLE_COL_TEXT     = (Color){ 230, 230, 230, 255 };
static const Color WORDLE_COL_DIM      = (Color){ 140, 160, 160, 255 };
static const Color WORDLE_COL_ACCENT   = (Color){ 0, 187, 187, 255 };
static const Color WORDLE_COL_BAD      = (Color){ 187, 0, 0, 255 };

static inline Color wordle_feedback_color(unsigned char fb) {
    switch (fb) {
        case WORDLE_FB_GREEN:  return WORDLE_COL_GREEN;
        case WORDLE_FB_YELLOW: return WORDLE_COL_YELLOW;
        case WORDLE_FB_GRAY:   return WORDLE_COL_GRAY;
        default:               return WORDLE_COL_BLANK;
    }
}

static inline void wordle_draw_letter_cell(int x, int y, Color fill, char letter) {
    DrawRectangle(x, y, WORDLE_RENDER_TILE, WORDLE_RENDER_TILE, fill);
    DrawRectangleLines(x, y, WORDLE_RENDER_TILE, WORDLE_RENDER_TILE, WORDLE_COL_BORDER);
    if (letter != 0) {
        char buf[2] = { letter, 0 };
        int fs = 40;
        int tw = MeasureText(buf, fs);
        DrawText(buf, x + (WORDLE_RENDER_TILE - tw) / 2,
                 y + (WORDLE_RENDER_TILE - fs) / 2, fs, WORDLE_COL_TEXT);
    }
}

static inline void wordle_ensure_window(Wordle* env) {
    if (env->client == NULL) {
        env->client = (Client*)calloc(1, sizeof(Client));
        env->client->reveal_target = true;
    }
    if (!env->client->window_open) {
        InitWindow(WORDLE_RENDER_W, WORDLE_RENDER_H, "PufferLib Wordle");
        SetTargetFPS(30);
        env->client->window_open = true;
    }
}

void wordle_draw_frame(Wordle* env) {
    Client* c = env->client;
    if (IsKeyPressed(KEY_R)) {
        c->reveal_target = !c->reveal_target;
    }

    ClearBackground(WORDLE_COL_BG);

    DrawText("WORDLE", WORDLE_RENDER_LEFT, 24, 48, WORDLE_COL_ACCENT);
    char turn_str[64];
    snprintf(turn_str, sizeof(turn_str), "Turn %d / %d", env->turn, WORDLE_MAX_GUESSES);
    DrawText(turn_str, WORDLE_RENDER_LEFT + 200, 36, 26, WORDLE_COL_DIM);

    int board_x = WORDLE_RENDER_LEFT;
    int board_y = WORDLE_RENDER_TOP;
    for (int g = 0; g < WORDLE_MAX_GUESSES; g++) {
        for (int p = 0; p < WORDLE_WORD_LEN; p++) {
            int x = board_x + p * (WORDLE_RENDER_TILE + WORDLE_RENDER_GAP);
            int y = board_y + g * (WORDLE_RENDER_TILE + WORDLE_RENDER_GAP);
            char letter = 0;
            Color color = WORDLE_COL_BLANK;
            if (g < env->turn) {
                letter = (char)('A' + env->guesses[g][p]);
                color = wordle_feedback_color(env->feedback[g][p]);
            }
            wordle_draw_letter_cell(x, y, color, letter);
        }
    }

    int panel_x = board_x + WORDLE_RENDER_BOARD_W + WORDLE_RENDER_LEFT;
    int panel_y = WORDLE_RENDER_TOP;
    int line_h = 24;

    char buf[128];
    if (c->reveal_target) {
        snprintf(buf, sizeof(buf), "Target: ");
        for (int i = 0; i < WORDLE_WORD_LEN; i++) {
            char letter[2] = { (char)('A' + WORDLE_WORDS[env->target_id][i]), 0 };
            int len = (int)strlen(buf);
            if (len + 2 < (int)sizeof(buf)) {
                buf[len] = letter[0];
                buf[len + 1] = 0;
            }
        }
        DrawText(buf, panel_x, panel_y, 22, WORDLE_COL_TEXT);
    } else {
        DrawText("Target hidden  (R toggles)", panel_x, panel_y, 22, WORDLE_COL_DIM);
    }
    panel_y += line_h + 4;

    snprintf(buf, sizeof(buf), "Candidates: %d", env->candidate_count);
    DrawText(buf, panel_x, panel_y, 20, WORDLE_COL_TEXT);
    panel_y += line_h;

    snprintf(buf, sizeof(buf), "Last info bits: %.2f", (double)env->last_info_bits);
    DrawText(buf, panel_x, panel_y, 20, WORDLE_COL_DIM);
    panel_y += line_h;

    snprintf(buf, sizeof(buf), "Total info: %.2f / %.2f",
             (double)env->info_bits_total, (double)wordle_log2(WORDLE_NUM_WORDS));
    DrawText(buf, panel_x, panel_y, 20, WORDLE_COL_DIM);
    panel_y += line_h;

    snprintf(buf, sizeof(buf), "Repeats: %d", env->repeats_in_episode);
    Color repeat_col = env->repeats_in_episode > 0 ? WORDLE_COL_BAD : WORDLE_COL_DIM;
    DrawText(buf, panel_x, panel_y, 20, repeat_col);
    panel_y += line_h;

    snprintf(buf, sizeof(buf), "Episode reward: %+.3f", (double)env->episode_reward);
    DrawText(buf, panel_x, panel_y, 20, WORDLE_COL_TEXT);
    panel_y += line_h + 8;

    DrawText("Letters:", panel_x, panel_y, 20, WORDLE_COL_ACCENT);
    panel_y += line_h;
    int lx = panel_x;
    int ly = panel_y;
    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < 13; col++) {
            int cidx = row * 13 + col;
            char letter[2] = { (char)('A' + cidx), 0 };
            Color cc = WORDLE_COL_DIM;
            switch (env->letter_state[cidx]) {
                case WORDLE_LETTER_ABSENT:  cc = WORDLE_COL_GRAY; break;
                case WORDLE_LETTER_PRESENT: cc = WORDLE_COL_YELLOW; break;
                case WORDLE_LETTER_EXACT:   cc = WORDLE_COL_GREEN; break;
                default: break;
            }
            DrawRectangle(lx + col * 22, ly + row * 28, 20, 24, cc);
            int tw = MeasureText(letter, 16);
            DrawText(letter, lx + col * 22 + (20 - tw) / 2, ly + row * 28 + 4, 16, WORDLE_COL_TEXT);
        }
    }
    panel_y += 28 * 2 + 8;

    if (env->last_was_repeat) {
        DrawText("REPEAT GUESS", panel_x, panel_y, 18, WORDLE_COL_BAD);
        panel_y += line_h;
    }

    int hud_y = WORDLE_RENDER_TOP + WORDLE_RENDER_BOARD_H + 16;
    snprintf(buf, sizeof(buf), "Episodes: %.0f  Wins: %.0f  Avg score: %.2f",
             (double)env->log.n, (double)env->log.win_rate,
             (double)(env->log.n > 0 ? env->log.score / env->log.n : 0.0f));
    DrawText(buf, WORDLE_RENDER_LEFT, hud_y, 18, WORDLE_COL_DIM);
}

void c_render(Wordle* env) {
    wordle_ensure_window(env);
    if (IsKeyDown(KEY_ESCAPE)) {
        exit(0);
    }
    BeginDrawing();
    wordle_draw_frame(env);
    EndDrawing();
}

void c_close(Wordle* env) {
    if (env->client != NULL) {
        if (env->client->window_open && IsWindowReady()) {
            CloseWindow();
        }
        free(env->client);
        env->client = NULL;
    }
}
