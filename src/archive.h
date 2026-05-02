/**
 * @file archive.h
 * @brief Cell-keyed archive for Go-Explore-style exploration.
 *
 * Stores encounter snapshots indexed by a fixed-size cell key (16 bytes). Hash
 * table with linear probing for O(1) lookup. Snapshot blobs and action chunks
 * live in fixed-capacity pools; the archive does not realloc, so capacity must
 * be sized up front.
 *
 * Each entry tracks (parent_idx, action_chunk) so a full replay back to root
 * can be materialized by walking parents — used for demo export.
 *
 * Cell selection uses the count-decay heuristic from Ecoffet et al. 2021.
 * Quality is a scalar in roughly [0, 1.5] supplied by the encounter; higher
 * quality cells are preferred, but cells that have been chosen rarely (or
 * recently produced new discoveries) get a bonus to keep exploration spread.
 */

#ifndef ARCHIVE_H
#define ARCHIVE_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "demostore.h"

#define ARCHIVE_KEY_SIZE 16
#define ARCHIVE_NULL_INDEX -1
#define ARCHIVE_ROOT_PARENT -1

typedef struct {
    uint8_t key[ARCHIVE_KEY_SIZE];
    int parent_idx;              /* ARCHIVE_ROOT_PARENT for the seed cell */
    int action_chunk_offset;     /* offset into action_chunk_pool (in ints), -1 if root */
    int action_chunk_len;        /* number of ticks in the chunk */
    uint32_t rng_seed;           /* RNG state captured at the start of the chunk */
    float quality;
    uint32_t chosen;             /* times this cell was selected from the archive */
    uint32_t seen;               /* times we observed a state mapping to this cell */
    uint32_t chosen_since_new;   /* chosen count since this cell last produced a new/improved entry */
} ArchiveEntry;

typedef struct {
    int* bucket_to_entry;        /* hash bucket -> entry index, or ARCHIVE_NULL_INDEX */
    int num_buckets;             /* power of 2 */

    ArchiveEntry* entries;
    int num_entries;
    int capacity;

    uint8_t* snapshot_pool;      /* dense: snapshot[i] at offset i * snapshot_size */
    size_t snapshot_size;        /* bytes per snapshot (per-encounter constant) */

    int* action_chunk_pool;      /* ints; action chunk i has num_atns * len entries */
    int action_chunk_pool_capacity_ints;
    int action_chunk_pool_used_ints;
    int num_atns;                /* ints per tick */

    /* Optional per-entry hidden state pool for recurrent policies. When
       hidden_state_size == 0 no pool is allocated and inserts ignore the
       hidden state argument. When > 0 each entry has a fixed hidden_state_size
       slot; inserting NULL writes zeros. */
    uint8_t* hidden_state_pool;
    size_t hidden_state_size;

    uint64_t rng_state;          /* xorshift state for sampling */
} Archive;


