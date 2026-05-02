/* DemoStore: holds top-K archive demos for phase 2.
 *
 * File format v2 (magic 'P2DM' version 2):
 *   Phase2DemoHeader header
 *   uint8  snapshots[num_snapshots * snapshot_size]   (chain root -> leaf)
 *   int32  snapshot_ticks[num_snapshots]              (cumulative ticks per slot)
 *   int32  actions[num_ticks * num_atns]              (root-to-leaf action chain)
 *
 * The snapshot pool covers each archive cell along the demo's parent chain.
 * Slot 0 is the chain root (low q, often c_reset state). Last slot is the
 * leaf (high q). Ladder builders memcpy this directly; no replay needed.
 * Action sequence is retained for diagnostic forward replay and per-slot BC
 * supervision (the action at slot s is actions[snapshot_ticks[s]]). */

#ifndef DEMOSTORE_H
#define DEMOSTORE_H

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEMOSTORE_MAX_NUM_ATNS 16
#define PHASE2_DEMO_MAGIC 0x4D443250u  /* 'P2DM' little-endian */
#define PHASE2_DEMO_VERSION 2u

typedef struct {
    uint32_t magic;
    uint32_t version;
    int32_t num_ticks;
    int32_t num_atns;
    uint32_t rng_seed;
    float quality;
    uint32_t snapshot_size;
    int32_t num_snapshots;
} Phase2DemoHeader;

typedef struct {
    int demo_id;
    int length_ticks;
    int num_atns;
    uint32_t rng_seed;
    float quality_at_root;
    uint32_t snapshot_size;
    int num_snapshots;
    uint8_t* snapshots;
    int* snapshot_ticks;
    int* actions;
    int cursor_tick;
} DemoTrajectory;

typedef struct {
    int capacity;
    int num_demos;
    DemoTrajectory* demos;
} DemoStore;

static inline DemoStore* demostore_create(int capacity) {
    if (capacity <= 0) return NULL;
    DemoStore* s = (DemoStore*)calloc(1, sizeof(DemoStore));
    s->capacity = capacity;
    s->demos = (DemoTrajectory*)calloc((size_t)capacity, sizeof(DemoTrajectory));
    return s;
}

static inline void demostore_destroy(DemoStore* s) {
    if (!s) return;
    for (int i = 0; i < s->num_demos; i++) {
        free(s->demos[i].actions);
        free(s->demos[i].snapshots);
        free(s->demos[i].snapshot_ticks);
    }
    free(s->demos);
    free(s);
}

/* Load one Phase2Demo file from `path`. `num_atns` and `expected_snapshot_size`
   are validated against the header. If `parse_filename_q` is non-zero, parses
   quality from the filename convention "demo_*_q<f>_t<n>.bin" and overrides the
   header value (older filenames may be more reliable than re-export q). Returns
   the new demo_id, or -1 on any mismatch / read failure / capacity full. */
