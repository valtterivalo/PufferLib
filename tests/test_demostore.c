#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "src/demostore.h"

static int g_pass = 0;
static int g_fail = 0;

#define ASSERT_INT_EQ(label, got, want)                                     \
    do {                                                                    \
        if ((int)(got) != (int)(want)) {                                    \
            fprintf(stderr, "FAIL: %s: got %d, want %d\n", (label),         \
                (int)(got), (int)(want));                                   \
            g_fail++;                                                       \
        } else { g_pass++; }                                                \
    } while (0)

#define ASSERT_FLOAT_NEAR(label, got, want, eps)                            \
    do {                                                                    \
        float _g = (float)(got);                                            \
        float _w = (float)(want);                                           \
        float _d = _g > _w ? _g - _w : _w - _g;                             \
        if (_d > (float)(eps)) {                                            \
            fprintf(stderr, "FAIL: %s: got %f, want %f (eps=%f)\n",         \
                (label), _g, _w, (float)(eps));                             \
            g_fail++;                                                       \
        } else { g_pass++; }                                                \
    } while (0)

#define ASSERT_TRUE(label, cond)                                            \
    do {                                                                    \
        if (!(cond)) {                                                      \
            fprintf(stderr, "FAIL: %s\n", (label));                         \
            g_fail++;                                                       \
        } else { g_pass++; }                                                \
    } while (0)

#define TEST_SNAPSHOT_SIZE 32
#define TEST_NUM_SNAPSHOTS 4

static int write_synthetic_demo(
    const char* path, int n_ticks, int num_atns, uint32_t rng_seed,
    int action_value
) {
    FILE* f = fopen(path, "wb");
    if (!f) return -1;
    Phase2DemoHeader h = {
        .magic = PHASE2_DEMO_MAGIC,
        .version = PHASE2_DEMO_VERSION,
        .num_ticks = n_ticks,
        .num_atns = num_atns,
        .rng_seed = rng_seed,
        .quality = 0.0f,
        .snapshot_size = TEST_SNAPSHOT_SIZE,
        .num_snapshots = TEST_NUM_SNAPSHOTS,
    };
    uint8_t snap_pool[TEST_NUM_SNAPSHOTS * TEST_SNAPSHOT_SIZE] = {0};
    int snap_ticks[TEST_NUM_SNAPSHOTS];
    for (int i = 0; i < TEST_NUM_SNAPSHOTS; i++) {
        snap_ticks[i] = (i * n_ticks) / TEST_NUM_SNAPSHOTS;
    }
    int* actions = (int*)malloc((size_t)n_ticks * (size_t)num_atns * sizeof(int));
    for (int t = 0; t < n_ticks; t++) {
        for (int hh = 0; hh < num_atns; hh++) {
            actions[t * num_atns + hh] = action_value + t * num_atns + hh;
        }
    }
    size_t expected = (size_t)n_ticks * (size_t)num_atns;
    int ok = fwrite(&h, sizeof(h), 1, f) == 1 &&
             fwrite(snap_pool, 1, sizeof(snap_pool), f) == sizeof(snap_pool) &&
             fwrite(snap_ticks, sizeof(int), TEST_NUM_SNAPSHOTS, f) == TEST_NUM_SNAPSHOTS &&
             fwrite(actions, sizeof(int), expected, f) == expected;
    free(actions);
    fclose(f);
    return ok ? 0 : -1;
}

static void test_create_and_destroy(void) {
    printf("--- demostore create/destroy ---\n");
    DemoStore* s = demostore_create(8);
    ASSERT_INT_EQ("capacity", s->capacity, 8);
    ASSERT_INT_EQ("num_demos starts 0", s->num_demos, 0);
    demostore_destroy(s);
    demostore_destroy(NULL);
}

static void test_load_one_demo_round_trip(void) {
    printf("--- demostore load one demo round-trip ---\n");
    char path[] = "/tmp/test_demostore_one.bin";
    int n_ticks = 17;
    int num_atns = 9;
    uint32_t rng_seed = 0xdeadbeef;
    ASSERT_INT_EQ("write synthetic", write_synthetic_demo(path, n_ticks, num_atns, rng_seed, 100), 0);

    DemoStore* s = demostore_create(4);
    int demo_id = demostore_load_demo(s, path, num_atns, 0, TEST_SNAPSHOT_SIZE);
    ASSERT_INT_EQ("load returns demo_id 0", demo_id, 0);
    ASSERT_INT_EQ("num_demos == 1", s->num_demos, 1);

    const DemoTrajectory* d = &s->demos[0];
    ASSERT_INT_EQ("length_ticks", d->length_ticks, n_ticks);
    ASSERT_INT_EQ("num_atns", d->num_atns, num_atns);
    ASSERT_INT_EQ("rng_seed", (int)d->rng_seed, (int)rng_seed);
    ASSERT_INT_EQ("cursor starts at last snapshot tick",
        d->cursor_tick, ((TEST_NUM_SNAPSHOTS - 1) * n_ticks) / TEST_NUM_SNAPSHOTS);

    const int* a5 = demostore_actions_at(s, 0, 5);
    ASSERT_INT_EQ("actions[5][3]", a5[3], 148);

    demostore_destroy(s);
    unlink(path);
}