static inline uint64_t archive_hash_key(const uint8_t* key) {
    /* FNV-1a 64-bit */
    uint64_t h = 14695981039346656037ULL;
    for (int i = 0; i < ARCHIVE_KEY_SIZE; i++) {
        h ^= (uint64_t)key[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static inline uint32_t archive_rand_u32(Archive* a) {
    /* xorshift64 */
    uint64_t x = a->rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    a->rng_state = x;
    return (uint32_t)(x >> 32);
}

static inline float archive_rand_float01(Archive* a) {
    return (float)archive_rand_u32(a) / 4294967296.0f;
}

static inline int archive_next_pow2(int n) {
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}


static inline Archive* archive_create(
    int capacity,
    size_t snapshot_size,
    int num_atns,
    int action_chunk_pool_capacity_ints,
    size_t hidden_state_size,
    uint64_t seed
) {
    if (capacity <= 0 || snapshot_size == 0 || num_atns <= 0) return NULL;

    Archive* a = (Archive*)calloc(1, sizeof(Archive));
    if (!a) return NULL;

    a->capacity = capacity;
    a->snapshot_size = snapshot_size;
    a->num_atns = num_atns;
    a->action_chunk_pool_capacity_ints = action_chunk_pool_capacity_ints;
    a->hidden_state_size = hidden_state_size;
    a->rng_state = seed ? seed : 0x12345678abcdef01ULL;

    /* hash table sized 2x capacity for low load factor, rounded up to pow2,
       minimum 16 buckets so the modulo math stays cheap on tiny archives. */
    a->num_buckets = archive_next_pow2(capacity * 2);
    if (a->num_buckets < 16) a->num_buckets = 16;

    a->bucket_to_entry = (int*)malloc((size_t)a->num_buckets * sizeof(int));
    a->entries = (ArchiveEntry*)calloc((size_t)capacity, sizeof(ArchiveEntry));
    a->snapshot_pool = (uint8_t*)malloc((size_t)capacity * snapshot_size);
    a->action_chunk_pool = (int*)malloc((size_t)action_chunk_pool_capacity_ints * sizeof(int));
    a->hidden_state_pool = (hidden_state_size > 0)
        ? (uint8_t*)malloc((size_t)capacity * hidden_state_size)
        : NULL;

    if (!a->bucket_to_entry || !a->entries ||
        !a->snapshot_pool || !a->action_chunk_pool ||
        (hidden_state_size > 0 && !a->hidden_state_pool)) {
        free(a->bucket_to_entry);
        free(a->entries);
        free(a->snapshot_pool);
        free(a->action_chunk_pool);
        free(a->hidden_state_pool);
        free(a);
        return NULL;
    }

    for (int i = 0; i < a->num_buckets; i++) {
        a->bucket_to_entry[i] = ARCHIVE_NULL_INDEX;
    }
    return a;
}

static inline void archive_destroy(Archive* a) {
    if (!a) return;
    free(a->bucket_to_entry);
    free(a->entries);
    free(a->snapshot_pool);
    free(a->action_chunk_pool);
    free(a->hidden_state_pool);
    free(a);
}


/* Find the bucket for a key. Returns the bucket index whose bucket_to_entry
   either holds the matching entry's index (found) or is ARCHIVE_NULL_INDEX
   (not found, caller may insert here). Returns -1 if the table is full. */
static inline int archive_find_bucket(const Archive* a, const uint8_t* key) {
    uint64_t h = archive_hash_key(key);
    int mask = a->num_buckets - 1;
    int b = (int)(h & (uint64_t)mask);

    for (int probe = 0; probe < a->num_buckets; probe++) {
        int idx = (b + probe) & mask;
        int e = a->bucket_to_entry[idx];
        if (e == ARCHIVE_NULL_INDEX) return idx;
        if (memcmp(a->entries[e].key, key, ARCHIVE_KEY_SIZE) == 0) return idx;
    }
    return -1;
}

static inline int archive_lookup(const Archive* a, const uint8_t* key) {
    int b = archive_find_bucket(a, key);
    if (b < 0) return ARCHIVE_NULL_INDEX;
    return a->bucket_to_entry[b];
}


/* Result of archive_insert: how the request was resolved. */
typedef enum {
    ARCHIVE_INSERT_NEW,         /* new cell created */
    ARCHIVE_INSERT_REPLACED,    /* existing cell, quality stat improved (no structural rewire) */
    ARCHIVE_INSERT_KEPT,        /* existing cell, quality not better, only seen++ */
    ARCHIVE_INSERT_FULL         /* no room (entry capacity or action chunk pool exhausted) */
} ArchiveInsertResult;

/* Insert or re-discover a cell. First-write wins for structural fields:
   rewiring parent_idx on re-discovery would invalidate every descendant's
   action_chunk and frequently produce parent-chain cycles. Re-discoveries
   only bump `seen` and the `quality` stat used by count_decay sampling. */
static inline int archive_insert(
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
    int b = archive_find_bucket(a, key);
    if (b < 0) {
        if (out_result) *out_result = ARCHIVE_INSERT_FULL;
        return ARCHIVE_NULL_INDEX;
    }

    int existing = a->bucket_to_entry[b];
    if (existing != ARCHIVE_NULL_INDEX) {
        ArchiveEntry* e = &a->entries[existing];
        e->seen++;
        if (quality > e->quality) {
            e->quality = quality;
            if (out_result) *out_result = ARCHIVE_INSERT_REPLACED;
        } else if (out_result) {
            *out_result = ARCHIVE_INSERT_KEPT;
        }
        return existing;
    }

    if (a->num_entries >= a->capacity) {
        if (out_result) *out_result = ARCHIVE_INSERT_FULL;
        return ARCHIVE_NULL_INDEX;
    }

    int needed = action_chunk_len * a->num_atns;
    if (action_chunk && needed > 0 &&
        a->action_chunk_pool_used_ints + needed > a->action_chunk_pool_capacity_ints) {
        if (out_result) *out_result = ARCHIVE_INSERT_FULL;
        return ARCHIVE_NULL_INDEX;
    }

    int idx = a->num_entries++;
    ArchiveEntry* e = &a->entries[idx];
    memcpy(e->key, key, ARCHIVE_KEY_SIZE);
    e->parent_idx = parent_idx;
    e->rng_seed = rng_seed;
    e->quality = quality;
    e->chosen = 0;
    e->seen = 1;
    e->chosen_since_new = 0;

    memcpy(&a->snapshot_pool[(size_t)idx * a->snapshot_size],
           snapshot, a->snapshot_size);

    if (a->hidden_state_size > 0) {
        uint8_t* dst = &a->hidden_state_pool[(size_t)idx * a->hidden_state_size];
        if (hidden_state) memcpy(dst, hidden_state, a->hidden_state_size);
        else memset(dst, 0, a->hidden_state_size);
    }

    if (action_chunk && needed > 0) {
        e->action_chunk_offset = a->action_chunk_pool_used_ints;
        e->action_chunk_len = action_chunk_len;
        memcpy(&a->action_chunk_pool[e->action_chunk_offset],
               action_chunk, (size_t)needed * sizeof(int));
        a->action_chunk_pool_used_ints += needed;
    } else {
        e->action_chunk_offset = -1;
        e->action_chunk_len = 0;
    }

    a->bucket_to_entry[b] = idx;
    if (out_result) *out_result = ARCHIVE_INSERT_NEW;
    return idx;
}


static inline const ArchiveEntry* archive_get(const Archive* a, int entry_idx) {
    if (entry_idx < 0 || entry_idx >= a->num_entries) return NULL;
    return &a->entries[entry_idx];
}

static inline const void* archive_get_snapshot(const Archive* a, int entry_idx) {
    if (entry_idx < 0 || entry_idx >= a->num_entries) return NULL;
    return &a->snapshot_pool[(size_t)entry_idx * a->snapshot_size];
}

static inline const int* archive_get_action_chunk(const Archive* a, int entry_idx) {
    if (entry_idx < 0 || entry_idx >= a->num_entries) return NULL;
    const ArchiveEntry* e = &a->entries[entry_idx];
    if (e->action_chunk_offset < 0) return NULL;
    return &a->action_chunk_pool[e->action_chunk_offset];
}

static inline const void* archive_get_hidden_state(const Archive* a, int entry_idx) {
    if (entry_idx < 0 || entry_idx >= a->num_entries) return NULL;
    if (a->hidden_state_size == 0 || !a->hidden_state_pool) return NULL;
    return &a->hidden_state_pool[(size_t)entry_idx * a->hidden_state_size];
}


/* count_decay weight per Ecoffet et al. quality contributes a multiplicative
   bias up to 10x; the count-decay subscore biases toward cells visited or
   chosen rarely. */
static inline double archive_entry_weight(const ArchiveEntry* e) {
    double q = (double)e->quality;
    if (q < 0.0) q = 0.0;
    return (1.0 + 9.0 * q) * (
        0.6 / sqrt((double)e->chosen + 1.0) +
        0.3 / sqrt((double)e->chosen_since_new + 1.0) +
        0.1 / sqrt((double)e->seen + 1.0) +
        1e-4
    );
}

/* Sample an entry weighted by archive_entry_weight. Increments chosen and
   chosen_since_new on the picked entry. Returns ARCHIVE_NULL_INDEX if empty. */
static inline int archive_sample(Archive* a) {
    if (a->num_entries == 0) return ARCHIVE_NULL_INDEX;

    double total = 0.0;
    for (int i = 0; i < a->num_entries; i++) {
        total += archive_entry_weight(&a->entries[i]);
    }

    double r = (double)archive_rand_float01(a) * total;
    double cum = 0.0;
    int picked = a->num_entries - 1;
    for (int i = 0; i < a->num_entries; i++) {
        cum += archive_entry_weight(&a->entries[i]);
        if (r <= cum) { picked = i; break; }
    }

    a->entries[picked].chosen++;
    a->entries[picked].chosen_since_new++;
    return picked;
}


/* Materialize the full action sequence from root to entry_idx by walking
   parent pointers. Writes interleaved actions[tick * num_atns + head] into
   `out` and returns the number of ticks written. Returns -1 if the chain
   exceeds out_capacity_ticks or hits a corrupted entry. */
static inline int archive_replay_actions(
    const Archive* a,
    int entry_idx,
    int* out,
    int out_capacity_ticks
) {
    /* walk parents to count total ticks first. cap at num_entries hops to
       defend against cycles introduced by quality-replace. */
    int total_ticks = 0;
    int idx = entry_idx;
    int hops = 0;
    while (idx != ARCHIVE_ROOT_PARENT) {
        if (hops++ > a->num_entries) return -1;
        const ArchiveEntry* e = archive_get(a, idx);
        if (!e) return -1;
        total_ticks += e->action_chunk_len;
        if (total_ticks > out_capacity_ticks) return -1;
        idx = e->parent_idx;
    }
    if (total_ticks == 0) return 0;

    /* second pass: fill in reverse, then we have it forward by construction */
    int write_end = total_ticks;
    idx = entry_idx;
    hops = 0;
    while (idx != ARCHIVE_ROOT_PARENT) {
        if (hops++ > a->num_entries) return -1;
        const ArchiveEntry* e = archive_get(a, idx);
        const int* chunk = archive_get_action_chunk(a, idx);
        int chunk_ticks = e->action_chunk_len;
        if (chunk_ticks > 0 && chunk) {
            int write_start = write_end - chunk_ticks;
            memcpy(&out[write_start * a->num_atns], chunk,
                   (size_t)chunk_ticks * (size_t)a->num_atns * sizeof(int));
            write_end = write_start;
        }
        idx = e->parent_idx;
    }
    return total_ticks;
}


/* Reset chosen_since_new on the parent of a cell that just produced a new or
   improved discovery. Caller passes the parent index from the originating
   archive_sample. No-op for the root cell. */
static inline void archive_note_discovery_from(Archive* a, int parent_idx) {
    if (parent_idx == ARCHIVE_ROOT_PARENT) return;
    if (parent_idx < 0 || parent_idx >= a->num_entries) return;
    a->entries[parent_idx].chosen_since_new = 0;
}


/* ============================================================================
 * Demo export
 * ==========================================================================*/

typedef struct {
    int leaf_idx;
    float quality;
    int chain_ticks;
} ArchiveDemoCandidate;

static int archive_demo_compare_desc(const void* x, const void* y) {
    const ArchiveDemoCandidate* a = (const ArchiveDemoCandidate*)x;
    const ArchiveDemoCandidate* b = (const ArchiveDemoCandidate*)y;
    if (a->quality > b->quality) return -1;
    if (a->quality < b->quality) return 1;
    /* tie break: shorter trajectory wins (cleaner demo) */
    if (a->chain_ticks < b->chain_ticks) return -1;
    if (a->chain_ticks > b->chain_ticks) return 1;
    return 0;
}

/* Walk parent chain from leaf to root, return the rng_seed of the entry
   whose parent is ARCHIVE_ROOT_PARENT (the first entry of the chain).
   Returns 0u if the chain cycles. */
static inline uint32_t archive_chain_root_rng_seed(const Archive* a, int leaf_idx) {
    int idx = leaf_idx;
    int hops = 0;
    while (idx >= 0) {
        if (hops++ > a->num_entries) return 0u;
        const ArchiveEntry* e = archive_get(a, idx);
        if (!e) return 0u;
        if (e->parent_idx == ARCHIVE_ROOT_PARENT) return e->rng_seed;
        idx = e->parent_idx;
    }
    return 0u;
}

/* Total tick count to walk the chain root->leaf. Returns -1 if the chain has
   a cycle (more hops than archive entries) — quality-replace can rewire a
   cell's parent_idx and produce cycles in pathological cases. */
static inline int archive_chain_tick_count(const Archive* a, int leaf_idx) {
    int total = 0;
    int idx = leaf_idx;
    int hops = 0;
    while (idx >= 0) {
        if (hops++ > a->num_entries) return -1;  /* cycle */
        const ArchiveEntry* e = archive_get(a, idx);
        if (!e) return -1;
        total += e->action_chunk_len;
        if (e->parent_idx == ARCHIVE_ROOT_PARENT) break;
        idx = e->parent_idx;
    }
    return total;
}

/* Walk parent pointers from leaf back to the chain root and return that
   root's archive index. Returns -1 if the chain has a cycle. */
static inline int archive_chain_root_idx(const Archive* a, int leaf_idx) {
    int idx = leaf_idx;
    int hops = 0;
    while (idx >= 0) {
        if (hops++ > a->num_entries) return -1;
        const ArchiveEntry* e = archive_get(a, idx);
        if (!e) return -1;
        if (e->parent_idx == ARCHIVE_ROOT_PARENT) return idx;
        idx = e->parent_idx;
    }
    return -1;
}

/* Export top-K archive cells as Phase2Demo files: header + root snapshot +
   action chain from chain root to leaf. Replaying the action sequence on the
   restored root reproduces the archive trajectory deterministically because
   the RNG state is captured inside the snapshot.

   File names: <output_dir>/demo_<rank>_q<quality>_t<ticks>.bin
   Returns the number of demos actually written. */
static inline int archive_export_top_k_demos(
    const Archive* a,
    const char* output_dir,
    int max_demos,
    int max_replay_ticks
) {
    if (a->num_entries == 0 || max_demos <= 0 || !output_dir) return 0;

    ArchiveDemoCandidate* candidates = (ArchiveDemoCandidate*)malloc(
        (size_t)a->num_entries * sizeof(ArchiveDemoCandidate));
    if (!candidates) return 0;

    int n_candidates = 0;
    for (int i = 0; i < a->num_entries; i++) {
        int chain = archive_chain_tick_count(a, i);
        if (chain <= 0 || chain > max_replay_ticks) continue;
        candidates[n_candidates].leaf_idx = i;
        candidates[n_candidates].quality = a->entries[i].quality;
        candidates[n_candidates].chain_ticks = chain;
        n_candidates++;
    }

    qsort(candidates, n_candidates, sizeof(ArchiveDemoCandidate),
        archive_demo_compare_desc);

    int n_export = (max_demos < n_candidates) ? max_demos : n_candidates;
    int* action_buf = (int*)malloc(
        (size_t)max_replay_ticks * (size_t)a->num_atns * sizeof(int));
    if (!action_buf) {
        free(candidates);
        return 0;
    }

    int written = 0;
    for (int rank = 0; rank < n_export; rank++) {
        int leaf = candidates[rank].leaf_idx;
        int n_ticks = archive_replay_actions(a, leaf, action_buf, max_replay_ticks);
        if (n_ticks <= 0) continue;
        /* embed the LEAF snapshot, not the chain root: action replay does
           not reproduce the archive trajectory (some non-determinism is
           outside InfernoState). The leaf snapshot lands the agent at
           q=quality directly. cursor=0 -> leaf state. */
        uint32_t rng_seed = archive_chain_root_rng_seed(a, leaf);
        const uint8_t* root_snapshot =
            &a->snapshot_pool[(size_t)leaf * a->snapshot_size];

        char path[1024];
        snprintf(path, sizeof(path),
            "%s/demo_%04d_q%.3f_t%d.bin",
            output_dir, rank, candidates[rank].quality, n_ticks);

        FILE* fp = fopen(path, "wb");
        if (!fp) {
            fprintf(stderr, "archive_export_top_k_demos: fopen %s failed\n", path);
            continue;
        }
        Phase2DemoHeader h = {
            .magic = PHASE2_DEMO_MAGIC,
            .version = PHASE2_DEMO_VERSION,
            .num_ticks = n_ticks,
            .num_atns = a->num_atns,
            .rng_seed = rng_seed,
            .quality = candidates[rank].quality,
            .snapshot_size = (uint32_t)a->snapshot_size,
        };
        size_t expected = (size_t)n_ticks * (size_t)a->num_atns;
        int ok =
            fwrite(&h, sizeof(h), 1, fp) == 1 &&
            fwrite(root_snapshot, 1, a->snapshot_size, fp) == a->snapshot_size &&
            fwrite(action_buf, sizeof(int), expected, fp) == expected;
        int close_ok = (fclose(fp) == 0);
        if (ok && close_ok) {
            written++;
        } else {
            fprintf(stderr, "archive_export_top_k_demos: short write %s\n", path);
        }
    }

    free(candidates);
    free(action_buf);
    return written;
}


/* ============================================================================
 * Save / load
 * ==========================================================================*/

#define ARCHIVE_FILE_MAGIC 0x41524356u  /* 'ARCV' little-endian */
#define ARCHIVE_FILE_VERSION 1u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint64_t capacity;
    uint64_t snapshot_size;
    uint64_t num_atns;
    uint64_t hidden_state_size;
    uint64_t num_entries;
    uint64_t action_chunk_pool_used_ints;
    uint64_t rng_state;
} ArchiveFileHeader;

/* Serialize the archive to `path`. Format:
     ArchiveFileHeader
     entries[num_entries]                   (ArchiveEntry, packed)
     snapshot_pool[num_entries]             (snapshot_size bytes each)
     action_chunk_pool[action_chunk_pool_used_ints]  (int32 each)
     hidden_state_pool[num_entries]         (hidden_state_size bytes each, omitted if zero)
   Bucket table is rebuilt at load time from the entry keys. Returns 0 on
   success, -1 on failure (also prints to stderr). */
static inline int archive_save(const Archive* a, const char* path) {
    FILE* fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "archive_save: fopen %s failed\n", path);
        return -1;
    }

    ArchiveFileHeader header = {
        .magic = ARCHIVE_FILE_MAGIC,
        .version = ARCHIVE_FILE_VERSION,
        .capacity = (uint64_t)a->capacity,
        .snapshot_size = (uint64_t)a->snapshot_size,
        .num_atns = (uint64_t)a->num_atns,
        .hidden_state_size = (uint64_t)a->hidden_state_size,
        .num_entries = (uint64_t)a->num_entries,
        .action_chunk_pool_used_ints = (uint64_t)a->action_chunk_pool_used_ints,
        .rng_state = a->rng_state,
    };

    int ok = 1;
    ok &= fwrite(&header, sizeof(header), 1, fp) == 1;
    if (a->num_entries > 0) {
        ok &= fwrite(a->entries, sizeof(ArchiveEntry),
            (size_t)a->num_entries, fp) == (size_t)a->num_entries;
        ok &= fwrite(a->snapshot_pool, a->snapshot_size,
            (size_t)a->num_entries, fp) == (size_t)a->num_entries;
    }
    if (a->action_chunk_pool_used_ints > 0) {
        ok &= fwrite(a->action_chunk_pool, sizeof(int),
            (size_t)a->action_chunk_pool_used_ints, fp)
            == (size_t)a->action_chunk_pool_used_ints;
    }
    if (a->hidden_state_size > 0 && a->num_entries > 0) {
        ok &= fwrite(a->hidden_state_pool, a->hidden_state_size,
            (size_t)a->num_entries, fp) == (size_t)a->num_entries;
    }

    int close_ok = (fclose(fp) == 0);
    if (!ok || !close_ok) {
        fprintf(stderr, "archive_save: short write or close error on %s\n", path);
        return -1;
    }
    return 0;
}

