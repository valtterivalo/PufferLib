#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/phase2_curriculum.h"

static int g_pass = 0, g_fail = 0;

#define ASSERT_INT_EQ(label, got, want) do {                                \
    if ((int)(got) != (int)(want)) {                                        \
        fprintf(stderr, "FAIL: %s: got %d, want %d\n", (label),             \
            (int)(got), (int)(want));                                       \
        g_fail++;                                                           \
    } else g_pass++;                                                        \
} while (0)

#define ASSERT_TRUE(label, cond) do {                                       \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", (label)); g_fail++; }      \
    else g_pass++;                                                          \
} while (0)

static DemoStore* mk_store_with_demos(int n_demos, int length, int stride) {
    DemoStore* s = demostore_create(n_demos);
    for (int i = 0; i < n_demos; i++) {
        DemoTrajectory* d = &s->demos[s->num_demos++];
        d->demo_id = i;
        d->length_ticks = length;
        d->num_atns = 9;
        d->actions = (int*)calloc((size_t)length * 9, sizeof(int));
        d->cursor_tick = length - 1;
    }
    (void)stride;
    return s;
}

static DemoSnapshotLadder** mk_ladders(int n_demos, int length, int stride) {
    DemoSnapshotLadder** l = (DemoSnapshotLadder**)calloc((size_t)n_demos, sizeof(*l));
    int num = demo_snapshot_ladder_count_for_length(length, stride);
    for (int i = 0; i < n_demos; i++) {
        l[i] = demo_snapshot_ladder_create(i, stride, num, 64, 0);
        for (int s = 0; s < num; s++) l[i]->snapshot_ticks[s] = s * stride;
    }
    return l;
}

static void free_ladders(DemoSnapshotLadder** l, int n_demos) {
    for (int i = 0; i < n_demos; i++) demo_snapshot_ladder_destroy(l[i]);
    free(l);
}

static void test_create_and_destroy(void) {
    printf("--- phase2 ctx create/destroy ---\n");
    DemoStore* s = mk_store_with_demos(4, 80, 4);
    DemoSnapshotLadder** l = mk_ladders(4, 80, 4);
    Phase2Context* ctx = phase2_ctx_create(s, l, 256, 42);
    ASSERT_INT_EQ("num_envs", ctx->num_envs, 256);
    ASSERT_INT_EQ("active_pool_size = num_demos", ctx->active_pool_size, 4);
    ASSERT_INT_EQ("env_state[0] starts as normal", ctx->env_states[0].demo_id, -1);
    phase2_ctx_destroy(ctx);
    free_ladders(l, 4);
    demostore_destroy(s);
}

static void test_decide_reset_normal_only(void) {
    printf("--- phase2 normal_start_frac=1 always returns demo_id=-1 ---\n");
    DemoStore* s = mk_store_with_demos(4, 80, 4);
    DemoSnapshotLadder** l = mk_ladders(4, 80, 4);
    Phase2Context* ctx = phase2_ctx_create(s, l, 1, 42);
    ctx->normal_start_frac = 1.0f;
    for (int i = 0; i < 100; i++) {
        Phase2ResetDecision d = phase2_decide_reset(ctx);
        if (d.demo_id != -1) { ASSERT_INT_EQ("normal_only", d.demo_id, -1); break; }
    }
    g_pass++;
    phase2_ctx_destroy(ctx);
    free_ladders(l, 4);
    demostore_destroy(s);
}

