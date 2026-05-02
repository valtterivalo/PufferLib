/**
 * @file test_archive.c
 * @brief Unit tests for the cell-keyed archive (src/archive.h).
 *
 * BUILD:
 *   cc -std=c11 -O0 -g -I. -o /tmp/test_archive tests/test_archive.c -lm
 *   /tmp/test_archive
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "src/archive.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_INT_EQ(label, actual, expected) do { \
    tests_run++; \
    if ((actual) == (expected)) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        printf("  FAIL: %s — got %lld, expected %lld\n", \
            (label), (long long)(actual), (long long)(expected)); \
    } \
} while (0)

#define ASSERT_TRUE(label, cond) do { \
    tests_run++; \
    if (cond) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        printf("  FAIL: %s — condition false\n", (label)); \
    } \
} while (0)

/* dummy snapshot type for the tests — fixed size, content is just an int and a tag. */
#define DUMMY_SNAP_SIZE 32
typedef struct {
    int tag;
    int payload;
    char filler[DUMMY_SNAP_SIZE - 2 * sizeof(int)];
} DummySnap;

#define DUMMY_NUM_ATNS 4
#define DUMMY_CAPACITY 64
#define DUMMY_CHUNK_POOL_INTS 4096

static void make_key(uint8_t* key, int seed) {
    /* deterministic but distinct keys per seed */
    memset(key, 0, ARCHIVE_KEY_SIZE);
    for (int i = 0; i < ARCHIVE_KEY_SIZE; i++) {
        key[i] = (uint8_t)((seed * (i + 1) * 31) & 0xff);
    }
}

static void test_archive_create_and_destroy(void) {
    printf("--- archive create and destroy ---\n");
    Archive* a = archive_create(DUMMY_CAPACITY, DUMMY_SNAP_SIZE,
        DUMMY_NUM_ATNS, DUMMY_CHUNK_POOL_INTS, 0, 42u);
    ASSERT_TRUE("archive created", a != NULL);
    ASSERT_INT_EQ("capacity", a->capacity, DUMMY_CAPACITY);
    ASSERT_INT_EQ("num_entries starts at 0", a->num_entries, 0);
    ASSERT_INT_EQ("num_buckets is power of two >= 2*capacity",
        (a->num_buckets & (a->num_buckets - 1)), 0);
    ASSERT_TRUE("num_buckets >= 2 * capacity", a->num_buckets >= 2 * DUMMY_CAPACITY);
    archive_destroy(a);
}

static void test_archive_insert_lookup_round_trip(void) {
    printf("--- archive insert / lookup round trip ---\n");
    Archive* a = archive_create(DUMMY_CAPACITY, DUMMY_SNAP_SIZE,
        DUMMY_NUM_ATNS, DUMMY_CHUNK_POOL_INTS, 0, 42u);

    DummySnap snap = { .tag = 0xAA, .payload = 7 };
    uint8_t key[ARCHIVE_KEY_SIZE];
    make_key(key, 1);

    ArchiveInsertResult r = (ArchiveInsertResult)-1;
    int idx = archive_insert(a, key, &snap, NULL, ARCHIVE_ROOT_PARENT,
        NULL, 0, 0u, 0.5f, &r);
    ASSERT_TRUE("first insert returns valid index", idx >= 0);
    ASSERT_INT_EQ("first insert result is NEW", (int)r, (int)ARCHIVE_INSERT_NEW);
    ASSERT_INT_EQ("num_entries == 1 after insert", a->num_entries, 1);

    int found = archive_lookup(a, key);
    ASSERT_INT_EQ("lookup finds the inserted entry", found, idx);

    const DummySnap* snap_back = (const DummySnap*)archive_get_snapshot(a, idx);
    ASSERT_TRUE("snapshot retrievable", snap_back != NULL);
    ASSERT_INT_EQ("snapshot tag round-trips", snap_back->tag, 0xAA);
    ASSERT_INT_EQ("snapshot payload round-trips", snap_back->payload, 7);

    archive_destroy(a);
}

