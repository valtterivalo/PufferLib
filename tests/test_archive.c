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
        DUMMY_NUM_ATNS, DUMMY_CHUNK_POOL_INTS, 42u);
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
        DUMMY_NUM_ATNS, DUMMY_CHUNK_POOL_INTS, 42u);

    DummySnap snap = { .tag = 0xAA, .payload = 7 };
    uint8_t key[ARCHIVE_KEY_SIZE];
    make_key(key, 1);

    ArchiveInsertResult r = (ArchiveInsertResult)-1;
    int idx = archive_insert(a, key, &snap, ARCHIVE_ROOT_PARENT,
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

static void test_archive_duplicate_replaces_on_higher_quality(void) {
    printf("--- archive duplicate replaces on strictly higher quality ---\n");
    Archive* a = archive_create(DUMMY_CAPACITY, DUMMY_SNAP_SIZE,
        DUMMY_NUM_ATNS, DUMMY_CHUNK_POOL_INTS, 42u);

    uint8_t key[ARCHIVE_KEY_SIZE];
    make_key(key, 5);

    DummySnap a_snap = { .tag = 1, .payload = 100 };
    ArchiveInsertResult r = (ArchiveInsertResult)-1;
    int idx_a = archive_insert(a, key, &a_snap, ARCHIVE_ROOT_PARENT,
        NULL, 0, 0u, 0.3f, &r);
    ASSERT_INT_EQ("first insert NEW", (int)r, (int)ARCHIVE_INSERT_NEW);

    /* lower quality: kept, seen++ */
    DummySnap b_snap = { .tag = 2, .payload = 200 };
    int idx_b = archive_insert(a, key, &b_snap, ARCHIVE_ROOT_PARENT,
        NULL, 0, 0u, 0.2f, &r);
    ASSERT_INT_EQ("lower quality returns same idx", idx_b, idx_a);
    ASSERT_INT_EQ("lower quality result KEPT", (int)r, (int)ARCHIVE_INSERT_KEPT);
    ASSERT_INT_EQ("seen incremented", (int)a->entries[idx_a].seen, 2);
    const DummySnap* still_a = (const DummySnap*)archive_get_snapshot(a, idx_a);
    ASSERT_INT_EQ("snapshot still tag=1", still_a->tag, 1);

    /* higher quality: replaces */
    DummySnap c_snap = { .tag = 3, .payload = 300 };
    int idx_c = archive_insert(a, key, &c_snap, ARCHIVE_ROOT_PARENT,
        NULL, 0, 0u, 0.7f, &r);
    ASSERT_INT_EQ("higher quality returns same idx", idx_c, idx_a);
    ASSERT_INT_EQ("higher quality result REPLACED", (int)r, (int)ARCHIVE_INSERT_REPLACED);
    const DummySnap* now_c = (const DummySnap*)archive_get_snapshot(a, idx_a);
    ASSERT_INT_EQ("snapshot replaced to tag=3", now_c->tag, 3);
    ASSERT_INT_EQ("snapshot payload replaced", now_c->payload, 300);

    /* equal quality: kept (strict) */
    DummySnap d_snap = { .tag = 4, .payload = 400 };
    int idx_d = archive_insert(a, key, &d_snap, ARCHIVE_ROOT_PARENT,
        NULL, 0, 0u, 0.7f, &r);
    ASSERT_INT_EQ("equal quality returns same idx", idx_d, idx_a);
    ASSERT_INT_EQ("equal quality result KEPT", (int)r, (int)ARCHIVE_INSERT_KEPT);
    const DummySnap* still_c = (const DummySnap*)archive_get_snapshot(a, idx_a);
    ASSERT_INT_EQ("equal quality leaves tag=3", still_c->tag, 3);

    archive_destroy(a);
}

static void test_archive_action_chunk_roundtrip_and_replay(void) {
    printf("--- archive action chunk replay walks parent chain ---\n");
    Archive* a = archive_create(DUMMY_CAPACITY, DUMMY_SNAP_SIZE,
        DUMMY_NUM_ATNS, DUMMY_CHUNK_POOL_INTS, 42u);

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
    int idx_root = archive_insert(a, key_root, &snap, ARCHIVE_ROOT_PARENT,
        chunk_root, 2, 0u, 0.1f, &r);
    int idx_mid = archive_insert(a, key_mid, &snap, idx_root,
        chunk_mid, 3, 0u, 0.2f, &r);
    int idx_tail = archive_insert(a, key_tail, &snap, idx_mid,
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
        DUMMY_NUM_ATNS, DUMMY_CHUNK_POOL_INTS, 0xC0FFEEu);

    DummySnap snap = { .tag = 0, .payload = 0 };

    /* insert 4 cells with quality 0.0, 0.25, 0.50, 1.0. equal counts to start. */
    int idx[4];
    float qs[] = { 0.0f, 0.25f, 0.5f, 1.0f };
    ArchiveInsertResult r;
    for (int i = 0; i < 4; i++) {
        uint8_t key[ARCHIVE_KEY_SIZE];
        make_key(key, 200 + i);
        idx[i] = archive_insert(a, key, &snap, ARCHIVE_ROOT_PARENT,
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
        DUMMY_NUM_ATNS, DUMMY_CHUNK_POOL_INTS, 7u);
    DummySnap snap = { .tag = 0, .payload = 0 };
    ArchiveInsertResult r;
    for (int i = 0; i < cap; i++) {
        uint8_t key[ARCHIVE_KEY_SIZE];
        make_key(key, 300 + i);
        int idx = archive_insert(a, key, &snap, ARCHIVE_ROOT_PARENT,
            NULL, 0, 0u, 0.1f, &r);
        ASSERT_TRUE("fits while under capacity", idx >= 0);
    }
    uint8_t over_key[ARCHIVE_KEY_SIZE];
    make_key(over_key, 999);
    int over = archive_insert(a, over_key, &snap, ARCHIVE_ROOT_PARENT,
        NULL, 0, 0u, 0.1f, &r);
    ASSERT_INT_EQ("over-capacity returns null index", over, ARCHIVE_NULL_INDEX);
    ASSERT_INT_EQ("over-capacity reports FULL", (int)r, (int)ARCHIVE_INSERT_FULL);
    archive_destroy(a);
}

int main(void) {
    test_archive_create_and_destroy();
    test_archive_insert_lookup_round_trip();
    test_archive_duplicate_replaces_on_higher_quality();
    test_archive_action_chunk_roundtrip_and_replay();
    test_archive_sample_weights_high_quality();
    test_archive_full_returns_null();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_failed == 0) ? 0 : 1;
}
