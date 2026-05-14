/* DemoStore: holds top-K archive demos for phase 2.
 *
 * File format v3 (magic 'P2DM' version 3):
 *   Phase2DemoHeader header
 *   uint8  snapshots[num_snapshots * snapshot_size]   (chain root -> leaf)
 *   int32  snapshot_ticks[num_snapshots]              (cumulative ticks per slot)
 *   int32  actions[num_ticks * num_atns]              (root-to-leaf action chain)
 *   uint8  hidden_states[num_snapshots * hidden_state_size]
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
#define PHASE2_DEMO_VERSION_LEGACY 2u
#define PHASE2_DEMO_VERSION 3u
#define PHASE2_FIRST_FORWARD_MAGIC 0x44574646u  /* 'FFWD' little-endian */
#define PHASE2_FIRST_FORWARD_VERSION_LEGACY 1u
#define PHASE2_FIRST_FORWARD_VERSION 2u

typedef struct {
    uint32_t magic;
    uint32_t version;
    int32_t num_ticks;
    int32_t num_atns;
    uint32_t rng_seed;
    float quality;
    uint32_t snapshot_size;
    int32_t num_snapshots;
} Phase2DemoHeaderV2;

typedef struct {
    uint32_t magic;
    uint32_t version;
    int32_t num_ticks;
    int32_t num_atns;
    uint32_t rng_seed;
    float quality;
    uint32_t snapshot_size;
    int32_t num_snapshots;
    uint32_t hidden_state_size;
    uint32_t reserved;
} Phase2DemoHeader;

typedef struct {
    uint32_t magic;
    uint32_t version;
    int32_t num_snapshots;
    uint32_t logits_value_size;
    uint32_t obs_size;
    uint32_t reserved[3];
} Phase2FirstForwardHeader;

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
    uint32_t hidden_state_size;
    uint8_t* hidden_states;
    uint8_t* first_forward_valid;
    uint8_t* first_forward_values;
    uint8_t* first_forward_obs_values;
    uint32_t first_forward_size;
    uint32_t first_forward_obs_size;
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
        free(s->demos[i].hidden_states);
        free(s->demos[i].first_forward_valid);
        free(s->demos[i].first_forward_values);
        free(s->demos[i].first_forward_obs_values);
    }
    free(s->demos);
    free(s);
}