static void test_load_multiple_demos(void) {
    printf("--- demostore load multiple demos ---\n");
    char p0[] = "/tmp/test_demostore_a.bin";
    char p1[] = "/tmp/test_demostore_b.bin";
    char p2[] = "/tmp/test_demostore_c.bin";
    write_synthetic_demo(p0, 10, 9, 1u, 0);
    write_synthetic_demo(p1, 20, 9, 2u, 1000);
    write_synthetic_demo(p2, 5, 9, 3u, 5000);

    DemoStore* s = demostore_create(4);
    ASSERT_INT_EQ("load a", demostore_load_demo(s, p0, 9, 0, TEST_SNAPSHOT_SIZE), 0);
    ASSERT_INT_EQ("load b", demostore_load_demo(s, p1, 9, 0, TEST_SNAPSHOT_SIZE), 1);
    ASSERT_INT_EQ("load c", demostore_load_demo(s, p2, 9, 0, TEST_SNAPSHOT_SIZE), 2);
    ASSERT_INT_EQ("num_demos == 3", s->num_demos, 3);

    ASSERT_INT_EQ("a length", s->demos[0].length_ticks, 10);
    ASSERT_INT_EQ("b length", s->demos[1].length_ticks, 20);
    ASSERT_INT_EQ("c length", s->demos[2].length_ticks, 5);

    ASSERT_INT_EQ("b cursor starts at last snapshot tick",
        s->demos[1].cursor_tick, (3 * 20) / 4);

    demostore_destroy(s);
    unlink(p0); unlink(p1); unlink(p2);
}

static void test_capacity_full(void) {
    printf("--- demostore capacity full rejects further loads ---\n");
    char p[] = "/tmp/test_demostore_cap.bin";
    write_synthetic_demo(p, 4, 2, 0u, 0);
    DemoStore* s = demostore_create(2);
    ASSERT_INT_EQ("first load", demostore_load_demo(s, p, 2, 0, TEST_SNAPSHOT_SIZE), 0);
    ASSERT_INT_EQ("second load", demostore_load_demo(s, p, 2, 0, TEST_SNAPSHOT_SIZE), 1);
    ASSERT_INT_EQ("third load returns -1", demostore_load_demo(s, p, 2, 0, TEST_SNAPSHOT_SIZE), -1);
    ASSERT_INT_EQ("num_demos still 2", s->num_demos, 2);
    demostore_destroy(s);
    unlink(p);
}

static void test_filename_quality_parsing(void) {
    printf("--- demostore parses quality from filename ---\n");
    char p[] = "/tmp/demo_0007_q0.716_t108.bin";
    write_synthetic_demo(p, 4, 2, 0u, 0);
    DemoStore* s = demostore_create(2);
    int id = demostore_load_demo(s, p, 2, /*parse_filename_q=*/1, TEST_SNAPSHOT_SIZE);
    ASSERT_INT_EQ("loaded ok", id, 0);
    ASSERT_FLOAT_NEAR("quality parsed from filename", s->demos[0].quality_at_root, 0.716f, 0.001f);
    demostore_destroy(s);
    unlink(p);
}

static void test_reject_misaligned_action_buffer(void) {
    printf("--- demostore rejects file with trailing bytes (wrong num_atns) ---\n");
    char p[] = "/tmp/test_demostore_misaligned.bin";
    /* file written with num_atns=4 */
    write_synthetic_demo(p, 6, 4, 0u, 0);
    DemoStore* s = demostore_create(2);
    /* try to load as num_atns=3: should reject (header.num_atns mismatch) */
    int id = demostore_load_demo(s, p, 3, 0, TEST_SNAPSHOT_SIZE);
    ASSERT_INT_EQ("misaligned load returns -1", id, -1);
    ASSERT_INT_EQ("num_demos still 0", s->num_demos, 0);
    demostore_destroy(s);
    unlink(p);
}

static void test_reject_nonexistent_file(void) {
    printf("--- demostore rejects nonexistent file ---\n");
    DemoStore* s = demostore_create(2);
    ASSERT_INT_EQ("nonexistent file -> -1",
        demostore_load_demo(s, "/no/such/file_for_demostore.bin", 9, 0, TEST_SNAPSHOT_SIZE), -1);
    demostore_destroy(s);
}

