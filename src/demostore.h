/* DemoStore: in-memory store for top-K archive demonstrations used by
 * Go-Explore phase 2 (BC warm start + per-demo backward curriculum).
 *
 * Encounter-agnostic: stores raw action sequences (PLAY_REPLAY format) plus
 * mutable cursor state per demo. The snapshot ladder (stride-spaced env
 * snapshots used as backward-curriculum restore points) lives in a separate
 * file built by the encounter binding because it needs encounter-specific
 * snapshot bytes.
 *
 * File format (per demo, PLAY_REPLAY):
 *   int32   num_ticks
 *   uint32  rng_seed
 *   int32   action[num_ticks * num_atns]   row-major (tick, head)
 *
 * Cursor lifecycle:
 *   - cursor starts at length_ticks - 1 (i.e. "restore from end of demo")
 *   - each env reset that picks this demo restores at jittered cursor_tick
 *   - on a success/attempt batch we either step cursor backwards (success
 *     rate above promote_threshold) or step it forwards (success rate below
 *     demote_threshold). Concrete gate logic lives outside this header so we
 *     can test it independently.
 */

#ifndef DEMOSTORE_H
#define DEMOSTORE_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEMOSTORE_MAX_NUM_ATNS 16

typedef struct {
    int demo_id;             /* index into DemoStore::demos */
    int length_ticks;        /* number of ticks in the action sequence */
    int num_atns;            /* number of multi-discrete heads */
    uint32_t rng_seed;       /* seed for env reset before replaying actions */
    float quality_at_root;   /* archive quality at the chain leaf */
    int* actions;            /* length_ticks * num_atns ints; owned */

    /* Mutable cursor state. Curriculum runner advances/regresses based on
       success rate at the current cursor. */
    int cursor_tick;         /* current restore tick in [0, length_ticks - 1] */
    int attempts;            /* attempts at current cursor since last gate */
    int successes;           /* successes at current cursor since last gate */
    float best_q_from_cursor;/* peak quality reached from current cursor */
    float weight;            /* sampling weight for active pool selection */
} DemoTrajectory;

typedef struct {
    int capacity;            /* max demos that can be held */
    int num_demos;           /* current number of loaded demos */
    DemoTrajectory* demos;
} DemoStore;

/* Create a store sized for up to `capacity` demos. Returns NULL on alloc
   failure. Demos are appended via demostore_load_demo. */
static inline DemoStore* demostore_create(int capacity) {
    if (capacity <= 0) return NULL;
    DemoStore* s = (DemoStore*)calloc(1, sizeof(DemoStore));
    if (!s) return NULL;
    s->capacity = capacity;
    s->demos = (DemoTrajectory*)calloc((size_t)capacity, sizeof(DemoTrajectory));
    if (!s->demos) { free(s); return NULL; }
    return s;
}

static inline void demostore_destroy(DemoStore* s) {
    if (!s) return;
    if (s->demos) {
        for (int i = 0; i < s->num_demos; i++) {
            free(s->demos[i].actions);
        }
        free(s->demos);
    }
    free(s);
}

/* Load one demo from `path` (PLAY_REPLAY format). `num_atns_expected` is
   used to size the action buffer; the file does not carry num_atns so the
   caller must supply it. `quality_at_root` is parsed from the filename
   convention "demo_*_q<f>_t<n>.bin" if `parse_filename_q` is non-zero;
   otherwise it stays at 0. Returns the new demo_id (>= 0) on success or
   -1 on failure. */
static inline int demostore_load_demo(
    DemoStore* s,
    const char* path,
    int num_atns_expected,
    int parse_filename_q
) {
    if (!s || !path || num_atns_expected <= 0 ||
        num_atns_expected > DEMOSTORE_MAX_NUM_ATNS) {
        return -1;
    }
    if (s->num_demos >= s->capacity) return -1;

    FILE* f = fopen(path, "rb");
    if (!f) return -1;

    int n_ticks = 0;
    uint32_t rng_seed = 0;
    if (fread(&n_ticks, sizeof(int), 1, f) != 1) { fclose(f); return -1; }
    if (fread(&rng_seed, sizeof(uint32_t), 1, f) != 1) { fclose(f); return -1; }
    if (n_ticks <= 0 || n_ticks > 1024 * 1024) { fclose(f); return -1; }

    size_t expected = (size_t)n_ticks * (size_t)num_atns_expected;
    int* actions = (int*)malloc(expected * sizeof(int));
    if (!actions) { fclose(f); return -1; }
    if (fread(actions, sizeof(int), expected, f) != expected) {
        free(actions); fclose(f); return -1;
    }

    /* If there is trailing data, this likely means num_atns_expected is wrong.
       Reject so we don't silently load a misaligned action buffer. */
    long here = ftell(f);
    fseek(f, 0, SEEK_END);
    long end = ftell(f);
    fclose(f);
    if (here != end) {
        fprintf(stderr,
            "demostore_load_demo: trailing bytes in %s (num_atns_expected=%d?)\n",
            path, num_atns_expected);
        free(actions);
        return -1;
    }

    float q_root = 0.0f;
    if (parse_filename_q) {
        const char* slash = strrchr(path, '/');
        const char* base = slash ? slash + 1 : path;
        const char* q_marker = strstr(base, "_q");
        if (q_marker) {
            q_root = (float)atof(q_marker + 2);
        }
    }

    int demo_id = s->num_demos++;
    DemoTrajectory* d = &s->demos[demo_id];
    d->demo_id = demo_id;
    d->length_ticks = n_ticks;
    d->num_atns = num_atns_expected;
    d->rng_seed = rng_seed;
    d->quality_at_root = q_root;
    d->actions = actions;

    d->cursor_tick = n_ticks - 1;
    d->attempts = 0;
    d->successes = 0;
    d->best_q_from_cursor = 0.0f;
    d->weight = 1.0f;

    return demo_id;
}