/* Load an archive from disk. Returns NULL on failure. The returned archive's
   action_chunk_pool_capacity is set equal to the loaded used count, so any
   subsequent insert will report ARCHIVE_INSERT_FULL (loaded archives are
   read-only by default). To extend, the caller can re-create with extra
   capacity and re-insert entries. */
static inline Archive* archive_load(const char* path) {
    FILE* fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "archive_load: fopen %s failed\n", path);
        return NULL;
    }

    ArchiveFileHeader header;
    if (fread(&header, sizeof(header), 1, fp) != 1) {
        fprintf(stderr, "archive_load: short read on header\n");
        fclose(fp);
        return NULL;
    }
    if (header.magic != ARCHIVE_FILE_MAGIC) {
        fprintf(stderr, "archive_load: bad magic 0x%08x in %s\n", header.magic, path);
        fclose(fp);
        return NULL;
    }
    if (header.version != ARCHIVE_FILE_VERSION) {
        fprintf(stderr, "archive_load: version mismatch %u (want %u)\n",
            header.version, ARCHIVE_FILE_VERSION);
        fclose(fp);
        return NULL;
    }

    Archive* a = archive_create(
        (int)header.capacity,
        (size_t)header.snapshot_size,
        (int)header.num_atns,
        (int)header.action_chunk_pool_used_ints,
        (size_t)header.hidden_state_size,
        header.rng_state);
    if (!a) {
        fclose(fp);
        return NULL;
    }

    a->num_entries = (int)header.num_entries;
    a->action_chunk_pool_used_ints = (int)header.action_chunk_pool_used_ints;

    int ok = 1;
    if (a->num_entries > 0) {
        ok &= fread(a->entries, sizeof(ArchiveEntry),
            (size_t)a->num_entries, fp) == (size_t)a->num_entries;
        ok &= fread(a->snapshot_pool, a->snapshot_size,
            (size_t)a->num_entries, fp) == (size_t)a->num_entries;
    }
    if (a->action_chunk_pool_used_ints > 0) {
        ok &= fread(a->action_chunk_pool, sizeof(int),
            (size_t)a->action_chunk_pool_used_ints, fp)
            == (size_t)a->action_chunk_pool_used_ints;
    }
    if (a->hidden_state_size > 0 && a->num_entries > 0) {
        ok &= fread(a->hidden_state_pool, a->hidden_state_size,
            (size_t)a->num_entries, fp) == (size_t)a->num_entries;
    }
    fclose(fp);

    if (!ok) {
        fprintf(stderr, "archive_load: short read on body of %s\n", path);
        archive_destroy(a);
        return NULL;
    }

    /* rebuild bucket table from loaded entry keys */
    for (int i = 0; i < a->num_entries; i++) {
        int b = archive_find_bucket(a, a->entries[i].key);
        if (b < 0) {
            fprintf(stderr, "archive_load: hash table full while rebuilding from %s\n", path);
            archive_destroy(a);
            return NULL;
        }
        a->bucket_to_entry[b] = i;
    }
    return a;
}

#endif /* ARCHIVE_H */
