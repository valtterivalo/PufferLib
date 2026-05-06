/**
 * @file filter_demos.c
 * @brief Select Inferno Phase2Demo v2 files from one or more archive v2 files.
 *
 * BUILD:
 *   cc -std=c11 -O0 -g -I. -o /tmp/filter_demos scripts/filter_demos.c -lm
 */

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../src/archive.h"
#include "../ocean/osrs/encounters/encounter_inferno.h"

typedef enum {
    FILTER_ALL,
    FILTER_JAD_DAMAGE,
    FILTER_POST_JAD,
    FILTER_RESOLVED,
} FilterMode;

typedef struct {
    Archive* archive;
    int entry_idx;
    int chain_ticks;
    float structural_quality;
    float selection_score;
    float min_zuk_hp;
    int jad_spawned;
    int healer_spawned;
    int live_jad;
    int live_healer;
    int live_set;
} Candidate;

static FilterMode parse_mode(const char* value) {
    if (strcmp(value, "all") == 0) return FILTER_ALL;
    if (strcmp(value, "jad_damage") == 0) return FILTER_JAD_DAMAGE;
    if (strcmp(value, "post_jad") == 0) return FILTER_POST_JAD;
    if (strcmp(value, "resolved") == 0) return FILTER_RESOLVED;
    fprintf(stderr, "filter_demos: unknown mode %s\n", value);
    exit(1);
}

static const char* mode_name(FilterMode mode) {
    switch (mode) {
        case FILTER_ALL:
            return "all";
        case FILTER_JAD_DAMAGE:
            return "jad_damage";
        case FILTER_POST_JAD:
            return "post_jad";
        case FILTER_RESOLVED:
            return "resolved";
    }
    abort();
}

static int parse_positive_int_arg(const char* name, const char* value) {
    char* end = NULL;
    long parsed = strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed <= 0 || parsed > INT_MAX) {
        fprintf(stderr, "filter_demos: %s must be a positive integer, got %s\n",
            name, value);
        exit(1);
    }
    return (int)parsed;
}

static int ensure_dir(const char* path) {
    if (mkdir(path, 0775) == 0) return 0;
    if (errno == EEXIST) {
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return 0;
    }
    fprintf(stderr, "filter_demos: mkdir %s failed: %s\n", path, strerror(errno));
    return -1;
}

static float min_zuk_hp(const InfernoState* s) {
    float v = s->min_zuk_hp_seen > 0.0f ? s->min_zuk_hp_seen : 1200.0f;
    if (v < 0.0f) return 0.0f;
    if (v > 1200.0f) return 1200.0f;
    return v;
}

static int candidate_matches_mode(
    const InfernoState* s,
    InfLateAddCounts counts,
    float zhp,
    FilterMode mode
) {
    int is_post_jad = s->zuk.jad_spawned && counts.live_jad_count == 0;
    int is_jad_damaged = s->zuk.jad_spawned &&
        counts.live_jad_max_hp > 0 &&
        counts.live_jad_hp < counts.live_jad_max_hp;
    int is_healer_resolved = !s->zuk.healer_spawned || counts.live_zuk_healer_count == 0;
    int is_set_resolved = zhp > 900.0f || counts.live_set_count == 0;
    switch (mode) {
        case FILTER_ALL:
            return 1;
        case FILTER_JAD_DAMAGE:
            return is_jad_damaged;
        case FILTER_POST_JAD:
            return is_post_jad;
        case FILTER_RESOLVED:
            return is_post_jad && is_healer_resolved && is_set_resolved;
    }
    abort();
}

static float candidate_selection_score(
    const InfernoState* s,
    InfLateAddCounts counts,
    float zhp,
    FilterMode mode
) {
    float q = inf_progress_score((EncounterState*)s);
    if (mode == FILTER_ALL) return q;

    float transition_bonus = 0.0f;
    if (s->zuk.jad_spawned && counts.live_jad_max_hp > 0) {
        transition_bonus +=
            (float)(counts.live_jad_max_hp - counts.live_jad_hp) /
            (float)counts.live_jad_max_hp;
    }
    if (s->zuk.jad_spawned && counts.live_jad_count == 0) transition_bonus += 1.0f;
    if (s->zuk.healer_spawned && counts.live_zuk_healer_count == 0) transition_bonus += 0.4f;
    if (zhp <= 900.0f && counts.live_set_count == 0) transition_bonus += 0.2f;
    return transition_bonus + q;
}

static int compare_candidates_desc(const void* x, const void* y) {
    const Candidate* a = (const Candidate*)x;
    const Candidate* b = (const Candidate*)y;
    if (a->selection_score > b->selection_score) return -1;
    if (a->selection_score < b->selection_score) return 1;
    if (a->structural_quality > b->structural_quality) return -1;
    if (a->structural_quality < b->structural_quality) return 1;
    if (a->chain_ticks < b->chain_ticks) return -1;
    if (a->chain_ticks > b->chain_ticks) return 1;
    return 0;
}

