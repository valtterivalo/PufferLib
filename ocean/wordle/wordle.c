/* Standalone Wordle demo.
 *
 * Modes:
 *   TAB toggles between MANUAL and AUTO.
 *   In MANUAL, type a-z, BACKSPACE deletes, ENTER submits if the typed word
 *     is in the word list.
 *   In AUTO, a heuristic agent picks a uniformly random candidate from the
 *     remaining valid set every press of SPACE (or step-per-frame if held).
 *
 * Other keys:
 *   R toggles the target reveal in the side panel.
 *   ESC quits.
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
    bool pending_submit;
    int hold_frames_after_terminal;
} DemoState;

static int find_word_id(const char* lower, int len) {
    if (len != WORDLE_WORD_LEN) return -1;
    unsigned char encoded[WORDLE_WORD_LEN];
    for (int i = 0; i < WORDLE_WORD_LEN; i++) {
        char ch = lower[i];
        if (ch < 'a' || ch > 'z') return -1;
        encoded[i] = (unsigned char)(ch - 'a');
    }
    int lo = 0;
    int hi = WORDLE_NUM_WORDS - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int cmp = memcmp(WORDLE_WORDS[mid], encoded, WORDLE_WORD_LEN);
        if (cmp == 0) return mid;
        if (cmp < 0) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

static int pick_random_candidate(Wordle* env) {
    int n = env->candidate_count;
    if (n <= 0) return rand_r(&env->rng) % WORDLE_NUM_WORDS;
    int target = (int)(rand_r(&env->rng) % (unsigned int)n);
    int seen = 0;
    for (int i = 0; i < WORDLE_NUM_WORDS; i++) {
        if (env->candidate_mask[i]) {
            if (seen == target) return i;
            seen++;
        }
    }
    return rand_r(&env->rng) % WORDLE_NUM_WORDS;
}

static void demo_set_status(DemoState* st, const char* msg) {
    snprintf(st->status, sizeof(st->status), "%s", msg);
    st->status_until = (float)GetTime() + 1.5f;
}

static void demo_capture_typing(DemoState* st) {
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
        st->typed_len--;
        st->typed[st->typed_len] = 0;
    }
    if (IsKeyPressed(KEY_ENTER) && st->typed_len == WORDLE_WORD_LEN) {
        st->pending_submit = true;
    }
}

static void demo_overlay(Wordle* env, DemoState* st) {
    int hud_y = WORDLE_RENDER_TOP + WORDLE_RENDER_BOARD_H + 36;
    char buf[160];
    const char* mode_text = (st->mode == DEMO_AUTO) ? "AUTO  (TAB for manual)" : "MANUAL  (TAB for auto)";
    snprintf(buf, sizeof(buf), "Mode: %s", mode_text);
    DrawText(buf, WORDLE_RENDER_LEFT, hud_y, 18, WORDLE_COL_ACCENT);

    if (st->mode == DEMO_MANUAL) {
        snprintf(buf, sizeof(buf), "Typed: %-5s  (ENTER to submit)", st->typed);
        DrawText(buf, WORDLE_RENDER_LEFT + 360, hud_y, 18, WORDLE_COL_TEXT);
    } else {
        DrawText("SPACE: step  HOLD: continuous", WORDLE_RENDER_LEFT + 360, hud_y, 18, WORDLE_COL_DIM);
    }

    if (GetTime() < (double)st->status_until) {
        DrawText(st->status, WORDLE_RENDER_LEFT, hud_y + 28, 18, WORDLE_COL_BAD);
    }
}

int main(void) {
    Wordle env = (Wordle){0};
    env.num_agents = 1;
    env.reward_step = -0.01f;
    env.reward_info = 0.10f;
    env.reward_win = 1.0f;
    env.reward_fail = -0.20f;
    env.reward_repeat = -0.05f;
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

        bool stepped = false;
        if (st.hold_frames_after_terminal > 0) {
            st.hold_frames_after_terminal--;
        } else if (st.mode == DEMO_MANUAL) {
            demo_capture_typing(&st);
            if (st.pending_submit) {
                int wid = find_word_id(st.typed, st.typed_len);
                if (wid < 0) {
                    demo_set_status(&st, "Not in word list");
                } else {
                    env.actions[0] = (float)wid;
                    c_step(&env);
                    stepped = true;
                }
                st.pending_submit = false;
                st.typed_len = 0;
                st.typed[0] = 0;
            }
        } else {
            if (IsKeyPressed(KEY_SPACE) || IsKeyDown(KEY_SPACE)) {
                env.actions[0] = (float)pick_random_candidate(&env);
                c_step(&env);
                stepped = true;
            }
        }

        if (stepped && env.terminals[0] != 0.0f) {
            st.hold_frames_after_terminal = 30;
        }

        wordle_ensure_window(&env);
        if (IsKeyDown(KEY_ESCAPE)) break;
        BeginDrawing();
        wordle_draw_frame(&env);
        demo_overlay(&env, &st);
        EndDrawing();
    }

    c_close(&env);
    return 0;
}