/* Convenience accessor: returns a pointer to the action vector at `tick`,
   or NULL if `tick` is out of range. The pointer is valid for the lifetime
   of the demo. */
static inline const int* demostore_actions_at(
    const DemoStore* s, int demo_id, int tick
) {
    if (!s || demo_id < 0 || demo_id >= s->num_demos) return NULL;
    const DemoTrajectory* d = &s->demos[demo_id];
    if (tick < 0 || tick >= d->length_ticks) return NULL;
    return &d->actions[(size_t)tick * (size_t)d->num_atns];
}

/* ============================================================================
 * Snapshot ladder
 *
 * For each demo we precompute env snapshots at stride-spaced tick positions
 * (0, stride, 2*stride, ...) so the backward-curriculum env-reset path can
 * restore directly to a mid-trajectory state instead of replaying actions
 * from scratch every reset.
 *
 * Hidden state pool is optional. v0 leaves it as NULL and lets the
 * recurrent policy warm up for a few ticks after each restore.
 *
 * The builder lives in the encounter binding because it needs to drive
 * c_step / c_reset / encounter snapshot vtable calls. demostore.h only
 * defines the data structure and create/destroy.
 * ========================================================================== */

typedef struct {
    int demo_id;             /* which demo this ladder is for */
    int snapshot_stride;     /* tick spacing between snapshots */
    int num_snapshots;       /* total snapshots stored */
    int* snapshot_ticks;     /* num_snapshots ints; tick index per snapshot */
    uint8_t* snapshot_pool;  /* num_snapshots * snapshot_size bytes (owned) */
    uint8_t* hidden_pool;    /* num_snapshots * hidden_size bytes or NULL */
    size_t snapshot_size;    /* bytes per encounter snapshot */
    size_t hidden_size;      /* 0 if hidden_pool is NULL */
} DemoSnapshotLadder;

/* Allocate a ladder with capacity for `num_snapshots` snapshots. The caller
   sizes this with stride and demo length. `hidden_size_or_zero` of 0 leaves
   `hidden_pool` NULL. Returns NULL on alloc failure. */
static inline DemoSnapshotLadder* demo_snapshot_ladder_create(
    int demo_id,
    int snapshot_stride,
    int num_snapshots,
    size_t snapshot_size,
    size_t hidden_size_or_zero
) {
    if (snapshot_stride <= 0 || num_snapshots <= 0 || snapshot_size == 0) {
        return NULL;
    }
    DemoSnapshotLadder* l =
        (DemoSnapshotLadder*)calloc(1, sizeof(DemoSnapshotLadder));
    if (!l) return NULL;
    l->demo_id = demo_id;
    l->snapshot_stride = snapshot_stride;
    l->num_snapshots = num_snapshots;
    l->snapshot_size = snapshot_size;
    l->hidden_size = hidden_size_or_zero;
    l->snapshot_ticks = (int*)calloc((size_t)num_snapshots, sizeof(int));
    l->snapshot_pool = (uint8_t*)calloc((size_t)num_snapshots, snapshot_size);
    if (hidden_size_or_zero > 0) {
        l->hidden_pool =
            (uint8_t*)calloc((size_t)num_snapshots, hidden_size_or_zero);
    }
    if (!l->snapshot_ticks || !l->snapshot_pool ||
        (hidden_size_or_zero > 0 && !l->hidden_pool)) {
        free(l->snapshot_ticks);
        free(l->snapshot_pool);
        free(l->hidden_pool);
        free(l);
        return NULL;
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

/* Compute how many snapshots a demo of `length_ticks` produces under
   stride spacing, including tick 0 and capping at length_ticks - 1. */
static inline int demo_snapshot_ladder_count_for_length(int length_ticks, int stride) {
    if (length_ticks <= 0 || stride <= 0) return 0;
    /* positions: 0, stride, 2*stride, ..., last <= length_ticks - 1 */
    int n = 1 + (length_ticks - 1) / stride;
    return n;
}

/* Pointer to the snapshot blob for slot `i` (0 <= i < num_snapshots). */
static inline const void* demo_snapshot_ladder_snapshot_at(
    const DemoSnapshotLadder* l, int i
) {
    if (!l || i < 0 || i >= l->num_snapshots) return NULL;
    return l->snapshot_pool + (size_t)i * l->snapshot_size;
}

/* Pointer to the hidden state blob for slot `i`, or NULL if no hidden
   pool. */
static inline const void* demo_snapshot_ladder_hidden_at(
    const DemoSnapshotLadder* l, int i
) {
    if (!l || !l->hidden_pool || i < 0 || i >= l->num_snapshots) return NULL;
    return l->hidden_pool + (size_t)i * l->hidden_size;
}

/* Pick the latest snapshot slot whose tick is <= `tick`. Returns -1 if no
   ladder or `tick` is out of range. The caller will typically replay
   actions[snapshot_tick..tick-1] after restoring to land at exactly `tick`. */
static inline int demo_snapshot_ladder_slot_for_tick(
    const DemoSnapshotLadder* l, int tick
) {
    if (!l || tick < 0) return -1;
    int slot = tick / l->snapshot_stride;
    if (slot >= l->num_snapshots) slot = l->num_snapshots - 1;
    return slot;
}

#ifdef __cplusplus
}
#endif

#endif /* DEMOSTORE_H */
