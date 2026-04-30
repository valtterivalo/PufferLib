/* Wordle env for PufferLib Ocean.
 *
 * Single-agent. One hidden 5-letter target chosen from a fixed word list.
 * The agent has MAX_GUESSES turns to guess the target. After each guess the
 * env reveals per-position feedback (green/yellow/gray) using the standard
 * Wordle duplicate-letter rule.
 *
 * Action: Discrete(WORDLE_NUM_WORDS) - index into the precompiled word table.
 * Observation: ByteTensor of OBS_SIZE binary features. Layout in compute_observations.
 * Reward: +1 on solve, plus reward_info * info_bits / log2(NUM_WORDS) per step.
 * Per-episode info bonus telescopes to log2(N0) - log2(final_count) and is bounded
 * by reward_info, keeping the win signal dominant.
 *
 * The full state of the game (history, derived constraints, candidate set)
 * is recomputed incrementally on each step so that compute_observations and
 * the renderer can read it cheaply.
 */

#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <stdbool.h>
#include "raylib.h"

#include "wordle_words.h"

#define WORDLE_MAX_GUESSES 6
#define WORDLE_ALPHABET 26

typedef enum {
    WORDLE_FB_UNKNOWN = 0,
    WORDLE_FB_GRAY    = 1,
    WORDLE_FB_YELLOW  = 2,
    WORDLE_FB_GREEN   = 3,
    WORDLE_NUM_FB     = 4,
} WordleFeedback;

typedef enum {
    WORDLE_LETTER_UNKNOWN     = 0,
    WORDLE_LETTER_ABSENT      = 1,
    WORDLE_LETTER_PRESENT     = 2,
    WORDLE_LETTER_EXACT       = 3,
    WORDLE_NUM_LETTER_STATES  = 4,
} WordleLetterState;

#define WORDLE_BUCKETS 16
#define WORDLE_MAX_COUNT_UNKNOWN (WORDLE_WORD_LEN + 1)

#define WORDLE_OBS_GUESS_LETTERS  (WORDLE_MAX_GUESSES * WORDLE_WORD_LEN * (WORDLE_ALPHABET + 1))
#define WORDLE_OBS_FEEDBACK       (WORDLE_MAX_GUESSES * WORDLE_WORD_LEN * WORDLE_NUM_FB)
#define WORDLE_OBS_TURN           (WORDLE_MAX_GUESSES + 1)
#define WORDLE_OBS_GREEN_POS      (WORDLE_WORD_LEN * WORDLE_ALPHABET)
#define WORDLE_OBS_FORBIDDEN_POS  (WORDLE_WORD_LEN * WORDLE_ALPHABET)
#define WORDLE_OBS_MIN_COUNT      (WORDLE_ALPHABET * (WORDLE_WORD_LEN + 1))
#define WORDLE_OBS_MAX_COUNT      (WORDLE_ALPHABET * (WORDLE_WORD_LEN + 2))
#define WORDLE_OBS_REMAINING      (WORDLE_BUCKETS)

#define WORDLE_OBS_SIZE ( \
    WORDLE_OBS_GUESS_LETTERS \
    + WORDLE_OBS_FEEDBACK \
    + WORDLE_OBS_TURN \
    + WORDLE_OBS_GREEN_POS \
    + WORDLE_OBS_FORBIDDEN_POS \
    + WORDLE_OBS_MIN_COUNT \
    + WORDLE_OBS_MAX_COUNT \
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

    float reward_info;

    int target_id;
    int turn;

    unsigned char guesses[WORDLE_MAX_GUESSES][WORDLE_WORD_LEN];
    unsigned char feedback[WORDLE_MAX_GUESSES][WORDLE_WORD_LEN];
    int guess_ids[WORDLE_MAX_GUESSES];

    unsigned char green_pos[WORDLE_WORD_LEN][WORDLE_ALPHABET];
    unsigned char forbidden_pos[WORDLE_WORD_LEN][WORDLE_ALPHABET];
    /* 0xFF = no green known at this position. */
    unsigned char green_letter[WORDLE_WORD_LEN];
    unsigned char min_count[WORDLE_ALPHABET];
    unsigned char max_count[WORDLE_ALPHABET];

    uint16_t candidate_list[WORDLE_NUM_WORDS];
    int candidate_count;

    int total_greens;
    int total_yellows;
    int repeats_in_episode;
    float episode_reward;

    Client* client;
    unsigned int rng;
} Wordle;