static void test_decide_reset_ladder_only(void) {
    printf("--- phase2 normal_start_frac=0 always returns valid demo ---\n");
    DemoStore* s = mk_store_with_demos(4, 80, 4);
    DemoSnapshotLadder** l = mk_ladders(4, 80, 4);
    Phase2Context* ctx = phase2_ctx_create(s, l, 1, 42);
    ctx->normal_start_frac = 0.0f;
    ctx->randomize_future_rng_frac = 0.0f;
    int valid = 0;
    for (int i = 0; i < 100; i++) {
        Phase2ResetDecision d = phase2_decide_reset(ctx);
        if (d.demo_id < 0 || d.demo_id >= 4) break;
        if (d.slot < 0 || d.slot >= l[d.demo_id]->num_snapshots) break;
        if (d.randomize_rng != 0) break;
        valid++;
    }
    ASSERT_INT_EQ("100 valid ladder decisions", valid, 100);
    phase2_ctx_destroy(ctx);
    free_ladders(l, 4);
    demostore_destroy(s);
}

static void test_decide_reset_randomize_rng(void) {
    printf("--- phase2 randomize_future_rng_frac drives randomize flag ---\n");
    DemoStore* s = mk_store_with_demos(4, 80, 4);
    DemoSnapshotLadder** l = mk_ladders(4, 80, 4);
    Phase2Context* ctx = phase2_ctx_create(s, l, 1, 42);
    ctx->normal_start_frac = 0.0f;
    ctx->randomize_future_rng_frac = 1.0f;
    int randomized = 0;
    for (int i = 0; i < 100; i++) {
        Phase2ResetDecision d = phase2_decide_reset(ctx);
        if (d.randomize_rng) randomized++;
    }
    ASSERT_INT_EQ("100 random rng decisions", randomized, 100);
    phase2_ctx_destroy(ctx);
    free_ladders(l, 4);
    demostore_destroy(s);
}

static void test_decide_reset_mix_around_target(void) {
    printf("--- phase2 mix at 0.5 gives both kinds of decision ---\n");
    DemoStore* s = mk_store_with_demos(4, 80, 4);
    DemoSnapshotLadder** l = mk_ladders(4, 80, 4);
    Phase2Context* ctx = phase2_ctx_create(s, l, 1, 42);
    ctx->normal_start_frac = 0.5f;
    int normal = 0, ladder = 0;
    for (int i = 0; i < 1000; i++) {
        Phase2ResetDecision d = phase2_decide_reset(ctx);
        if (d.demo_id < 0) normal++;
        else ladder++;
    }
    ASSERT_TRUE("normal ~ 500", normal > 350 && normal < 650);
    ASSERT_TRUE("ladder ~ 500", ladder > 350 && ladder < 650);
    phase2_ctx_destroy(ctx);
    free_ladders(l, 4);
    demostore_destroy(s);
}

static void test_decide_reset_slot_jitter_bounds(void) {
    printf("--- phase2 slot jitter respects ladder bounds ---\n");
    DemoStore* s = mk_store_with_demos(1, 80, 4);
    DemoSnapshotLadder** l = mk_ladders(1, 80, 4);
    s->demos[0].cursor_tick = 0;
    Phase2Context* ctx = phase2_ctx_create(s, l, 1, 42);
    ctx->normal_start_frac = 0.0f;
    int n_snap = l[0]->num_snapshots;
    for (int i = 0; i < 200; i++) {
        Phase2ResetDecision d = phase2_decide_reset(ctx);
        ASSERT_TRUE("slot in bounds", d.slot >= 0 && d.slot < n_snap);
    }
    s->demos[0].cursor_tick = 79;
    for (int i = 0; i < 200; i++) {
        Phase2ResetDecision d = phase2_decide_reset(ctx);
        ASSERT_TRUE("slot in bounds at end", d.slot >= 0 && d.slot < n_snap);
    }
    phase2_ctx_destroy(ctx);
    free_ladders(l, 1);
    demostore_destroy(s);
}

int main(void) {
    test_create_and_destroy();
    test_decide_reset_normal_only();
    test_decide_reset_ladder_only();
    test_decide_reset_randomize_rng();
    test_decide_reset_mix_around_target();
    test_decide_reset_slot_jitter_bounds();
    printf("\n%d/%d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail == 0 ? 0 : 1;
}