static void test_archive_duplicate_keeps_first_write_structural_fields(void) {
    printf("--- archive re-discovery preserves snapshot/parent/actions, only updates quality stat ---\n");
    Archive* a = archive_create(DUMMY_CAPACITY, DUMMY_SNAP_SIZE,
        DUMMY_NUM_ATNS, DUMMY_CHUNK_POOL_INTS, 0, 42u);

    uint8_t key[ARCHIVE_KEY_SIZE];
    make_key(key, 5);

    DummySnap a_snap = { .tag = 1, .payload = 100 };
    ArchiveInsertResult r = (ArchiveInsertResult)-1;
    int idx_a = archive_insert(a, key, &a_snap, NULL, ARCHIVE_ROOT_PARENT,
        NULL, 0, 0u, 0.3f, &r);
    ASSERT_INT_EQ("first insert NEW", (int)r, (int)ARCHIVE_INSERT_NEW);

    /* lower quality: kept, seen++ */
    DummySnap b_snap = { .tag = 2, .payload = 200 };
    int idx_b = archive_insert(a, key, &b_snap, NULL, ARCHIVE_ROOT_PARENT,
        NULL, 0, 0u, 0.2f, &r);
    ASSERT_INT_EQ("lower quality returns same idx", idx_b, idx_a);
    ASSERT_INT_EQ("lower quality result KEPT", (int)r, (int)ARCHIVE_INSERT_KEPT);
    ASSERT_INT_EQ("seen incremented", (int)a->entries[idx_a].seen, 2);
    const DummySnap* still_a = (const DummySnap*)archive_get_snapshot(a, idx_a);
    ASSERT_INT_EQ("snapshot still tag=1", still_a->tag, 1);

    /* higher quality: structural fields stay frozen, only quality stat updates */
    DummySnap c_snap = { .tag = 3, .payload = 300 };
    int idx_c = archive_insert(a, key, &c_snap, NULL, ARCHIVE_ROOT_PARENT,
        NULL, 0, 0u, 0.7f, &r);
    ASSERT_INT_EQ("higher quality returns same idx", idx_c, idx_a);
    ASSERT_INT_EQ("higher quality result REPLACED", (int)r, (int)ARCHIVE_INSERT_REPLACED);
    const DummySnap* still_a_after_q_bump = (const DummySnap*)archive_get_snapshot(a, idx_a);
    ASSERT_INT_EQ("snapshot NOT replaced (stays tag=1)", still_a_after_q_bump->tag, 1);
    ASSERT_INT_EQ("snapshot payload NOT replaced", still_a_after_q_bump->payload, 100);
    ASSERT_TRUE("quality stat updated to 0.7", a->entries[idx_a].quality > 0.69f);

    /* equal quality: kept (strict) */
    DummySnap d_snap = { .tag = 4, .payload = 400 };
    int idx_d = archive_insert(a, key, &d_snap, NULL, ARCHIVE_ROOT_PARENT,
        NULL, 0, 0u, 0.7f, &r);
    ASSERT_INT_EQ("equal quality returns same idx", idx_d, idx_a);
    ASSERT_INT_EQ("equal quality result KEPT", (int)r, (int)ARCHIVE_INSERT_KEPT);
    const DummySnap* still_a_2 = (const DummySnap*)archive_get_snapshot(a, idx_a);
    ASSERT_INT_EQ("equal quality leaves snapshot tag=1", still_a_2->tag, 1);

    archive_destroy(a);
}