static inline int demostore_load_first_forward_sidecar(
    DemoTrajectory* d,
    const char* demo_path
) {
    char sidecar_path[1200];
    int n = snprintf(sidecar_path, sizeof(sidecar_path), "%s.ffwd", demo_path);
    if (n < 0 || (size_t)n >= sizeof(sidecar_path)) return -1;

    FILE* f = fopen(sidecar_path, "rb");
    if (!f) return 0;

    Phase2FirstForwardHeader h;
    if (fread(&h, sizeof(h), 1, f) != 1) {
        fclose(f);
        return -1;
    }
    if (h.magic != PHASE2_FIRST_FORWARD_MAGIC ||
            (h.version != PHASE2_FIRST_FORWARD_VERSION &&
             h.version != PHASE2_FIRST_FORWARD_VERSION_LEGACY) ||
            h.num_snapshots != d->num_snapshots ||
            h.logits_value_size == 0) {
        fprintf(stderr, "demostore_load_demo: invalid first-forward sidecar %s\n",
            sidecar_path);
        fclose(f);
        return -1;
    }

    size_t valid_bytes = (size_t)h.num_snapshots;
    size_t value_bytes = (size_t)h.num_snapshots * (size_t)h.logits_value_size;
    size_t obs_bytes = (size_t)h.num_snapshots * (size_t)h.obs_size;
    uint8_t* valid = (uint8_t*)malloc(valid_bytes);
    uint8_t* values = (uint8_t*)malloc(value_bytes);
    uint8_t* obs_values = h.obs_size > 0 ? (uint8_t*)malloc(obs_bytes) : NULL;
    if (!valid || !values ||
            fread(valid, 1, valid_bytes, f) != valid_bytes ||
            fread(values, 1, value_bytes, f) != value_bytes ||
            (h.obs_size > 0 && fread(obs_values, 1, obs_bytes, f) != obs_bytes)) {
        free(valid);
        free(values);
        free(obs_values);
        fclose(f);
        return -1;
    }

    long here = ftell(f);
    fseek(f, 0, SEEK_END);
    long end = ftell(f);
    fclose(f);
    if (here != end) {
        fprintf(stderr,
            "demostore_load_demo: trailing bytes in first-forward sidecar %s\n",
            sidecar_path);
        free(valid);
        free(values);
        free(obs_values);
        return -1;
    }

    d->first_forward_valid = valid;
    d->first_forward_values = values;
    d->first_forward_obs_values = obs_values;
    d->first_forward_size = h.logits_value_size;
    d->first_forward_obs_size = h.obs_size;
    return 0;
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

    Phase2DemoHeaderV2 h2;
    if (fread(&h2, sizeof(h2), 1, f) != 1) { fclose(f); return -1; }
    if (h2.magic != PHASE2_DEMO_MAGIC ||
        (h2.version != PHASE2_DEMO_VERSION_LEGACY &&
         h2.version != PHASE2_DEMO_VERSION)) {
        fprintf(stderr,
            "demostore_load_demo: bad magic/version in %s "
            "(got 0x%08x v%u, want 0x%08x v%u or v%u)\n",
            path, h2.magic, h2.version, PHASE2_DEMO_MAGIC,
            PHASE2_DEMO_VERSION_LEGACY, PHASE2_DEMO_VERSION);
        fclose(f); return -1;
    }
    Phase2DemoHeader h = {
        .magic = h2.magic,
        .version = h2.version,
        .num_ticks = h2.num_ticks,
        .num_atns = h2.num_atns,
        .rng_seed = h2.rng_seed,
        .quality = h2.quality,
        .snapshot_size = h2.snapshot_size,
        .num_snapshots = h2.num_snapshots,
        .hidden_state_size = 0,
        .reserved = 0,
    };
    if (h.version == PHASE2_DEMO_VERSION) {
        if (fread(&h.hidden_state_size, sizeof(uint32_t), 1, f) != 1 ||
            fread(&h.reserved, sizeof(uint32_t), 1, f) != 1) {
            fclose(f); return -1;
        }
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
    if (h.hidden_state_size > 0 &&
        (size_t)h.num_snapshots > SIZE_MAX / (size_t)h.hidden_state_size) {
        fclose(f); return -1;
    }

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

    uint8_t* hidden_states = NULL;
    if (h.hidden_state_size > 0) {
        size_t hidden_bytes = (size_t)h.num_snapshots * (size_t)h.hidden_state_size;
        hidden_states = (uint8_t*)malloc(hidden_bytes);
        if (!hidden_states || fread(hidden_states, 1, hidden_bytes, f) != hidden_bytes) {
            free(snapshots);
            free(snapshot_ticks);
            free(actions);
            free(hidden_states);
            fclose(f);
            return -1;
        }
    }

    long here = ftell(f);
    fseek(f, 0, SEEK_END);
    long end = ftell(f);
    fclose(f);
    if (here != end) {
        fprintf(stderr,
            "demostore_load_demo: trailing bytes in %s\n", path);
        free(snapshots);
        free(snapshot_ticks);
        free(actions);
        free(hidden_states);
        return -1;
    }

    float q_root = h.quality;
    if (parse_filename_q) {
        const char* slash = strrchr(path, '/');
        const char* q_marker = strstr(slash ? slash + 1 : path, "_q");
        if (q_marker) {
            char* q_end = NULL;
            float parsed = strtof(q_marker + 2, &q_end);
            if (q_end == q_marker + 2) {
                fprintf(stderr,
                    "demostore_load_demo: bad filename quality in %s\n",
                    path);
                free(snapshots);
                free(snapshot_ticks);
                free(actions);
                free(hidden_states);
                return -1;
            }
            q_root = parsed;
        }
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
    d->hidden_state_size = h.hidden_state_size;
    d->hidden_states = hidden_states;
    d->first_forward_valid = NULL;
    d->first_forward_values = NULL;
    d->first_forward_obs_values = NULL;
    d->first_forward_size = 0;
    d->first_forward_obs_size = 0;
    d->cursor_tick = snapshot_ticks[h.num_snapshots - 1];
    if (demostore_load_first_forward_sidecar(d, path) < 0) {
        free(d->actions);
        free(d->snapshots);
        free(d->snapshot_ticks);
        free(d->hidden_states);
        free(d->first_forward_valid);
        free(d->first_forward_values);
        free(d->first_forward_obs_values);
        memset(d, 0, sizeof(*d));
        s->num_demos--;
        return -1;
    }
    return demo_id;
}

static inline const int* demostore_actions_at(
    const DemoStore* s, int demo_id, int tick
) {
    const DemoTrajectory* d = &s->demos[demo_id];
    return &d->actions[(size_t)tick * (size_t)d->num_atns];
}

static inline const void* demostore_hidden_at(
    const DemoStore* s, int demo_id, int slot
) {
    const DemoTrajectory* d = &s->demos[demo_id];
    if (!d->hidden_states || d->hidden_state_size == 0 ||
        slot < 0 || slot >= d->num_snapshots) return NULL;
    return d->hidden_states + (size_t)slot * (size_t)d->hidden_state_size;
}

static inline const void* demostore_first_forward_at(
    const DemoStore* s, int demo_id, int slot, uint32_t* out_size
) {
    const DemoTrajectory* d = &s->demos[demo_id];
    if (!d->first_forward_valid || !d->first_forward_values ||
            d->first_forward_size == 0 ||
            slot < 0 || slot >= d->num_snapshots ||
            !d->first_forward_valid[slot]) return NULL;
    if (out_size) *out_size = d->first_forward_size;
    return d->first_forward_values + (size_t)slot * (size_t)d->first_forward_size;
}

static inline const void* demostore_first_forward_obs_at(
    const DemoStore* s, int demo_id, int slot, uint32_t* out_size
) {
    const DemoTrajectory* d = &s->demos[demo_id];
    if (!d->first_forward_valid || !d->first_forward_obs_values ||
            d->first_forward_obs_size == 0 ||
            slot < 0 || slot >= d->num_snapshots ||
            !d->first_forward_valid[slot]) return NULL;
    if (out_size) *out_size = d->first_forward_obs_size;
    return d->first_forward_obs_values +
        (size_t)slot * (size_t)d->first_forward_obs_size;
}

/* Sort + load .bin files in `dir`; max_demos limits the sorted load set.
   Returns number loaded, or -1 on directory, allocation, schema, or read failure. */
static inline int qsort_strcmp_(const void* a, const void* b) {
    return strcmp(*(const char**)a, *(const char**)b);
}

static inline void demostore_free_names(char** names, int n) {
    for (int i = 0; i < n; i++) free(names[i]);
    free(names);
}

static inline void demostore_truncate(DemoStore* s, int num_demos) {
    for (int i = num_demos; i < s->num_demos; i++) {
        free(s->demos[i].actions);
        free(s->demos[i].snapshots);
        free(s->demos[i].snapshot_ticks);
        free(s->demos[i].hidden_states);
        free(s->demos[i].first_forward_valid);
        free(s->demos[i].first_forward_values);
        free(s->demos[i].first_forward_obs_values);
        memset(&s->demos[i], 0, sizeof(DemoTrajectory));
    }
    s->num_demos = num_demos;
}

static inline int demostore_load_dir(
    DemoStore* s, const char* dir, int num_atns, int parse_q, int max_demos,
    uint32_t expected_snapshot_size
) {
    DIR* d = opendir(dir);
    if (!d) return -1;
    int cap = 64;
    int n = 0;
    char** names = (char**)calloc((size_t)cap, sizeof(char*));
    if (!names) {
        closedir(d);
        return -1;
    }
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        const char* name = ent->d_name;
        size_t len = strlen(name);
        if (len < 4 || strcmp(name + len - 4, ".bin") != 0) continue;
        if (n == cap) {
            int next_cap = cap * 2;
            char** next = (char**)realloc(names, (size_t)next_cap * sizeof(char*));
            if (!next) {
                closedir(d);
                demostore_free_names(names, n);
                return -1;
            }
            names = next;
            cap = next_cap;
        }
        names[n++] = strdup(name);
        if (!names[n - 1]) {
            closedir(d);
            demostore_free_names(names, n - 1);
            return -1;
        }
    }
    closedir(d);
    qsort(names, (size_t)n, sizeof(char*), qsort_strcmp_);

    int load_count = n;
    if (max_demos > 0 && load_count > max_demos) load_count = max_demos;
    int start_count = s->num_demos;
    if (s->num_demos + load_count > s->capacity) {
        demostore_free_names(names, n);
        return -1;
    }

    char path[1024];
    int loaded = 0;
    for (int i = 0; i < load_count; i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        if (demostore_load_demo(s, path, num_atns, parse_q,
                                expected_snapshot_size) < 0) {
            demostore_truncate(s, start_count);
            demostore_free_names(names, n);
            return -1;
        }
        loaded++;
    }
    demostore_free_names(names, n);
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
