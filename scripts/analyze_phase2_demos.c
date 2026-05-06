/**
 * @file analyze_phase2_demos.c
 * @brief Inspect and select Inferno Phase2Demo v2 trajectories.
 *
 * BUILD:
 *   cc -std=c11 -O0 -g -I. -o /tmp/analyze_phase2_demos \
 *       scripts/analyze_phase2_demos.c -lm
 */

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../src/demostore.h"
#include "../ocean/osrs/encounters/encounter_inferno.h"

typedef enum {
    SELECT_ALL,
    SELECT_POST_JAD,
    SELECT_RESOLVED,
} SelectMode;

typedef struct {
    int demo_id;
    char* name;
    float file_quality;
    int length_ticks;
    int best_slot;
    int best_tick;
    float best_quality;
    float best_min_zuk_hp;
    int best_jad_spawned;
    int best_healer_spawned;
    int best_live_jad;
    int best_live_healer;
    int best_live_set;
    int has_post_jad_clear;
    int has_healer_clear;
    int has_set_clear;
    float post_jad_quality;
    float resolved_quality;
    float selection_score;
} DemoReport;

static int compare_string_ptrs(const void* x, const void* y) {
    return strcmp(*(const char* const*)x, *(const char* const*)y);
}

static int compare_reports_desc(const void* x, const void* y) {
    const DemoReport* a = (const DemoReport*)x;
    const DemoReport* b = (const DemoReport*)y;
    if (a->selection_score > b->selection_score) return -1;
    if (a->selection_score < b->selection_score) return 1;
    if (a->best_quality > b->best_quality) return -1;
    if (a->best_quality < b->best_quality) return 1;
    if (a->length_ticks < b->length_ticks) return -1;
    if (a->length_ticks > b->length_ticks) return 1;
    return strcmp(a->name, b->name);
}

static SelectMode parse_mode(const char* value) {
    if (strcmp(value, "all") == 0) return SELECT_ALL;
    if (strcmp(value, "post_jad") == 0) return SELECT_POST_JAD;
    if (strcmp(value, "resolved") == 0) return SELECT_RESOLVED;
    fprintf(stderr, "unknown mode %s\n", value);
    exit(1);
}

static int parse_positive_int_arg(const char* name, const char* value) {
    char* end = NULL;
    long parsed = strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed <= 0 || parsed > INT_MAX) {
        fprintf(stderr, "%s must be a positive integer, got %s\n", name, value);
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
    fprintf(stderr, "mkdir %s failed: %s\n", path, strerror(errno));
    return -1;
}

static int copy_file(const char* src, const char* dst) {
    FILE* in = fopen(src, "rb");
    if (!in) return -1;
    FILE* out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }

    uint8_t buf[1 << 16];
    for (;;) {
        size_t n = fread(buf, 1, sizeof(buf), in);
        if (n > 0 && fwrite(buf, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return -1;
        }
        if (n < sizeof(buf)) {
            if (ferror(in)) {
                fclose(in);
                fclose(out);
                return -1;
            }
            break;
        }
    }

    int close_in = fclose(in);
    int close_out = fclose(out);
    return (close_in == 0 && close_out == 0) ? 0 : -1;
}

static char** sorted_demo_names(const char* dir, int* out_count) {
    DIR* d = opendir(dir);
    if (!d) {
        fprintf(stderr, "opendir %s failed\n", dir);
        exit(1);
    }

    int cap = 256;
    int n = 0;
    char** names = (char**)calloc((size_t)cap, sizeof(char*));
    if (!names) {
        closedir(d);
        exit(1);
    }
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        const char* name = ent->d_name;
        size_t len = strlen(name);
        if (len < 4 || strcmp(name + len - 4, ".bin") != 0) continue;
        if (n == cap) {
            cap *= 2;
            char** next = (char**)realloc(names, (size_t)cap * sizeof(char*));
            if (!next) {
                closedir(d);
                for (int i = 0; i < n; i++) free(names[i]);
                free(names);
                exit(1);
            }
            names = next;
        }
        names[n] = strdup(name);
        if (!names[n]) {
            closedir(d);
            for (int i = 0; i < n; i++) free(names[i]);
            free(names);
            exit(1);
        }
        n++;
    }
    closedir(d);
    qsort(names, (size_t)n, sizeof(char*), compare_string_ptrs);
    *out_count = n;
    return names;
}