static void test_archive_action_chunk_roundtrip_and_replay(void) {
    printf("--- archive action chunk replay walks parent chain ---\n");
    Archive* a = archive_create(DUMMY_CAPACITY, DUMMY_SNAP_SIZE,
        DUMMY_NUM_ATNS, DUMMY_CHUNK_POOL_INTS, 0, 42u);

    DummySnap snap = { .tag = 0, .payload = 0 };

    /* Build a 3-deep parent chain.
       root -> middle -> tail
       chunks of 2, 3, 4 ticks respectively, each tick has DUMMY_NUM_ATNS=4 ints. */
    uint8_t key_root[ARCHIVE_KEY_SIZE], key_mid[ARCHIVE_KEY_SIZE],
            key_tail[ARCHIVE_KEY_SIZE];
    make_key(key_root, 100);
    make_key(key_mid, 101);
    make_key(key_tail, 102);

    int chunk_root[2 * DUMMY_NUM_ATNS] = {
        1,1,1,1,
        2,2,2,2,
    };
    int chunk_mid[3 * DUMMY_NUM_ATNS] = {
        3,3,3,3,
        4,4,4,4,
        5,5,5,5,
    };
    int chunk_tail[4 * DUMMY_NUM_ATNS] = {
        6,6,6,6,
        7,7,7,7,
        8,8,8,8,
        9,9,9,9,
    };

    ArchiveInsertResult r;
    int idx_root = archive_insert(a, key_root, &snap, NULL, ARCHIVE_ROOT_PARENT,
        chunk_root, 2, 0u, 0.1f, &r);
    int idx_mid = archive_insert(a, key_mid, &snap, NULL, idx_root,
        chunk_mid, 3, 0u, 0.2f, &r);
    int idx_tail = archive_insert(a, key_tail, &snap, NULL, idx_mid,
        chunk_tail, 4, 0u, 0.3f, &r);

    ASSERT_TRUE("root inserted", idx_root >= 0);
    ASSERT_TRUE("mid inserted", idx_mid >= 0);
    ASSERT_TRUE("tail inserted", idx_tail >= 0);

    /* expected full sequence is chunk_root then chunk_mid then chunk_tail */
    int total_ticks = 2 + 3 + 4;
    int* replayed = (int*)malloc((size_t)total_ticks * DUMMY_NUM_ATNS * sizeof(int));
    int written = archive_replay_actions(a, idx_tail, replayed, 100);

    ASSERT_INT_EQ("replay tick count matches total chain length",
        written, total_ticks);

    int expected[9 * DUMMY_NUM_ATNS] = {
        1,1,1,1, 2,2,2,2,
        3,3,3,3, 4,4,4,4, 5,5,5,5,
        6,6,6,6, 7,7,7,7, 8,8,8,8, 9,9,9,9,
    };
    int diff = memcmp(replayed, expected,
        (size_t)total_ticks * DUMMY_NUM_ATNS * sizeof(int));
    ASSERT_INT_EQ("replayed sequence matches root->mid->tail concat", diff, 0);

    free(replayed);
    archive_destroy(a);
}

static void test_archive_sample_weights_high_quality(void) {
    printf("--- archive sample biases toward higher-quality entries ---\n");
    Archive* a = archive_create(DUMMY_CAPACITY, DUMMY_SNAP_SIZE,
        DUMMY_NUM_ATNS, DUMMY_CHUNK_POOL_INTS, 0, 0xC0FFEEu);

    DummySnap snap = { .tag = 0, .payload = 0 };

    /* insert 4 cells with quality 0.0, 0.25, 0.50, 1.0. equal counts to start. */
    int idx[4];
    float qs[] = { 0.0f, 0.25f, 0.5f, 1.0f };
    ArchiveInsertResult r;
    for (int i = 0; i < 4; i++) {
        uint8_t key[ARCHIVE_KEY_SIZE];
        make_key(key, 200 + i);
        idx[i] = archive_insert(a, key, &snap, NULL, ARCHIVE_ROOT_PARENT,
            NULL, 0, 0u, qs[i], &r);
    }

    int counts[4] = {0};
    const int N_SAMPLES = 4000;
    for (int i = 0; i < N_SAMPLES; i++) {
        int picked = archive_sample(a);
        for (int j = 0; j < 4; j++) {
            if (picked == idx[j]) { counts[j]++; break; }
        }
    }

    /* weights at chosen=0: (1 + 9q) * (0.6/sqrt(c+1) + 0.3/sqrt(csn+1) + 0.1/sqrt(s+1) + eps).
       BUT chosen and chosen_since_new grow during sampling, so high-quality cells
       lose their bonus over time. We just assert the highest-quality cell is the
       most-sampled overall and the lowest-quality cell is least-sampled. */
    ASSERT_TRUE("q=1.0 sampled more than q=0.0 (bias check)",
        counts[3] > counts[0]);
    ASSERT_TRUE("q=0.5 sampled more than q=0.0 (bias check)",
        counts[2] > counts[0]);

    /* sanity: total samples == N */
    int total = counts[0] + counts[1] + counts[2] + counts[3];
    ASSERT_INT_EQ("all samples accounted for", total, N_SAMPLES);

    archive_destroy(a);
}