static int write_phase2_demo(
    const Candidate* candidate,
    const char* output_dir,
    int rank,
    int max_replay_ticks,
    int* action_buf,
    int* chain_cells,
    int* chain_ticks
) {
    Archive* a = candidate->archive;
    int leaf = candidate->entry_idx;
    int n_ticks = archive_replay_actions(a, leaf, action_buf, max_replay_ticks);
    if (n_ticks <= 0) return 0;

    int n_cells = archive_collect_chain(a, leaf, chain_cells, chain_ticks, a->num_entries);
    if (n_cells < 2) return 0;
    if (chain_ticks[n_cells - 1] + a->entries[leaf].action_chunk_len != n_ticks) {
        fprintf(stderr,
            "filter_demos: chain tick mismatch at leaf %d "
            "(cum %d + leaf chunk %d != n_ticks %d)\n",
            leaf, chain_ticks[n_cells - 1],
            a->entries[leaf].action_chunk_len, n_ticks);
        return 0;
    }

    char path[1024];
    snprintf(path, sizeof(path),
        "%s/demo_%04d_s%.3f_q%.3f_t%d.bin",
        output_dir, rank, candidate->selection_score,
        candidate->structural_quality, n_ticks);

    FILE* fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "filter_demos: fopen %s failed\n", path);
        return 0;
    }

    Phase2DemoHeader h = {
        .magic = PHASE2_DEMO_MAGIC,
        .version = PHASE2_DEMO_VERSION,
        .num_ticks = n_ticks,
        .num_atns = a->num_atns,
        .rng_seed = archive_chain_root_rng_seed(a, leaf),
        .quality = candidate->structural_quality,
        .snapshot_size = (uint32_t)a->snapshot_size,
        .num_snapshots = n_cells,
    };

    int ok = fwrite(&h, sizeof(h), 1, fp) == 1;
    for (int i = 0; i < n_cells && ok; i++) {
        const uint8_t* snap = &a->snapshot_pool[(size_t)chain_cells[i] * a->snapshot_size];
        ok = fwrite(snap, 1, a->snapshot_size, fp) == a->snapshot_size;
    }
    if (ok) ok = fwrite(chain_ticks, sizeof(int), n_cells, fp) == (size_t)n_cells;
    size_t expected = (size_t)n_ticks * (size_t)a->num_atns;
    if (ok) ok = fwrite(action_buf, sizeof(int), expected, fp) == expected;
    int close_ok = fclose(fp) == 0;
    if (!ok || !close_ok) {
        fprintf(stderr, "filter_demos: short write %s\n", path);
        return 0;
    }
    return 1;
}