static inline int demostore_load_demo(
    DemoStore* s,
    const char* path,
    int num_atns,
    int parse_filename_q,
    uint32_t expected_snapshot_size
) {
    if (s->num_demos >= s->capacity || num_atns <= 0 ||
        num_atns > DEMOSTORE_MAX_NUM_ATNS) return -1;

    FILE* f = fopen(path, "rb");
    if (!f) return -1;

    Phase2DemoHeader h;
    if (fread(&h, sizeof(h), 1, f) != 1) { fclose(f); return -1; }
    if (h.magic != PHASE2_DEMO_MAGIC || h.version != PHASE2_DEMO_VERSION) {
        fprintf(stderr,
            "demostore_load_demo: bad magic/version in %s "
            "(got 0x%08x v%u, want 0x%08x v%u)\n",
            path, h.magic, h.version, PHASE2_DEMO_MAGIC, PHASE2_DEMO_VERSION);
        fclose(f); return -1;
    }
    if (h.num_atns != num_atns) {
        fprintf(stderr,
            "demostore_load_demo: %s num_atns=%d, expected %d\n",
            path, h.num_atns, num_atns);
        fclose(f); return -1;
    }
    if (h.snapshot_size > expected_snapshot_size) {
        fprintf(stderr,
            "demostore_load_demo: %s snapshot_size=%u > expected %u "
            "(archive newer than runtime)\n",
            path, h.snapshot_size, expected_snapshot_size);
        fclose(f); return -1;
    }
    if (h.num_ticks <= 0 || h.num_ticks > 1024 * 1024) { fclose(f); return -1; }
    if (h.num_snapshots <= 0 || h.num_snapshots > h.num_ticks + 1) { fclose(f); return -1; }

    /* allocate the runtime size per snapshot and zero-pad each tail. older
       archives missed trailing fields like the Log accumulator extension. */
    uint8_t* snapshots = (uint8_t*)calloc(
        (size_t)h.num_snapshots, expected_snapshot_size);
    for (int i = 0; i < h.num_snapshots; i++) {
        uint8_t* slot = snapshots + (size_t)i * (size_t)expected_snapshot_size;
        if (fread(slot, 1, h.snapshot_size, f) != h.snapshot_size) {
            free(snapshots); fclose(f); return -1;
        }
    }

    int* snapshot_ticks = (int*)malloc((size_t)h.num_snapshots * sizeof(int));
    if (fread(snapshot_ticks, sizeof(int), h.num_snapshots, f) != (size_t)h.num_snapshots) {
        free(snapshots); free(snapshot_ticks); fclose(f); return -1;
    }

    size_t expected = (size_t)h.num_ticks * (size_t)num_atns;
    int* actions = (int*)malloc(expected * sizeof(int));
    if (fread(actions, sizeof(int), expected, f) != expected) {
        free(snapshots); free(snapshot_ticks); free(actions); fclose(f); return -1;
    }

    long here = ftell(f);
    fseek(f, 0, SEEK_END);
    long end = ftell(f);
    fclose(f);
    if (here != end) {
        fprintf(stderr,
            "demostore_load_demo: trailing bytes in %s\n", path);
        free(snapshots); free(snapshot_ticks); free(actions); return -1;
    }

    float q_root = h.quality;
    if (parse_filename_q) {
        const char* slash = strrchr(path, '/');
        const char* q_marker = strstr(slash ? slash + 1 : path, "_q");
        if (q_marker) q_root = (float)atof(q_marker + 2);
    }

    int demo_id = s->num_demos++;
    DemoTrajectory* d = &s->demos[demo_id];
    d->demo_id = demo_id;
    d->length_ticks = h.num_ticks;
    d->num_atns = num_atns;
    d->rng_seed = h.rng_seed;
    d->quality_at_root = q_root;
    d->snapshot_size = expected_snapshot_size;
    d->num_snapshots = h.num_snapshots;
    d->snapshots = snapshots;
    d->snapshot_ticks = snapshot_ticks;
    d->actions = actions;
    d->cursor_tick = snapshot_ticks[h.num_snapshots - 1];
    return demo_id;
}

static inline const int* demostore_actions_at(
    const DemoStore* s, int demo_id, int tick
) {
    const DemoTrajectory* d = &s->demos[demo_id];
    return &d->actions[(size_t)tick * (size_t)d->num_atns];
}

/* Sort + load all .bin files in `dir` (up to max_demos). Returns number
   loaded, or -1 on opendir failure. Aborts on individual load failures. */
static inline int qsort_strcmp_(const void* a, const void* b) {
    return strcmp(*(const char**)a, *(const char**)b);
}
static inline int demostore_load_dir(
    DemoStore* s, const char* dir, int num_atns, int parse_q, int max_demos,
    uint32_t expected_snapshot_size
) {
    DIR* d = opendir(dir);
    if (!d) return -1;
    char** names = (char**)calloc(1024, sizeof(char*));
    int n = 0;
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL && n < 1024) {
        const char* name = ent->d_name;
        size_t len = strlen(name);
        if (len < 4 || strcmp(name + len - 4, ".bin") != 0) continue;
        names[n++] = strdup(name);
    }
    closedir(d);
    qsort(names, (size_t)n, sizeof(char*), qsort_strcmp_);
    if (max_demos > 0 && n > max_demos) n = max_demos;

    char path[1024];
    int loaded = 0;
    for (int i = 0; i < n; i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        if (demostore_load_demo(s, path, num_atns, parse_q,
                                expected_snapshot_size) >= 0) loaded++;
    }
    for (int i = 0; i < n; i++) free(names[i]);
    free(names);
    return loaded;
}

