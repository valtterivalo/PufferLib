/* DemoStore: holds top-K archive demos (PLAY_REPLAY format) for phase 2.
 *
 * File format per demo: int32 num_ticks; uint32 rng_seed;
 *                       int32 action[num_ticks * num_atns]. */

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

typedef struct {
    int demo_id;
    int length_ticks;
    int num_atns;
    uint32_t rng_seed;
    float quality_at_root;
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
    for (int i = 0; i < s->num_demos; i++) free(s->demos[i].actions);
    free(s->demos);
    free(s);
}

/* Load one PLAY_REPLAY demo from `path`. `num_atns` matches the encounter
   (the file does not carry it). If `parse_filename_q` is non-zero, parses
   quality from the filename convention "demo_*_q<f>_t<n>.bin". Returns the
   new demo_id, or -1 on file read failure / capacity full / trailing bytes
   (which usually means a num_atns mismatch). */
static inline int demostore_load_demo(
    DemoStore* s,
    const char* path,
    int num_atns,
    int parse_filename_q
) {
    if (s->num_demos >= s->capacity || num_atns <= 0 ||
        num_atns > DEMOSTORE_MAX_NUM_ATNS) return -1;

    FILE* f = fopen(path, "rb");
    if (!f) return -1;

    int n_ticks = 0;
    uint32_t rng_seed = 0;
    if (fread(&n_ticks, sizeof(int), 1, f) != 1) { fclose(f); return -1; }
    if (fread(&rng_seed, sizeof(uint32_t), 1, f) != 1) { fclose(f); return -1; }
    if (n_ticks <= 0 || n_ticks > 1024 * 1024) { fclose(f); return -1; }

    size_t expected = (size_t)n_ticks * (size_t)num_atns;
    int* actions = (int*)malloc(expected * sizeof(int));
    if (fread(actions, sizeof(int), expected, f) != expected) {
        free(actions); fclose(f); return -1;
    }

    long here = ftell(f);
    fseek(f, 0, SEEK_END);
    long end = ftell(f);
    fclose(f);
    if (here != end) {
        fprintf(stderr,
            "demostore_load_demo: trailing bytes in %s (num_atns=%d?)\n",
            path, num_atns);
        free(actions);
        return -1;
    }

    float q_root = 0.0f;
    if (parse_filename_q) {
        const char* slash = strrchr(path, '/');
        const char* q_marker = strstr(slash ? slash + 1 : path, "_q");
        if (q_marker) q_root = (float)atof(q_marker + 2);
    }

    int demo_id = s->num_demos++;
    DemoTrajectory* d = &s->demos[demo_id];
    d->demo_id = demo_id;
    d->length_ticks = n_ticks;
    d->num_atns = num_atns;
    d->rng_seed = rng_seed;
    d->quality_at_root = q_root;
    d->actions = actions;
    d->cursor_tick = n_ticks - 1;
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
    DemoStore* s, const char* dir, int num_atns, int parse_q, int max_demos
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
        if (demostore_load_demo(s, path, num_atns, parse_q) >= 0) loaded++;
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

static inline int demo_snapshot_ladder_slot_for_tick(const DemoSnapshotLadder* l, int tick) {
    if (!l || tick < 0) return -1;
    int slot = tick / l->snapshot_stride;
    return slot >= l->num_snapshots ? l->num_snapshots - 1 : slot;
}

/* env->observations captured at every demo tick; layout matches env-side
   [obs, mask] split. */
typedef struct {
    int demo_id;
    int length_ticks;
    int obs_floats_per_tick;
    float* obs;
} DemoObsCache;

static inline DemoObsCache* demo_obs_cache_create(
    int demo_id, int length_ticks, int obs_floats_per_tick
) {
    DemoObsCache* c = (DemoObsCache*)calloc(1, sizeof(*c));
    c->demo_id = demo_id;
    c->length_ticks = length_ticks;
    c->obs_floats_per_tick = obs_floats_per_tick;
    c->obs = (float*)calloc((size_t)length_ticks * (size_t)obs_floats_per_tick, sizeof(float));
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