void c_reset(Wordle* env);
void c_step(Wordle* env);
void c_render(Wordle* env);
void c_close(Wordle* env);

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

static inline int wordle_letter_state(int mn, int mx) {
    if (mx == 0) return WORDLE_LETTER_ABSENT;
    if (mn == 0) return WORDLE_LETTER_UNKNOWN;
    if (mx != WORDLE_MAX_COUNT_UNKNOWN && mn == mx) return WORDLE_LETTER_EXACT;
    return WORDLE_LETTER_PRESENT;
}

static inline void wordle_init_constraints(Wordle* env) {
    memset(env->green_pos, 0, sizeof(env->green_pos));
    memset(env->forbidden_pos, 0, sizeof(env->forbidden_pos));
    memset(env->green_letter, 0xFF, sizeof(env->green_letter));
    memset(env->min_count, 0, sizeof(env->min_count));
    memset(env->max_count, WORDLE_MAX_COUNT_UNKNOWN, sizeof(env->max_count));
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
            env->green_letter[i] = c;
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
}

static inline bool wordle_word_satisfies_constraints(const Wordle* env, int word_id) {
    const unsigned char* w = WORDLE_WORDS[word_id];
    for (int i = 0; i < WORDLE_WORD_LEN; i++) {
        unsigned char c = w[i];
        unsigned char green = env->green_letter[i];
        if (green != 0xFF && c != green) return false;
        if (env->forbidden_pos[i][c]) return false;
    }
    unsigned char counts[WORDLE_ALPHABET] = {0};
    for (int i = 0; i < WORDLE_WORD_LEN; i++) counts[w[i]]++;
    for (int c = 0; c < WORDLE_ALPHABET; c++) {
        if (counts[c] < env->min_count[c]) return false;
        if (env->max_count[c] != WORDLE_MAX_COUNT_UNKNOWN
            && counts[c] > env->max_count[c]) return false;
    }
    return true;
}

static inline int wordle_recount_candidates(Wordle* env) {
    int write = 0;
    for (int read = 0; read < env->candidate_count; read++) {
        int i = env->candidate_list[read];
        if (wordle_word_satisfies_constraints(env, i)) {
            env->candidate_list[write++] = (uint16_t)i;
        }
    }
    env->candidate_count = write;
    return write;
}

static inline int wordle_remaining_bucket(int n) {
    int bucket = (n <= 1) ? 0 : 31 - __builtin_clz((unsigned int)n);
    return bucket >= WORDLE_BUCKETS ? WORDLE_BUCKETS - 1 : bucket;
}