int main(int argc, char** argv) {
    int positional_start = 1;
    FilterMode mode = FILTER_ALL;
    if (argc >= 3 && strcmp(argv[1], "--mode") == 0) {
        mode = parse_mode(argv[2]);
        positional_start = 3;
    }

    if (argc - positional_start < 4) {
        fprintf(stderr,
            "usage: %s [--mode all|jad_damage|post_jad|resolved] "
            "<output_dir> <max_demos> <max_replay_ticks> "
            "<archive1> [archive2 ...]\n", argv[0]);
        return 1;
    }

    const char* output_dir = argv[positional_start];
    int max_demos = parse_positive_int_arg(
        "max_demos", argv[positional_start + 1]);
    int max_replay_ticks = parse_positive_int_arg(
        "max_replay_ticks", argv[positional_start + 2]);
    int archive_count = argc - (positional_start + 3);
    int archive_start = positional_start + 3;

    Archive** archives = (Archive**)calloc((size_t)archive_count, sizeof(Archive*));
    int total_cells = 0;
    int num_atns = -1;
    int snapshot_size = -1;
    for (int i = 0; i < archive_count; i++) {
        archives[i] = archive_load(argv[archive_start + i]);
        if (!archives[i]) {
            fprintf(stderr, "filter_demos: load %s failed\n", argv[archive_start + i]);
            return 1;
        }
        if (num_atns < 0) num_atns = archives[i]->num_atns;
        if (snapshot_size < 0) snapshot_size = archives[i]->snapshot_size;
        if (num_atns != archives[i]->num_atns ||
            snapshot_size != archives[i]->snapshot_size) {
            fprintf(stderr, "filter_demos: incompatible archive schema in %s\n",
                argv[archive_start + i]);
            return 1;
        }
        total_cells += archives[i]->num_entries;
    }
    if (snapshot_size != (int)sizeof(InfSnapshot)) {
        fprintf(stderr, "filter_demos: archive snapshot size %d != runtime %zu\n",
            snapshot_size, sizeof(InfSnapshot));
        return 1;
    }

    Candidate* candidates = (Candidate*)calloc((size_t)total_cells, sizeof(Candidate));
    int candidate_count = 0;
    int post_jad_count = 0;
    int jad_damage_count = 0;
    int healer_clear_count = 0;
    int set_clear_count = 0;
    int cycle_count = 0;
    for (int aidx = 0; aidx < archive_count; aidx++) {
        Archive* a = archives[aidx];
        for (int i = 0; i < a->num_entries; i++) {
            int chain_ticks = archive_chain_tick_count(a, i);
            if (chain_ticks < 0) {
                cycle_count++;
                continue;
            }
            if (chain_ticks <= 0 || chain_ticks > max_replay_ticks) continue;

            const InfSnapshot* snap =
                (const InfSnapshot*)archive_get_snapshot(a, i);
            if (!snap || snap->magic != INF_SNAPSHOT_MAGIC ||
                snap->version != INF_SNAPSHOT_VERSION) {
                fprintf(stderr, "filter_demos: bad snapshot at entry %d\n", i);
                return 1;
            }
            const InfernoState* s = &snap->state;
            if (s->episode_over) continue;

            InfLateAddCounts counts = inf_late_add_counts(s);
            float zhp = min_zuk_hp(s);
            if (s->zuk.jad_spawned && counts.live_jad_max_hp > 0 &&
                counts.live_jad_hp < counts.live_jad_max_hp) {
                jad_damage_count++;
            }
            if (s->zuk.jad_spawned && counts.live_jad_count == 0) post_jad_count++;
            if (s->zuk.healer_spawned && counts.live_zuk_healer_count == 0) healer_clear_count++;
            if (zhp <= 900.0f && counts.live_set_count == 0) set_clear_count++;
            if (!candidate_matches_mode(s, counts, zhp, mode)) continue;

            Candidate* c = &candidates[candidate_count++];
            c->archive = a;
            c->entry_idx = i;
            c->chain_ticks = chain_ticks;
            c->structural_quality = a->entries[i].structural_quality;
            c->selection_score = candidate_selection_score(s, counts, zhp, mode);
            c->min_zuk_hp = zhp;
            c->jad_spawned = s->zuk.jad_spawned;
            c->healer_spawned = s->zuk.healer_spawned;
            c->live_jad = counts.live_jad_count;
            c->live_healer = counts.live_zuk_healer_count;
            c->live_set = counts.live_set_count;
        }
    }

    qsort(candidates, (size_t)candidate_count, sizeof(Candidate),
        compare_candidates_desc);
    fprintf(stderr,
        "filter_demos: mode=%s archives=%d cells=%d candidates=%d cycles=%d "
        "jad_damage_cells=%d post_jad_cells=%d healer_clear_cells=%d set_clear_cells=%d\n",
        mode_name(mode), archive_count, total_cells, candidate_count,
        cycle_count, jad_damage_count, post_jad_count, healer_clear_count, set_clear_count);
    for (int i = 0; i < candidate_count && i < 20; i++) {
        Candidate* c = &candidates[i];
        fprintf(stderr,
            "  cand %03d score=%.3f q=%.3f zhp=%.1f jad_spawn=%d "
            "heal_spawn=%d live_jad=%d live_healer=%d live_set=%d ticks=%d\n",
            i, c->selection_score, c->structural_quality, c->min_zuk_hp,
            c->jad_spawned, c->healer_spawned, c->live_jad,
            c->live_healer, c->live_set, c->chain_ticks);
    }

    if (ensure_dir(output_dir) != 0) return 1;
    int* action_buf = (int*)malloc(
        (size_t)max_replay_ticks * (size_t)num_atns * sizeof(int));
    int* chain_cells = (int*)malloc((size_t)total_cells * sizeof(int));
    int* chain_ticks = (int*)malloc((size_t)total_cells * sizeof(int));
    if (!action_buf || !chain_cells || !chain_ticks) return 1;

    int written = 0;
    for (int i = 0; i < candidate_count && written < max_demos; i++) {
        written += write_phase2_demo(
            &candidates[i], output_dir, written, max_replay_ticks,
            action_buf, chain_cells, chain_ticks);
    }
    fprintf(stderr, "filter_demos: wrote %d demos to %s\n", written, output_dir);

    free(action_buf);
    free(chain_cells);
    free(chain_ticks);
    free(candidates);
    for (int i = 0; i < archive_count; i++) archive_destroy(archives[i]);
    free(archives);
    return written > 0 ? 0 : 1;
}