static void test_ladder_count_for_length(void) {
    printf("--- ladder count_for_length covers tick 0 through length-1 ---\n");
    ASSERT_INT_EQ("100/4", demo_snapshot_ladder_count_for_length(100, 4), 25);
    ASSERT_INT_EQ("10/3", demo_snapshot_ladder_count_for_length(10, 3), 4);
    ASSERT_INT_EQ("1/4", demo_snapshot_ladder_count_for_length(1, 4), 1);
    ASSERT_INT_EQ("0/x", demo_snapshot_ladder_count_for_length(0, 4), 0);
    ASSERT_INT_EQ("100/0", demo_snapshot_ladder_count_for_length(100, 0), 0);
}

static void test_ladder_create_and_destroy(void) {
    printf("--- ladder create/destroy with and without hidden pool ---\n");
    DemoSnapshotLadder* l = demo_snapshot_ladder_create(3, 4, 25, 128, 0);
    ASSERT_INT_EQ("demo_id stored", l->demo_id, 3);
    ASSERT_INT_EQ("stride stored", l->snapshot_stride, 4);
    ASSERT_INT_EQ("num_snapshots stored", l->num_snapshots, 25);
    ASSERT_TRUE("hidden_pool null when hidden_size=0", l->hidden_pool == NULL);
    demo_snapshot_ladder_destroy(l);

    DemoSnapshotLadder* l2 = demo_snapshot_ladder_create(7, 4, 25, 128, 256 * 4);
    ASSERT_TRUE("hidden_pool allocated", l2->hidden_pool != NULL);
    demo_snapshot_ladder_destroy(l2);

    ASSERT_TRUE("stride=0 -> NULL",
        demo_snapshot_ladder_create(0, 0, 25, 128, 0) == NULL);
    ASSERT_TRUE("num_snapshots=0 -> NULL",
        demo_snapshot_ladder_create(0, 4, 0, 128, 0) == NULL);
    ASSERT_TRUE("snapshot_size=0 -> NULL",
        demo_snapshot_ladder_create(0, 4, 25, 0, 0) == NULL);
    demo_snapshot_ladder_destroy(NULL);
}

static void test_ladder_slot_for_tick(void) {
    printf("--- ladder slot_for_tick maps to floor(tick/stride) ---\n");
    DemoSnapshotLadder* l = demo_snapshot_ladder_create(0, 4, 10, 16, 0);
    ASSERT_INT_EQ("tick 0", demo_snapshot_ladder_slot_for_tick(l, 0), 0);
    ASSERT_INT_EQ("tick 3", demo_snapshot_ladder_slot_for_tick(l, 3), 0);
    ASSERT_INT_EQ("tick 4", demo_snapshot_ladder_slot_for_tick(l, 4), 1);
    ASSERT_INT_EQ("tick 8", demo_snapshot_ladder_slot_for_tick(l, 8), 2);
    ASSERT_INT_EQ("tick 999 clamps to last", demo_snapshot_ladder_slot_for_tick(l, 999), 9);
    ASSERT_INT_EQ("tick -1 -> -1", demo_snapshot_ladder_slot_for_tick(l, -1), -1);
    demo_snapshot_ladder_destroy(l);
}

static void test_ladder_snapshot_at_bounds(void) {
    printf("--- ladder snapshot_at and hidden_at bounds-check ---\n");
    DemoSnapshotLadder* l = demo_snapshot_ladder_create(0, 4, 5, 16, 8);
    ASSERT_TRUE("snapshot_at(0)", demo_snapshot_ladder_snapshot_at(l, 0) != NULL);
    ASSERT_TRUE("snapshot_at(-1) null", demo_snapshot_ladder_snapshot_at(l, -1) == NULL);
    ASSERT_TRUE("snapshot_at(5) null", demo_snapshot_ladder_snapshot_at(l, 5) == NULL);
    ASSERT_TRUE("hidden_at(0)", demo_snapshot_ladder_hidden_at(l, 0) != NULL);
    demo_snapshot_ladder_destroy(l);

    DemoSnapshotLadder* nh = demo_snapshot_ladder_create(0, 4, 3, 16, 0);
    ASSERT_TRUE("hidden_at NULL when no pool",
        demo_snapshot_ladder_hidden_at(nh, 0) == NULL);
    demo_snapshot_ladder_destroy(nh);
}

int main(void) {
    test_create_and_destroy();
    test_load_one_demo_round_trip();
    test_load_multiple_demos();
    test_capacity_full();
    test_filename_quality_parsing();
    test_reject_misaligned_action_buffer();
    test_reject_nonexistent_file();
    test_ladder_count_for_length();
    test_ladder_create_and_destroy();
    test_ladder_slot_for_tick();
    test_ladder_snapshot_at_bounds();

    printf("\n%d/%d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail == 0 ? 0 : 1;
}