void compute_observations(Wordle* env) {
    unsigned char* o = env->observations;
    memset(o, 0, WORDLE_OBS_SIZE);
    int off = 0;

    for (int g = 0; g < WORDLE_MAX_GUESSES; g++) {
        for (int p = 0; p < WORDLE_WORD_LEN; p++) {
            int slot_off = off + (g * WORDLE_WORD_LEN + p) * (WORDLE_ALPHABET + 1);
            int letter_idx = (g < env->turn) ? env->guesses[g][p] : WORDLE_ALPHABET;
            o[slot_off + letter_idx] = 1;
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

    o[off + env->turn] = 1;
    off += WORDLE_OBS_TURN;

    memcpy(o + off, env->green_pos, sizeof(env->green_pos));
    off += WORDLE_OBS_GREEN_POS;

    memcpy(o + off, env->forbidden_pos, sizeof(env->forbidden_pos));
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

    o[off + wordle_remaining_bucket(env->candidate_count)] = 1;
    off += WORDLE_OBS_REMAINING;

    assert(off == WORDLE_OBS_SIZE);
}

void c_reset(Wordle* env) {
    env->target_id = (int)(rand_r(&env->rng) % WORDLE_NUM_WORDS);
    env->turn = 0;
    memset(env->guesses, 0, sizeof(env->guesses));
    memset(env->feedback, WORDLE_FB_UNKNOWN, sizeof(env->feedback));
    for (int g = 0; g < WORDLE_MAX_GUESSES; g++) env->guess_ids[g] = -1;

    wordle_init_constraints(env);
    for (int i = 0; i < WORDLE_NUM_WORDS; i++) env->candidate_list[i] = (uint16_t)i;
    env->candidate_count = WORDLE_NUM_WORDS;

    env->total_greens = 0;
    env->total_yellows = 0;
    env->repeats_in_episode = 0;
    env->episode_reward = 0.0f;

    compute_observations(env);
}

void c_step(Wordle* env) {
    int action = (int)env->actions[0];
    assert(action >= 0 && action < WORDLE_NUM_WORDS);

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

    int prev_count = env->candidate_count;
    wordle_apply_constraints(env, guess, fb);
    wordle_recount_candidates(env);

    static const float info_norm = 11.176558f;  /* log2(WORDLE_NUM_WORDS = 2315) */
    float info_bits = log2f((float)prev_count) - log2f((float)env->candidate_count);
    if (info_bits < 0.0f) info_bits = 0.0f;

    if (is_repeat) env->repeats_in_episode++;

    env->turn++;
    bool solved = (greens == WORDLE_WORD_LEN);
    bool out_of_guesses = (env->turn >= WORDLE_MAX_GUESSES);
    float reward = (solved ? 1.0f : 0.0f) + env->reward_info * (info_bits / info_norm);
    env->rewards[0] = reward;
    env->episode_reward += reward;
    env->terminals[0] = (solved || out_of_guesses) ? 1.0f : 0.0f;

    if (env->terminals[0]) {
        float gu = (float)env->turn;
        float final_log2 = solved ? 0.0f : log2f((float)env->candidate_count);
        env->log.perf                  += solved ? 1.0f : 0.0f;
        env->log.score                 += solved ? (float)(WORDLE_MAX_GUESSES + 1 - env->turn) : 0.0f;
        env->log.episode_return        += env->episode_reward;
        env->log.episode_length        += gu;
        env->log.win_rate              += solved ? 1.0f : 0.0f;
        env->log.guesses               += gu;
        env->log.repeat_rate           += (float)env->repeats_in_episode / gu;
        env->log.info_bits             += log2f((float)WORDLE_NUM_WORDS) - final_log2;
        env->log.final_log2_candidates += final_log2;
        env->log.mean_greens           += (float)env->total_greens / gu;
        env->log.mean_yellows          += (float)env->total_yellows / gu;
        env->log.n                     += 1.0f;
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

static const Color WORDLE_LETTER_COL[WORDLE_NUM_LETTER_STATES] = {
    { 140, 160, 160, 255 },
    {  58,  58,  60, 255 },
    { 181, 159,  59, 255 },
    {  83, 141,  78, 255 },
};

static inline void wordle_panel_line(int x, int* y, int fs, Color col, const char* text) {
    DrawText(text, x, *y, fs, col);
    *y += fs + 4;
}

static inline void wordle_draw_letter_cell(int x, int y, Color fill, char letter) {
    DrawRectangle(x, y, WORDLE_RENDER_TILE, WORDLE_RENDER_TILE, fill);
    DrawRectangleLines(x, y, WORDLE_RENDER_TILE, WORDLE_RENDER_TILE, WORDLE_COL_BORDER);
    if (letter != 0) {
        char buf[2] = { letter, 0 };
        int tw = MeasureText(buf, 40);
        DrawText(buf, x + (WORDLE_RENDER_TILE - tw) / 2,
                 y + (WORDLE_RENDER_TILE - 40) / 2, 40, WORDLE_COL_TEXT);
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
    if (IsKeyPressed(KEY_R)) c->reveal_target = !c->reveal_target;

    ClearBackground(WORDLE_COL_BG);
    DrawText("WORDLE", WORDLE_RENDER_LEFT, 24, 48, WORDLE_COL_ACCENT);

    int panel_x = WORDLE_RENDER_LEFT + WORDLE_RENDER_BOARD_W + WORDLE_RENDER_LEFT;
    int top_y = 36;
    wordle_panel_line(WORDLE_RENDER_LEFT + 200, &top_y, 26, WORDLE_COL_DIM,
                      TextFormat("Turn %d / %d", env->turn, WORDLE_MAX_GUESSES));

    for (int g = 0; g < WORDLE_MAX_GUESSES; g++) {
        for (int p = 0; p < WORDLE_WORD_LEN; p++) {
            int x = WORDLE_RENDER_LEFT + p * (WORDLE_RENDER_TILE + WORDLE_RENDER_GAP);
            int y = WORDLE_RENDER_TOP + g * (WORDLE_RENDER_TILE + WORDLE_RENDER_GAP);
            char letter = (g < env->turn) ? (char)('A' + env->guesses[g][p]) : 0;
            Color color = (g < env->turn) ? wordle_feedback_color(env->feedback[g][p]) : WORDLE_COL_BLANK;
            wordle_draw_letter_cell(x, y, color, letter);
        }
    }

    int panel_y = WORDLE_RENDER_TOP;
    if (c->reveal_target) {
        const unsigned char* w = WORDLE_WORDS[env->target_id];
        wordle_panel_line(panel_x, &panel_y, 22, WORDLE_COL_TEXT,
            TextFormat("Target: %c%c%c%c%c", 'A'+w[0], 'A'+w[1], 'A'+w[2], 'A'+w[3], 'A'+w[4]));
    } else {
        wordle_panel_line(panel_x, &panel_y, 22, WORDLE_COL_DIM, "Target hidden  (R toggles)");
    }
    wordle_panel_line(panel_x, &panel_y, 20, WORDLE_COL_TEXT,
        TextFormat("Candidates: %d", env->candidate_count));
    wordle_panel_line(panel_x, &panel_y, 20,
        env->repeats_in_episode > 0 ? WORDLE_COL_BAD : WORDLE_COL_DIM,
        TextFormat("Repeats: %d", env->repeats_in_episode));
    panel_y += 8;

    DrawText("Letters:", panel_x, panel_y, 20, WORDLE_COL_ACCENT);
    panel_y += 28;
    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < 13; col++) {
            int cidx = row * 13 + col;
            int state = wordle_letter_state(env->min_count[cidx], env->max_count[cidx]);
            char letter[2] = { (char)('A' + cidx), 0 };
            int cell_x = panel_x + col * 22;
            int cell_y = panel_y + row * 28;
            DrawRectangle(cell_x, cell_y, 20, 24, WORDLE_LETTER_COL[state]);
            int tw = MeasureText(letter, 16);
            DrawText(letter, cell_x + (20 - tw) / 2, cell_y + 4, 16, WORDLE_COL_TEXT);
        }
    }

    int hud_y = WORDLE_RENDER_TOP + WORDLE_RENDER_BOARD_H + 16;
    float avg_score = env->log.n > 0 ? env->log.score / env->log.n : 0.0f;
    DrawText(
        TextFormat("Episodes: %.0f  Wins: %.0f  Avg score: %.2f",
                   (double)env->log.n, (double)env->log.win_rate, (double)avg_score),
        WORDLE_RENDER_LEFT, hud_y, 18, WORDLE_COL_DIM);
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