/* Per-demo snapshot ladder: env snapshots at ticks 0, stride, 2*stride, ...
   for backward-curriculum restores. Hidden pool optional; v0 keeps it NULL
   and warms up the recurrent state after restore. The encounter binding
   fills these via c_step replay. */

typedef struct {
    int demo_id;
    int snapshot_stride;
    int num_snapshots;
    int* snapshot_ticks;
    uint8_t* snapshot_pool;
    uint8_t* hidden_pool;
    size_t snapshot_size;
    size_t hidden_size;
} DemoSnapshotLadder;

static inline DemoSnapshotLadder* demo_snapshot_ladder_create(
    int demo_id, int snapshot_stride, int num_snapshots,
    size_t snapshot_size, size_t hidden_size_or_zero
) {
    if (snapshot_stride <= 0 || num_snapshots <= 0 || snapshot_size == 0) return NULL;
    DemoSnapshotLadder* l = (DemoSnapshotLadder*)calloc(1, sizeof(*l));
    l->demo_id = demo_id;
    l->snapshot_stride = snapshot_stride;
    l->num_snapshots = num_snapshots;
    l->snapshot_size = snapshot_size;
    l->hidden_size = hidden_size_or_zero;
    l->snapshot_ticks = (int*)calloc((size_t)num_snapshots, sizeof(int));
    l->snapshot_pool = (uint8_t*)calloc((size_t)num_snapshots, snapshot_size);
    if (hidden_size_or_zero > 0) {
        l->hidden_pool = (uint8_t*)calloc((size_t)num_snapshots, hidden_size_or_zero);
    }
    return l;
}

static inline void demo_snapshot_ladder_destroy(DemoSnapshotLadder* l) {
    if (!l) return;
    free(l->snapshot_ticks);
    free(l->snapshot_pool);
    free(l->hidden_pool);
    free(l);
}

static inline int demo_snapshot_ladder_count_for_length(int length_ticks, int stride) {
    if (length_ticks <= 0 || stride <= 0) return 0;
    return 1 + (length_ticks - 1) / stride;
}

static inline const void* demo_snapshot_ladder_snapshot_at(const DemoSnapshotLadder* l, int i) {
    if (!l || i < 0 || i >= l->num_snapshots) return NULL;
    return l->snapshot_pool + (size_t)i * l->snapshot_size;
}

static inline const void* demo_snapshot_ladder_hidden_at(const DemoSnapshotLadder* l, int i) {
    if (!l || !l->hidden_pool || i < 0 || i >= l->num_snapshots) return NULL;
    return l->hidden_pool + (size_t)i * l->hidden_size;
}

/* Locate the slot whose snapshot_tick is closest to (and not exceeding) tick.
   Snapshot ticks are irregular (chain chunk lengths vary), so this is a
   linear search. Returns the index of the largest slot s with
   snapshot_ticks[s] <= tick, or 0 if tick is below the first slot. */
static inline int demo_snapshot_ladder_slot_for_tick(const DemoSnapshotLadder* l, int tick) {
    if (!l || tick < 0 || l->num_snapshots <= 0) return -1;
    int slot = 0;
    for (int s = 1; s < l->num_snapshots; s++) {
        if (l->snapshot_ticks[s] <= tick) slot = s;
        else break;
    }
    return slot;
}

/* env->observations captured at every chain slot; one obs vector per slot,
   not per tick (action replay does not reproduce the archive trajectory, so
   ticks between slots have no recoverable state). For per-slot BC, action
   at slot s is actions[snapshot_ticks[s]] from the demo's action chain. */
typedef struct {
    int demo_id;
    int num_slots;
    int obs_floats_per_slot;
    float* obs;
} DemoObsCache;

static inline DemoObsCache* demo_obs_cache_create(
    int demo_id, int num_slots, int obs_floats_per_slot
) {
    DemoObsCache* c = (DemoObsCache*)calloc(1, sizeof(*c));
    c->demo_id = demo_id;
    c->num_slots = num_slots;
    c->obs_floats_per_slot = obs_floats_per_slot;
    c->obs = (float*)calloc((size_t)num_slots * (size_t)obs_floats_per_slot, sizeof(float));
    return c;
}

static inline void demo_obs_cache_destroy(DemoObsCache* c) {
    if (!c) return;
    free(c->obs);
    free(c);
}

#ifdef __cplusplus
}
#endif

#endif /* DEMOSTORE_H */