static void test_archive_full_returns_null(void) {
    printf("--- archive full returns ARCHIVE_NULL_INDEX ---\n");
    int cap = 4;
    Archive* a = archive_create(cap, DUMMY_SNAP_SIZE,
        DUMMY_NUM_ATNS, DUMMY_CHUNK_POOL_INTS, 0, 7u);
    DummySnap snap = { .tag = 0, .payload = 0 };
    ArchiveInsertResult r;
    for (int i = 0; i < cap; i++) {
        uint8_t key[ARCHIVE_KEY_SIZE];
        make_key(key, 300 + i);
        int idx = archive_insert(a, key, &snap, NULL, ARCHIVE_ROOT_PARENT,
            NULL, 0, 0u, 0.1f, &r);
        ASSERT_TRUE("fits while under capacity", idx >= 0);
    }
    uint8_t over_key[ARCHIVE_KEY_SIZE];
    make_key(over_key, 999);
    int over = archive_insert(a, over_key, &snap, NULL, ARCHIVE_ROOT_PARENT,
        NULL, 0, 0u, 0.1f, &r);
    ASSERT_INT_EQ("over-capacity returns null index", over, ARCHIVE_NULL_INDEX);
    ASSERT_INT_EQ("over-capacity reports FULL", (int)r, (int)ARCHIVE_INSERT_FULL);
    archive_destroy(a);
}

static void test_archive_hidden_state_round_trip(void) {
    printf("--- archive hidden state round trips per entry ---\n");
    /* simulate a recurrent policy with [num_layers, hidden_size] = [3, 256] floats */
    const size_t hidden_state_size = 3 * 256 * sizeof(float);

    Archive* a = archive_create(DUMMY_CAPACITY, DUMMY_SNAP_SIZE,
        DUMMY_NUM_ATNS, DUMMY_CHUNK_POOL_INTS, hidden_state_size, 42u);
    ASSERT_TRUE("archive with hidden state created", a != NULL);
    ASSERT_INT_EQ("hidden_state_pool allocated",
        (int)(a->hidden_state_pool != NULL ? 1 : 0), 1);

    DummySnap snap = { .tag = 0, .payload = 0 };

    /* entry 1: insert with a recognizable hidden state */
    float hs_a[3 * 256];
    for (int i = 0; i < 3 * 256; i++) hs_a[i] = (float)i * 0.001f;
    uint8_t key_a[ARCHIVE_KEY_SIZE];
    make_key(key_a, 50);
    ArchiveInsertResult r;
    int idx_a = archive_insert(a, key_a, &snap, hs_a, ARCHIVE_ROOT_PARENT,
        NULL, 0, 0u, 0.5f, &r);
    ASSERT_TRUE("insert with hidden state ok", idx_a >= 0);

    const float* hs_back = (const float*)archive_get_hidden_state(a, idx_a);
    ASSERT_TRUE("hidden state retrievable", hs_back != NULL);
    int diff = memcmp(hs_back, hs_a, hidden_state_size);
    ASSERT_INT_EQ("hidden state byte-identical to insert", diff, 0);

    /* entry 2: insert with NULL hidden state (should be zero-filled) */
    uint8_t key_b[ARCHIVE_KEY_SIZE];
    make_key(key_b, 51);
    int idx_b = archive_insert(a, key_b, &snap, NULL, ARCHIVE_ROOT_PARENT,
        NULL, 0, 0u, 0.5f, &r);
    ASSERT_TRUE("insert with NULL hidden state ok", idx_b >= 0);
    const float* hs_b = (const float*)archive_get_hidden_state(a, idx_b);
    int all_zero = 1;
    for (int i = 0; i < 3 * 256; i++) {
        if (hs_b[i] != 0.0f) { all_zero = 0; break; }
    }
    ASSERT_INT_EQ("NULL hidden state stored as zeros", all_zero, 1);

    float hs_a2[3 * 256];
    for (int i = 0; i < 3 * 256; i++) hs_a2[i] = (float)(i + 1000) * 0.002f;
    int idx_a2 = archive_insert(a, key_a, &snap, hs_a2, ARCHIVE_ROOT_PARENT,
        NULL, 0, 0u, 0.9f, &r);
    ASSERT_INT_EQ("replace returns same idx", idx_a2, idx_a);
    ASSERT_INT_EQ("replace result", (int)r, (int)ARCHIVE_INSERT_REPLACED);
    const float* hs_back2 = (const float*)archive_get_hidden_state(a, idx_a);
    int diff2 = memcmp(hs_back2, hs_a, hidden_state_size);
    ASSERT_INT_EQ("hidden state stays as first-write on quality bump", diff2, 0);

    archive_destroy(a);
}