static float min_zuk_hp(const InfernoState* s) {
    float v = s->min_zuk_hp_seen > 0.0f ? s->min_zuk_hp_seen : 1200.0f;
    if (v < 0.0f) return 0.0f;
    if (v > 1200.0f) return 1200.0f;
    return v;
}

static DemoReport analyze_demo(const DemoTrajectory* demo, const char* name) {
    DemoReport r;
    memset(&r, 0, sizeof(r));
    r.demo_id = demo->demo_id;
    r.name = strdup(name);
    r.file_quality = demo->quality_at_root;
    r.length_ticks = demo->length_ticks;
    r.best_slot = -1;
    r.best_quality = -1.0f;
    r.best_min_zuk_hp = 1200.0f;
    r.post_jad_quality = -1.0f;
    r.resolved_quality = -1.0f;

    for (int i = 0; i < demo->num_snapshots; i++) {
        const InfSnapshot* snap = (const InfSnapshot*)(
            demo->snapshots + (size_t)i * (size_t)demo->snapshot_size);
        if (snap->magic != INF_SNAPSHOT_MAGIC || snap->version != INF_SNAPSHOT_VERSION) {
            fprintf(stderr, "bad snapshot in %s slot %d\n", name, i);
            exit(1);
        }

        const InfernoState* s = &snap->state;
        float q = inf_progress_score((EncounterState*)s);
        InfLateAddCounts counts = inf_late_add_counts(s);
        float zhp = min_zuk_hp(s);
        int is_terminal = s->episode_over;
        int is_post_jad = s->zuk.jad_spawned && counts.live_jad_count == 0;
        int is_healer_clear = s->zuk.healer_spawned && counts.live_zuk_healer_count == 0;
        int is_set_clear = zhp <= 900.0f && counts.live_set_count == 0;
        int is_resolved = is_post_jad &&
            (!s->zuk.healer_spawned || is_healer_clear) &&
            (zhp > 900.0f || is_set_clear);

        if (is_post_jad) {
            r.has_post_jad_clear = 1;
            if (q > r.post_jad_quality) r.post_jad_quality = q;
        }
        if (is_healer_clear) r.has_healer_clear = 1;
        if (is_set_clear) r.has_set_clear = 1;
        if (is_resolved && q > r.resolved_quality) r.resolved_quality = q;

        if (!is_terminal && q > r.best_quality) {
            r.best_slot = i;
            r.best_tick = demo->snapshot_ticks[i];
            r.best_quality = q;
            r.best_min_zuk_hp = zhp;
            r.best_jad_spawned = s->zuk.jad_spawned;
            r.best_healer_spawned = s->zuk.healer_spawned;
            r.best_live_jad = counts.live_jad_count;
            r.best_live_healer = counts.live_zuk_healer_count;
            r.best_live_set = counts.live_set_count;
        }
    }

    return r;
}

static int report_matches_mode(const DemoReport* r, SelectMode mode) {
    switch (mode) {
        case SELECT_ALL:
            return 1;
        case SELECT_POST_JAD:
            return r->has_post_jad_clear;
        case SELECT_RESOLVED:
            return r->resolved_quality >= 0.0f;
    }
    abort();
}

static float report_score_for_mode(const DemoReport* r, SelectMode mode) {
    switch (mode) {
        case SELECT_ALL:
            return r->best_quality;
        case SELECT_POST_JAD:
            return r->post_jad_quality;
        case SELECT_RESOLVED:
            return r->resolved_quality;
    }
    abort();
}

