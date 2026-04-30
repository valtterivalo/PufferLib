/* Standalone Wordle demo.
 *
 * MANUAL: type a-z, BACKSPACE deletes, ENTER submits if the typed word is in
 * the word list. AUTO: SPACE picks a uniformly random remaining candidate.
 * TAB toggles modes. R toggles target reveal. ESC quits.
 */

#include "wordle.h"
#include <stdio.h>
#include <time.h>

typedef enum { DEMO_MANUAL = 0, DEMO_AUTO = 1 } DemoMode;

typedef struct DemoState {
    DemoMode mode;
    char typed[WORDLE_WORD_LEN + 1];
    int typed_len;
    char status[128];
    float status_until;
    int hold_frames_after_terminal;
} DemoState;

static int wordle_word_cmp(const void* key, const void* row) {
    return memcmp(key, row, WORDLE_WORD_LEN);
}

static int find_word_id(const char* lower, int len) {
    if (len != WORDLE_WORD_LEN) return -1;
    unsigned char encoded[WORDLE_WORD_LEN];
    for (int i = 0; i < WORDLE_WORD_LEN; i++) {
        char ch = lower[i];
        if (ch < 'a' || ch > 'z') return -1;
        encoded[i] = (unsigned char)(ch - 'a');
    }
    const unsigned char (*hit)[WORDLE_WORD_LEN] = bsearch(
        encoded, WORDLE_WORDS, WORDLE_NUM_WORDS, WORDLE_WORD_LEN, wordle_word_cmp);
    return hit ? (int)(hit - WORDLE_WORDS) : -1;
}

static int pick_random_candidate(Wordle* env) {
    if (env->candidate_count <= 0) return rand_r(&env->rng) % WORDLE_NUM_WORDS;
    int target = (int)(rand_r(&env->rng) % (unsigned int)env->candidate_count);
    return env->candidate_list[target];
}

static void demo_capture_typing(Wordle* env, DemoState* st) {
    int key = GetCharPressed();
    while (key > 0) {
        if (key >= 'A' && key <= 'Z') key += ('a' - 'A');
        if (key >= 'a' && key <= 'z' && st->typed_len < WORDLE_WORD_LEN) {
            st->typed[st->typed_len++] = (char)key;
            st->typed[st->typed_len] = 0;
        }
        key = GetCharPressed();
    }
    if (IsKeyPressed(KEY_BACKSPACE) && st->typed_len > 0) {
        st->typed[--st->typed_len] = 0;
    }
    if (IsKeyPressed(KEY_ENTER) && st->typed_len == WORDLE_WORD_LEN) {
        int wid = find_word_id(st->typed, st->typed_len);
        if (wid < 0) {
            snprintf(st->status, sizeof(st->status), "Not in word list");
            st->status_until = (float)GetTime() + 1.5f;
        } else {
            env->actions[0] = (float)wid;
            c_step(env);
            if (env->terminals[0] != 0.0f) st->hold_frames_after_terminal = 30;
        }
        st->typed_len = 0;
        st->typed[0] = 0;
    }
}

static void demo_overlay(DemoState* st) {
    int hud_y = WORDLE_RENDER_TOP + WORDLE_RENDER_BOARD_H + 36;
    const char* mode_text = (st->mode == DEMO_AUTO) ? "AUTO  (TAB for manual)" : "MANUAL  (TAB for auto)";
    char buf[128];
    snprintf(buf, sizeof(buf), "Mode: %s", mode_text);
    DrawText(buf, WORDLE_RENDER_LEFT, hud_y, 18, WORDLE_COL_ACCENT);
    if (st->mode == DEMO_MANUAL) {
        snprintf(buf, sizeof(buf), "Typed: %-5s  (ENTER to submit)", st->typed);
    } else {
        snprintf(buf, sizeof(buf), "SPACE: step  HOLD: continuous");
    }
    DrawText(buf, WORDLE_RENDER_LEFT + 360, hud_y, 18, WORDLE_COL_TEXT);
    if (GetTime() < (double)st->status_until) {
        DrawText(st->status, WORDLE_RENDER_LEFT, hud_y + 28, 18, WORDLE_COL_BAD);
    }
}

int main(void) {
    Wordle env = (Wordle){0};
    env.num_agents = 1;
    env.reward_candidate = 0.02f;
    env.rng = (unsigned int)time(NULL);

    unsigned char observations[WORDLE_OBS_SIZE];
    float actions[1] = { 0 };
    float rewards[1] = { 0 };
    float terminals[1] = { 0 };
    env.observations = observations;
    env.actions = actions;
    env.rewards = rewards;
    env.terminals = terminals;

    c_reset(&env);
    c_render(&env);

    DemoState st = (DemoState){0};
    st.mode = DEMO_MANUAL;

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_TAB)) {
            st.mode = (st.mode == DEMO_MANUAL) ? DEMO_AUTO : DEMO_MANUAL;
            st.typed_len = 0;
            st.typed[0] = 0;
        }
        if (st.hold_frames_after_terminal > 0) {
            st.hold_frames_after_terminal--;
        } else if (st.mode == DEMO_MANUAL) {
            demo_capture_typing(&env, &st);
        } else if (IsKeyPressed(KEY_SPACE) || IsKeyDown(KEY_SPACE)) {
            env.actions[0] = (float)pick_random_candidate(&env);
            c_step(&env);
            if (env.terminals[0] != 0.0f) st.hold_frames_after_terminal = 30;
        }

        wordle_ensure_window(&env);
        if (IsKeyDown(KEY_ESCAPE)) break;
        BeginDrawing();
        wordle_draw_frame(&env);
        demo_overlay(&st);
        EndDrawing();
    }

    c_close(&env);
    return 0;
}