static void test_archive_no_hidden_state_returns_null(void) {
    printf("--- archive without hidden state pool returns NULL on get ---\n");
    Archive* a = archive_create(DUMMY_CAPACITY, DUMMY_SNAP_SIZE,
        DUMMY_NUM_ATNS, DUMMY_CHUNK_POOL_INTS, 0, 42u);
    DummySnap snap = { 0 };
    uint8_t key[ARCHIVE_KEY_SIZE];
    make_key(key, 60);
    ArchiveInsertResult r;
    int idx = archive_insert(a, key, &snap, NULL, ARCHIVE_ROOT_PARENT,
        NULL, 0, 0u, 0.5f, &r);
    ASSERT_TRUE("insert ok", idx >= 0);
    const void* hs = archive_get_hidden_state(a, idx);
    ASSERT_TRUE("get_hidden_state returns NULL when pool absent", hs == NULL);
    archive_destroy(a);
}

static void test_archive_save_load_round_trip(void) {
    printf("--- archive save / load preserves entries, pools, and lookup ---\n");

    Archive* a = archive_create(DUMMY_CAPACITY, DUMMY_SNAP_SIZE,
        DUMMY_NUM_ATNS, DUMMY_CHUNK_POOL_INTS, /*hidden_state_size=*/64, 12345u);

    /* insert a small but representative set so we exercise every pool */
    DummySnap snap = { .tag = 0xBE, .payload = 99 };
    uint8_t hs[64];
    for (int i = 0; i < 64; i++) hs[i] = (uint8_t)(i * 3 + 7);

    int chunk[3 * DUMMY_NUM_ATNS] = { 11,12,13,14,  21,22,23,24,  31,32,33,34 };

    ArchiveInsertResult r;
    uint8_t k_root[ARCHIVE_KEY_SIZE], k_mid[ARCHIVE_KEY_SIZE], k_leaf[ARCHIVE_KEY_SIZE];
    make_key(k_root, 400);
    make_key(k_mid, 401);
    make_key(k_leaf, 402);

    int idx_root = archive_insert(a, k_root, &snap, hs, ARCHIVE_ROOT_PARENT,
        NULL, 0, 0u, 0.1f, &r);
    int idx_mid  = archive_insert(a, k_mid,  &snap, hs, idx_root,
        chunk, 2, 7u, 0.4f, &r);
    int idx_leaf = archive_insert(a, k_leaf, &snap, hs, idx_mid,
        chunk + 2 * DUMMY_NUM_ATNS, 1, 99u, 0.9f, &r);

    /* simulate some sample/observe traffic so chosen and seen != 0 */
    a->entries[idx_root].chosen = 5;
    a->entries[idx_root].seen = 9;
    a->entries[idx_mid].chosen = 2;

    const char* path = "/tmp/test_archive_round_trip.bin";
    int save_rc = archive_save(a, path);
    ASSERT_INT_EQ("save returns 0", save_rc, 0);

    Archive* b = archive_load(path);
    ASSERT_TRUE("load returns non-null", b != NULL);
    ASSERT_INT_EQ("loaded num_entries matches", b->num_entries, a->num_entries);
    ASSERT_INT_EQ("loaded snapshot_size matches",
        (int)b->snapshot_size, (int)a->snapshot_size);
    ASSERT_INT_EQ("loaded num_atns matches", b->num_atns, a->num_atns);
    ASSERT_INT_EQ("loaded hidden_state_size matches",
        (int)b->hidden_state_size, (int)a->hidden_state_size);
    ASSERT_INT_EQ("loaded action_chunk_pool_used matches",
        b->action_chunk_pool_used_ints, a->action_chunk_pool_used_ints);

    /* lookup works on the loaded archive */
    ASSERT_INT_EQ("loaded lookup finds root", archive_lookup(b, k_root), idx_root);
    ASSERT_INT_EQ("loaded lookup finds mid", archive_lookup(b, k_mid), idx_mid);
    ASSERT_INT_EQ("loaded lookup finds leaf", archive_lookup(b, k_leaf), idx_leaf);

    /* entries equal */
    int entries_eq = memcmp(a->entries, b->entries,
        (size_t)a->num_entries * sizeof(ArchiveEntry)) == 0;
    ASSERT_INT_EQ("entries memcmp == 0", entries_eq, 1);

    /* snapshot pool equal */
    int snap_eq = memcmp(a->snapshot_pool, b->snapshot_pool,
        (size_t)a->num_entries * a->snapshot_size) == 0;
    ASSERT_INT_EQ("snapshot pool memcmp == 0", snap_eq, 1);

    /* action chunk pool equal */
    int chunks_eq = memcmp(a->action_chunk_pool, b->action_chunk_pool,
        (size_t)a->action_chunk_pool_used_ints * sizeof(int)) == 0;
    ASSERT_INT_EQ("action chunk pool memcmp == 0", chunks_eq, 1);

    /* hidden state pool equal */
    int hs_eq = memcmp(a->hidden_state_pool, b->hidden_state_pool,
        (size_t)a->num_entries * a->hidden_state_size) == 0;
    ASSERT_INT_EQ("hidden state pool memcmp == 0", hs_eq, 1);

    /* replay actions on the loaded archive matches the original */
    int total_ticks = 2 + 1;
    int* a_replay = (int*)malloc((size_t)total_ticks * DUMMY_NUM_ATNS * sizeof(int));
    int* b_replay = (int*)malloc((size_t)total_ticks * DUMMY_NUM_ATNS * sizeof(int));
    int aw = archive_replay_actions(a, idx_leaf, a_replay, 100);
    int bw = archive_replay_actions(b, idx_leaf, b_replay, 100);
    ASSERT_INT_EQ("replay length matches", bw, aw);
    int replay_eq = memcmp(a_replay, b_replay,
        (size_t)aw * DUMMY_NUM_ATNS * sizeof(int)) == 0;
    ASSERT_INT_EQ("replay actions memcmp == 0", replay_eq, 1);
    free(a_replay);
    free(b_replay);

    archive_destroy(a);
    archive_destroy(b);
    remove(path);
}