int main(int argc, char** argv) {
    if (argc < 3 || argc > 5) {
        fprintf(stderr,
            "usage: %s <demo_dir> <max_demos> [mode=all|post_jad|resolved] [output_dir]\n",
            argv[0]);
        return 1;
    }

    const char* demo_dir = argv[1];
    int max_demos = parse_positive_int_arg("max_demos", argv[2]);
    SelectMode mode = argc >= 4 ? parse_mode(argv[3]) : SELECT_ALL;
    const char* output_dir = argc >= 5 ? argv[4] : NULL;

    int n_names = 0;
    char** names = sorted_demo_names(demo_dir, &n_names);

    DemoStore* store = demostore_create(n_names);
    if (!store) return 1;

    char path[2048];
    int loaded = 0;
    for (int i = 0; i < n_names; i++) {
        snprintf(path, sizeof(path), "%s/%s", demo_dir, names[i]);
        int id = demostore_load_demo(store, path, INF_NUM_ACTION_HEADS, 1, sizeof(InfSnapshot));
        if (id < 0) {
            fprintf(stderr, "failed to load %s\n", path);
            return 1;
        }
        loaded++;
    }

    DemoReport* reports = (DemoReport*)calloc((size_t)loaded, sizeof(DemoReport));
    int selected_count = 0;
    int post_jad_count = 0;
    int healer_count = 0;
    int set_count = 0;
    for (int i = 0; i < loaded; i++) {
        reports[i] = analyze_demo(&store->demos[i], names[i]);
        if (reports[i].has_post_jad_clear) post_jad_count++;
        if (reports[i].has_healer_clear) healer_count++;
        if (reports[i].has_set_clear) set_count++;
        if (report_matches_mode(&reports[i], mode)) {
            reports[i].selection_score = report_score_for_mode(&reports[i], mode);
            selected_count++;
        } else {
            reports[i].selection_score = -INFINITY;
        }
    }

    qsort(reports, (size_t)loaded, sizeof(DemoReport), compare_reports_desc);

    printf("loaded=%d post_jad=%d healer_clear=%d set_clear=%d selected=%d\n",
        loaded, post_jad_count, healer_count, set_count, selected_count);
    printf("rank,name,file_q,best_q,best_tick,best_min_zuk,best_jad_spawned,best_healer_spawned,best_live_jad,best_live_healer,best_live_set,post_jad_q,resolved_q\n");
    for (int i = 0; i < loaded; i++) {
        if (reports[i].selection_score == -INFINITY) continue;
        printf("%d,%s,%.3f,%.3f,%d,%.1f,%d,%d,%d,%d,%d,%.3f,%.3f\n",
            i, reports[i].name, reports[i].file_quality,
            reports[i].best_quality, reports[i].best_tick,
            reports[i].best_min_zuk_hp,
            reports[i].best_jad_spawned, reports[i].best_healer_spawned,
            reports[i].best_live_jad, reports[i].best_live_healer,
            reports[i].best_live_set, reports[i].post_jad_quality,
            reports[i].resolved_quality);
    }

    if (output_dir) {
        if (ensure_dir(output_dir) != 0) return 1;
        int written = 0;
        for (int i = 0; i < loaded && written < max_demos; i++) {
            if (reports[i].selection_score == -INFINITY) continue;
            snprintf(path, sizeof(path), "%s/%s", demo_dir, reports[i].name);
            char dst[2048];
            snprintf(dst, sizeof(dst), "%s/demo_%04d_q%.3f_t%d_%s",
                output_dir, written, reports[i].selection_score,
                reports[i].length_ticks, reports[i].name);
            if (copy_file(path, dst) != 0) {
                fprintf(stderr, "copy %s -> %s failed\n", path, dst);
                return 1;
            }
            written++;
        }
        fprintf(stderr, "wrote %d selected demos to %s\n", written, output_dir);
    }

    for (int i = 0; i < loaded; i++) free(reports[i].name);
    free(reports);
    demostore_destroy(store);
    for (int i = 0; i < n_names; i++) free(names[i]);
    free(names);
    return 0;
}
