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
#include <unistd.h>

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

static int archive_insert_same_quality(
    Archive* a,
    const uint8_t* key,
    const void* snapshot,
    const void* hidden_state,
    int parent_idx,
    const int* action_chunk,
    int action_chunk_len,
    uint32_t rng_seed,
    float quality,
    ArchiveInsertResult* out_result
) {
    return archive_insert(a, key, snapshot, hidden_state, parent_idx,
        action_chunk, action_chunk_len, rng_seed, quality, quality, out_result);
}

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
    int idx = archive_insert_same_quality(a, key, &snap, NULL, ARCHIVE_ROOT_PARENT,
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

static void test_archive_duplicate_updates_sampling_and_replaces_better_structure(void) {
    printf("--- archive duplicate updates sampling and replaces better structure ---\n");
    Archive* a = archive_create(DUMMY_CAPACITY, DUMMY_SNAP_SIZE,
        DUMMY_NUM_ATNS, DUMMY_CHUNK_POOL_INTS, 64, 42u);

    uint8_t key[ARCHIVE_KEY_SIZE];
    make_key(key, 5);

    DummySnap a_snap = { .tag = 1, .payload = 100 };
    uint8_t hs_a[64];
    memset(hs_a, 11, sizeof(hs_a));
    int chunk_a[2 * DUMMY_NUM_ATNS] = { 1,1,1,1, 2,2,2,2 };
    ArchiveInsertResult r = (ArchiveInsertResult)-1;
    int idx_a = archive_insert(a, key, &a_snap, hs_a, ARCHIVE_ROOT_PARENT,
        chunk_a, 2, 17u, 0.3f, 0.3f, &r);
    ASSERT_INT_EQ("first insert NEW", (int)r, (int)ARCHIVE_INSERT_NEW);

    DummySnap b_snap = { .tag = 2, .payload = 200 };
    uint8_t hs_b[64];
    memset(hs_b, 22, sizeof(hs_b));
    int chunk_b[1 * DUMMY_NUM_ATNS] = { 3,3,3,3 };
    int idx_b = archive_insert(a, key, &b_snap, hs_b, ARCHIVE_ROOT_PARENT,
        chunk_b, 1, 23u, 0.8f, 0.2f, &r);
    ASSERT_INT_EQ("sampling-only update returns same idx", idx_b, idx_a);
    ASSERT_INT_EQ("sampling-only update result", (int)r,
        (int)ARCHIVE_INSERT_SAMPLING_UPDATED);
    ASSERT_INT_EQ("seen incremented", (int)a->entries[idx_a].seen, 2);
    const DummySnap* still_a = (const DummySnap*)archive_get_snapshot(a, idx_a);
    ASSERT_INT_EQ("sampling-only leaves snapshot tag=1", still_a->tag, 1);
    ASSERT_INT_EQ("sampling quality updated to 0.8",
        (int)(a->entries[idx_a].sampling_quality * 1000.0f), 800);
    ASSERT_INT_EQ("structural quality remains 0.3",
        (int)(a->entries[idx_a].structural_quality * 1000.0f), 300);

    DummySnap c_snap = { .tag = 3, .payload = 300 };
    uint8_t hs_c[64];
    memset(hs_c, 33, sizeof(hs_c));
    int chunk_c[3 * DUMMY_NUM_ATNS] = {
        4,4,4,4,
        5,5,5,5,
        6,6,6,6,
    };
    int idx_c = archive_insert(a, key, &c_snap, hs_c, ARCHIVE_ROOT_PARENT,
        chunk_c, 3, 31u, 0.7f, 0.9f, &r);
    ASSERT_INT_EQ("structural replacement returns same idx", idx_c, idx_a);
    ASSERT_INT_EQ("structural replacement result", (int)r,
        (int)ARCHIVE_INSERT_STRUCTURAL_REPLACED);
    const DummySnap* replaced = (const DummySnap*)archive_get_snapshot(a, idx_a);
    ASSERT_INT_EQ("snapshot replaced tag=3", replaced->tag, 3);
    ASSERT_INT_EQ("snapshot replaced payload=300", replaced->payload, 300);
    ASSERT_INT_EQ("sampling quality remains best 0.8",
        (int)(a->entries[idx_a].sampling_quality * 1000.0f), 800);
    ASSERT_INT_EQ("structural quality updated to 0.9",
        (int)(a->entries[idx_a].structural_quality * 1000.0f), 900);
    ASSERT_INT_EQ("rng seed replaced", (int)a->entries[idx_a].rng_seed, 31);
    ASSERT_INT_EQ("action chunk len replaced", a->entries[idx_a].action_chunk_len, 3);
    const int* chunk_back = archive_get_action_chunk(a, idx_a);
    ASSERT_INT_EQ("replacement action chunk first value", chunk_back[0], 4);
    const uint8_t* hs_back = (const uint8_t*)archive_get_hidden_state(a, idx_a);
    ASSERT_INT_EQ("hidden state replaced", hs_back[0], 33);

    DummySnap d_snap = { .tag = 4, .payload = 400 };
    int idx_d = archive_insert(a, key, &d_snap, NULL, ARCHIVE_ROOT_PARENT,
        NULL, 0, 0u, 0.7f, 0.9f, &r);
    ASSERT_INT_EQ("equal structural quality returns same idx", idx_d, idx_a);
    ASSERT_INT_EQ("equal quality result KEPT", (int)r, (int)ARCHIVE_INSERT_KEPT);
    const DummySnap* still_c = (const DummySnap*)archive_get_snapshot(a, idx_a);
    ASSERT_INT_EQ("equal structural quality leaves snapshot tag=3", still_c->tag, 3);

    archive_destroy(a);
}

static void test_archive_duplicate_sampling_updates_when_structural_pool_is_full(void) {
    printf("--- archive duplicate sampling updates when structural replacement has no chunk space ---\n");

    Archive* a = archive_create(DUMMY_CAPACITY, DUMMY_SNAP_SIZE,
        DUMMY_NUM_ATNS, DUMMY_NUM_ATNS, 0, 42u);

    uint8_t key[ARCHIVE_KEY_SIZE];
    make_key(key, 6);
    DummySnap snap_a = { .tag = 1, .payload = 100 };
    DummySnap snap_b = { .tag = 2, .payload = 200 };
    int chunk_a[DUMMY_NUM_ATNS] = { 1,1,1,1 };
    int chunk_b[2 * DUMMY_NUM_ATNS] = { 2,2,2,2, 3,3,3,3 };

    ArchiveInsertResult r = (ArchiveInsertResult)-1;
    int idx = archive_insert(a, key, &snap_a, NULL, ARCHIVE_ROOT_PARENT,
        chunk_a, 1, 17u, 0.3f, 0.3f, &r);
    ASSERT_INT_EQ("first insert NEW", (int)r, (int)ARCHIVE_INSERT_NEW);
    ASSERT_TRUE("first insert returns valid index", idx >= 0);

    int duplicate = archive_insert(a, key, &snap_b, NULL, ARCHIVE_ROOT_PARENT,
        chunk_b, 2, 23u, 0.8f, 0.9f, &r);
    ASSERT_INT_EQ("duplicate returns existing index", duplicate, idx);
    ASSERT_INT_EQ("sampling update reported", (int)r,
        (int)ARCHIVE_INSERT_SAMPLING_UPDATED);
    ASSERT_INT_EQ("sampling quality updated despite full pool",
        (int)(a->entries[idx].sampling_quality * 1000.0f), 800);
    ASSERT_INT_EQ("structural quality unchanged",
        (int)(a->entries[idx].structural_quality * 1000.0f), 300);
    const DummySnap* still_a = (const DummySnap*)archive_get_snapshot(a, idx);
    ASSERT_INT_EQ("snapshot unchanged", still_a->tag, 1);
    ASSERT_INT_EQ("action chunk unchanged", a->entries[idx].action_chunk_len, 1);

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
    int idx_root = archive_insert_same_quality(a, key_root, &snap, NULL, ARCHIVE_ROOT_PARENT,
        chunk_root, 2, 0u, 0.1f, &r);
    int idx_mid = archive_insert_same_quality(a, key_mid, &snap, NULL, idx_root,
        chunk_mid, 3, 0u, 0.2f, &r);
    int idx_tail = archive_insert_same_quality(a, key_tail, &snap, NULL, idx_mid,
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
        idx[i] = archive_insert_same_quality(a, key, &snap, NULL, ARCHIVE_ROOT_PARENT,
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
        int idx = archive_insert_same_quality(a, key, &snap, NULL, ARCHIVE_ROOT_PARENT,
            NULL, 0, 0u, 0.1f, &r);
        ASSERT_TRUE("fits while under capacity", idx >= 0);
    }
    uint8_t over_key[ARCHIVE_KEY_SIZE];
    make_key(over_key, 999);
    int over = archive_insert_same_quality(a, over_key, &snap, NULL, ARCHIVE_ROOT_PARENT,
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
    int idx_a = archive_insert_same_quality(a, key_a, &snap, hs_a, ARCHIVE_ROOT_PARENT,
        NULL, 0, 0u, 0.5f, &r);
    ASSERT_TRUE("insert with hidden state ok", idx_a >= 0);

    const float* hs_back = (const float*)archive_get_hidden_state(a, idx_a);
    ASSERT_TRUE("hidden state retrievable", hs_back != NULL);
    int diff = memcmp(hs_back, hs_a, hidden_state_size);
    ASSERT_INT_EQ("hidden state byte-identical to insert", diff, 0);

    /* entry 2: insert with NULL hidden state (should be zero-filled) */
    uint8_t key_b[ARCHIVE_KEY_SIZE];
    make_key(key_b, 51);
    int idx_b = archive_insert_same_quality(a, key_b, &snap, NULL, ARCHIVE_ROOT_PARENT,
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
    int idx_a2 = archive_insert_same_quality(a, key_a, &snap, hs_a2, ARCHIVE_ROOT_PARENT,
        NULL, 0, 0u, 0.9f, &r);
    ASSERT_INT_EQ("replace returns same idx", idx_a2, idx_a);
    ASSERT_INT_EQ("replace result", (int)r, (int)ARCHIVE_INSERT_STRUCTURAL_REPLACED);
    const float* hs_back2 = (const float*)archive_get_hidden_state(a, idx_a);
    int diff2 = memcmp(hs_back2, hs_a2, hidden_state_size);
    ASSERT_INT_EQ("hidden state replaced on structural improvement", diff2, 0);

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
    int idx = archive_insert_same_quality(a, key, &snap, NULL, ARCHIVE_ROOT_PARENT,
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

    int idx_root = archive_insert_same_quality(a, k_root, &snap, hs, ARCHIVE_ROOT_PARENT,
        NULL, 0, 0u, 0.1f, &r);
    int idx_mid  = archive_insert_same_quality(a, k_mid,  &snap, hs, idx_root,
        chunk, 2, 7u, 0.4f, &r);
    int idx_leaf = archive_insert_same_quality(a, k_leaf, &snap, hs, idx_mid,
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

static void test_archive_load_rejects_v1_archives(void) {
    printf("--- archive load rejects v1 archives ---\n");
    const char* path = "/tmp/test_archive_v1.bin";
    FILE* fp = fopen(path, "wb");
    ASSERT_TRUE("scratch file opened", fp != NULL);

    ArchiveFileHeader h = {
        .magic = ARCHIVE_FILE_MAGIC,
        .version = 1u,
        .capacity = 4u,
        .snapshot_size = DUMMY_SNAP_SIZE,
        .num_atns = DUMMY_NUM_ATNS,
        .hidden_state_size = 0u,
        .num_entries = 0u,
        .action_chunk_pool_used_ints = 0u,
        .rng_state = 1u,
    };
    fwrite(&h, sizeof(h), 1, fp);
    fclose(fp);

    Archive* a = archive_load(path);
    ASSERT_TRUE("load returns NULL on v1 archive", a == NULL);
    remove(path);
}

static void test_archive_export_top_k_demos(void) {
    printf("--- archive export uses structural quality and replaced trajectory ---\n");

    Archive* a = archive_create(DUMMY_CAPACITY, DUMMY_SNAP_SIZE,
        DUMMY_NUM_ATNS, DUMMY_CHUNK_POOL_INTS, 0, 999u);

    uint8_t k_root[ARCHIVE_KEY_SIZE];
    uint8_t k_leaf[ARCHIVE_KEY_SIZE];
    make_key(k_root, 500);
    make_key(k_leaf, 501);

    DummySnap root_snap = { .tag = 10, .payload = 10 };
    DummySnap old_leaf_snap = { .tag = 20, .payload = 20 };
    DummySnap new_leaf_snap = { .tag = 30, .payload = 30 };

    int old_chunk[2 * DUMMY_NUM_ATNS] = { 1,1,1,1, 2,2,2,2 };
    int new_chunk[2 * DUMMY_NUM_ATNS] = { 8,8,8,8, 9,9,9,9 };

    ArchiveInsertResult r;
    int root = archive_insert(a, k_root, &root_snap, NULL, ARCHIVE_ROOT_PARENT,
        NULL, 0, 7u, 0.5f, 0.5f, &r);
    ASSERT_TRUE("root inserted", root >= 0);

    int leaf = archive_insert(a, k_leaf, &old_leaf_snap, NULL, root,
        old_chunk, 2, 17u, 0.9f, 0.4f, &r);
    ASSERT_TRUE("old leaf inserted", leaf >= 0);

    int replaced_leaf = archive_insert(a, k_leaf, &new_leaf_snap, NULL, root,
        new_chunk, 2, 23u, 0.1f, 1.2f, &r);
    ASSERT_INT_EQ("leaf replaced in place", replaced_leaf, leaf);
    ASSERT_INT_EQ("replacement result", (int)r,
        (int)ARCHIVE_INSERT_STRUCTURAL_REPLACED);

    const char* dir = "/tmp/test_archive_demos";
    mkdir(dir, 0755);

    int written = archive_export_top_k_demos(a, dir, 10, 100);
    ASSERT_INT_EQ("one demo file written", written, 1);

    char top_path[1024];
    snprintf(top_path, sizeof(top_path), "%s/demo_0000_q1.200_t2.bin", dir);
    FILE* fp = fopen(top_path, "rb");
    ASSERT_TRUE("top demo file opens", fp != NULL);

    Phase2DemoHeader h;
    int read_ok = fread(&h, sizeof(h), 1, fp) == 1;
    ASSERT_INT_EQ("top demo header reads", read_ok ? 1 : 0, 1);
    ASSERT_INT_EQ("top demo n_ticks == 2", (int)h.num_ticks, 2);
    ASSERT_INT_EQ("top demo rng_seed matches chain root", (int)h.rng_seed, 7);
    ASSERT_INT_EQ("top demo quality uses structural quality",
        (int)(h.quality * 1000.0f), 1200);
    ASSERT_INT_EQ("top demo snapshot count", (int)h.num_snapshots, 2);

    DummySnap snapshots[2];
    size_t snaps_read = fread(snapshots, sizeof(DummySnap), 2, fp);
    ASSERT_INT_EQ("snapshots read", (int)snaps_read, 2);
    ASSERT_INT_EQ("leaf snapshot is replaced trajectory", snapshots[1].tag, 30);

    int ticks[2] = {0};
    size_t ticks_read = fread(ticks, sizeof(int), 2, fp);
    ASSERT_INT_EQ("snapshot ticks read", (int)ticks_read, 2);

    int actions[2 * DUMMY_NUM_ATNS] = {0};
    size_t got = fread(actions, sizeof(int),
        (size_t)h.num_ticks * DUMMY_NUM_ATNS, fp);
    ASSERT_INT_EQ("top demo action bytes match",
        (int)got, (int)h.num_ticks * DUMMY_NUM_ATNS);
    int actions_eq = memcmp(actions, new_chunk, sizeof(new_chunk)) == 0;
    ASSERT_INT_EQ("top demo actions match replacement chunk", actions_eq, 1);
    fclose(fp);

    remove(top_path);
    rmdir(dir);

    archive_destroy(a);
}

int main(void) {
    test_archive_create_and_destroy();
    test_archive_insert_lookup_round_trip();
    test_archive_duplicate_updates_sampling_and_replaces_better_structure();
    test_archive_duplicate_sampling_updates_when_structural_pool_is_full();
    test_archive_action_chunk_roundtrip_and_replay();
    test_archive_sample_weights_high_quality();
    test_archive_full_returns_null();
    test_archive_hidden_state_round_trip();
    test_archive_no_hidden_state_returns_null();
    test_archive_save_load_round_trip();
    test_archive_load_rejects_bad_magic();
    test_archive_load_rejects_v1_archives();
    test_archive_export_top_k_demos();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_failed == 0) ? 0 : 1;
}