static void test_archive_load_rejects_bad_magic(void) {
    printf("--- archive load rejects file with bad magic ---\n");
    const char* path = "/tmp/test_archive_bad_magic.bin";
    FILE* fp = fopen(path, "wb");
    ASSERT_TRUE("scratch file opened", fp != NULL);
    uint32_t bad_magic = 0xDEADBEEFu;
    fwrite(&bad_magic, sizeof(bad_magic), 1, fp);
    /* pad enough bytes that a header read doesn't get a short-read first */
    char pad[sizeof(ArchiveFileHeader)];
    fwrite(pad, sizeof(pad), 1, fp);
    fclose(fp);

    Archive* a = archive_load(path);
    ASSERT_TRUE("load returns NULL on bad magic", a == NULL);
    remove(path);
}

static void test_archive_export_top_k_demos(void) {
    printf("--- archive export top-K demos in PLAY_REPLAY format ---\n");

    Archive* a = archive_create(DUMMY_CAPACITY, DUMMY_SNAP_SIZE,
        DUMMY_NUM_ATNS, DUMMY_CHUNK_POOL_INTS, 0, 999u);
    DummySnap snap = { .tag = 1, .payload = 1 };

    /* build three chains with different qualities so we can verify ordering */
    uint8_t k[6][ARCHIVE_KEY_SIZE];
    for (int i = 0; i < 6; i++) make_key(k[i], 500 + i);

    int chunk_a[2 * DUMMY_NUM_ATNS] = { 1,1,1,1, 2,2,2,2 };
    int chunk_b[3 * DUMMY_NUM_ATNS] = { 3,3,3,3, 4,4,4,4, 5,5,5,5 };

    ArchiveInsertResult r;
    /* chain 1: leaf has q=0.9 (best), 2 ticks long */
    int c1_root = archive_insert(a, k[0], &snap, NULL, ARCHIVE_ROOT_PARENT, NULL, 0, 7u, 0.5f, &r);
    int c1_leaf = archive_insert(a, k[1], &snap, NULL, c1_root, chunk_a, 2, 7u, 0.9f, &r);

    /* chain 2: leaf has q=0.4 (mid), 5 ticks long */
    int c2_root = archive_insert(a, k[2], &snap, NULL, ARCHIVE_ROOT_PARENT, NULL, 0, 11u, 0.2f, &r);
    int c2_mid  = archive_insert(a, k[3], &snap, NULL, c2_root, chunk_a, 2, 11u, 0.3f, &r);
    int c2_leaf = archive_insert(a, k[4], &snap, NULL, c2_mid,  chunk_b, 3, 11u, 0.4f, &r);

    /* chain 3: a single root-only entry with q=0.1 (no chunk, will be skipped) */
    int c3_root = archive_insert(a, k[5], &snap, NULL, ARCHIVE_ROOT_PARENT, NULL, 0, 0u, 0.1f, &r);
    (void)c1_leaf; (void)c2_leaf; (void)c3_root;

    const char* dir = "/tmp/test_archive_demos";
    mkdir(dir, 0755);

    int written = archive_export_top_k_demos(a, dir, 10, 100);
    /* expected: 4 (c1_root has 0 chunks but quality > 0; chain_tick_count = 0
       for c1_root since its action_chunk_len is 0 and parent is ROOT, so it
       gets filtered. c1_leaf, c2_root (chunk=0 also filtered), c2_mid, c2_leaf,
       c3_root (chunk=0 filtered). So 3 chains have chunks: c1_leaf (2 ticks),
       c2_mid (2 ticks via chain), c2_leaf (5 ticks via chain). */
    ASSERT_INT_EQ("3 demo files written (chains with at least 1 action)",
        written, 3);

    /* verify the highest-quality file exists and is parseable */
    char top_path[1024];
    snprintf(top_path, sizeof(top_path), "%s/demo_0000_q0.900_t2.bin", dir);
    FILE* fp = fopen(top_path, "rb");
    ASSERT_TRUE("top demo file opens", fp != NULL);
    int n_ticks = 0;
    uint32_t rng_seed = 0;
    int read_ok = (fread(&n_ticks, sizeof(int), 1, fp) == 1) &&
                  (fread(&rng_seed, sizeof(uint32_t), 1, fp) == 1);
    ASSERT_INT_EQ("top demo header reads", read_ok ? 1 : 0, 1);
    ASSERT_INT_EQ("top demo n_ticks == 2", n_ticks, 2);
    ASSERT_INT_EQ("top demo rng_seed matches chain root", (int)rng_seed, 7);

    int actions[2 * DUMMY_NUM_ATNS] = {0};
    size_t got = fread(actions, sizeof(int),
        (size_t)n_ticks * DUMMY_NUM_ATNS, fp);
    ASSERT_INT_EQ("top demo action bytes match",
        (int)got, n_ticks * DUMMY_NUM_ATNS);
    int actions_eq = memcmp(actions, chunk_a,
        sizeof(chunk_a)) == 0;
    ASSERT_INT_EQ("top demo actions match chunk_a", actions_eq, 1);
    fclose(fp);

    /* cleanup */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    system(cmd);

    archive_destroy(a);
}

int main(void) {
    test_archive_create_and_destroy();
    test_archive_insert_lookup_round_trip();
    test_archive_duplicate_keeps_first_write_structural_fields();
    test_archive_action_chunk_roundtrip_and_replay();
    test_archive_sample_weights_high_quality();
    test_archive_full_returns_null();
    test_archive_hidden_state_round_trip();
    test_archive_no_hidden_state_returns_null();
    test_archive_save_load_round_trip();
    test_archive_load_rejects_bad_magic();
    test_archive_export_top_k_demos();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_failed == 0) ? 0 : 1;
}
