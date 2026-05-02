/* Merge archives, dedupe cells by a fingerprint over the cell key, sort by
 * quality, write top-K as PLAY_REPLAY demos.
 *
 * usage: filter_demos [--mode full|coarse] <output_dir> <max_demos>
 *                     <max_replay_ticks> <archive1> [archive2 ...]
 *
 * --mode coarse strips per-tick-noisy bytes (player_x/y, prayer flags, hp
 * bin) from the dedup key so near-duplicate positions inside one chain
 * don't all survive. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/archive.h"

typedef struct {
    Archive* arc;
    int entry_idx;
    int chain_ticks;
} Cand;

static int cmp_cand_by_q(const void* a, const void* b) {
    const Cand* ca = (const Cand*)a;
    const Cand* cb = (const Cand*)b;
    float qa = ca->arc->entries[ca->entry_idx].quality;
    float qb = cb->arc->entries[cb->entry_idx].quality;
    if (qa > qb) return -1;
    if (qa < qb) return 1;
    if (ca->chain_ticks < cb->chain_ticks) return -1;
    if (ca->chain_ticks > cb->chain_ticks) return 1;
    return 0;
}

static uint64_t fnv1a64(const uint8_t* data, size_t n) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < n; i++) {
        h ^= data[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

/* InfCellKey macro-state bytes: wave, weapon_set, brew/restore doses,
   zuk_hp_bin, phase flags, jad/healer/set counts. */
static const int COARSE_BYTES[] = {0, 1, 4, 5, 10, 11, 12, 13, 14};
static const int COARSE_BYTE_COUNT = (int)(sizeof(COARSE_BYTES) / sizeof(COARSE_BYTES[0]));

static uint64_t fingerprint_full(const uint8_t* key) {
    return fnv1a64(key, ARCHIVE_KEY_SIZE);
}

static uint64_t fingerprint_coarse(const uint8_t* key) {
    uint8_t buf[16];
    for (int i = 0; i < COARSE_BYTE_COUNT; i++) buf[i] = key[COARSE_BYTES[i]];
    return fnv1a64(buf, (size_t)COARSE_BYTE_COUNT);
}

static int keys_equal_under_mode(const uint8_t* a, const uint8_t* b, int coarse) {
    if (!coarse) return memcmp(a, b, ARCHIVE_KEY_SIZE) == 0;
    for (int i = 0; i < COARSE_BYTE_COUNT; i++) {
        if (a[COARSE_BYTES[i]] != b[COARSE_BYTES[i]]) return 0;
    }
    return 1;
}

int main(int argc, char** argv) {
    int coarse = 0;
    int positional_start = 1;
    if (argc >= 3 && strcmp(argv[1], "--mode") == 0) {
        if (strcmp(argv[2], "coarse") == 0) coarse = 1;
        else if (strcmp(argv[2], "full") == 0) coarse = 0;
        else {
            fprintf(stderr, "filter_demos: unknown mode '%s'\n", argv[2]);
            return 1;
        }
        positional_start = 3;
    }
    if (argc - positional_start < 4) {
        fprintf(stderr,
            "usage: %s [--mode full|coarse] "
            "<output_dir> <max_demos> <max_replay_ticks> "
            "<archive1> [archive2 ...]\n", argv[0]);
        return 1;
    }
    const char* out_dir = argv[positional_start + 0];
    int max_demos = atoi(argv[positional_start + 1]);
    int max_replay_ticks = atoi(argv[positional_start + 2]);
    int n_archives = argc - (positional_start + 3);
    int archives_start = positional_start + 3;
    fprintf(stderr, "filter_demos: mode=%s\n", coarse ? "coarse" : "full");

    Archive** arcs = (Archive**)calloc((size_t)n_archives, sizeof(Archive*));
    int num_atns = -1;
    int total_cells = 0;
    for (int i = 0; i < n_archives; i++) {
        arcs[i] = archive_load(argv[archives_start + i]);
        if (!arcs[i]) {
            fprintf(stderr, "filter_demos: load %s failed\n",
                argv[archives_start + i]);
            return 1;
        }
        if (num_atns < 0) num_atns = arcs[i]->num_atns;
        else if (num_atns != arcs[i]->num_atns) {
            fprintf(stderr,
                "filter_demos: num_atns mismatch %d vs %d (archive %s)\n",
                num_atns, arcs[i]->num_atns, argv[archives_start + i]);
            return 1;
        }
        total_cells += arcs[i]->num_entries;
    }
    fprintf(stderr,
        "filter_demos: %d archives, %d total cells, num_atns=%d\n",
        n_archives, total_cells, num_atns);

    Cand* cands = (Cand*)malloc((size_t)total_cells * sizeof(Cand));
    int cycles = 0;
    int n_cands = 0;
    for (int a = 0; a < n_archives; a++) {
        Archive* ar = arcs[a];
        for (int i = 0; i < ar->num_entries; i++) {
            int chain = archive_chain_tick_count(ar, i);
            if (chain < 0) { cycles++; continue; }
            if (chain == 0 || chain > max_replay_ticks) continue;
            cands[n_cands].arc = ar;
            cands[n_cands].entry_idx = i;
            cands[n_cands].chain_ticks = chain;
            n_cands++;
        }
    }
    fprintf(stderr,
        "filter_demos: %d valid candidates, %d cycles skipped\n",
        n_cands, cycles);

    qsort(cands, (size_t)n_cands, sizeof(Cand), cmp_cand_by_q);

    int hashcap = 1;
    while (hashcap < 2 * n_cands) hashcap *= 2;
    if (hashcap < 16) hashcap = 16;
    int* slot_to_cand = (int*)malloc((size_t)hashcap * sizeof(int));
    for (int i = 0; i < hashcap; i++) slot_to_cand[i] = -1;
    int* selected = (int*)malloc((size_t)n_cands * sizeof(int));
    int n_selected = 0;
    int dedup_skipped = 0;
    for (int i = 0; i < n_cands && n_selected < max_demos; i++) {
        const ArchiveEntry* e = &cands[i].arc->entries[cands[i].entry_idx];
        uint64_t h = coarse ? fingerprint_coarse(e->key)
                            : fingerprint_full(e->key);
        int slot = (int)(h & (uint64_t)(hashcap - 1));
        int found = 0;
        for (;;) {
            int idx = slot_to_cand[slot];
            if (idx == -1) break;
            const ArchiveEntry* eh =
                &cands[idx].arc->entries[cands[idx].entry_idx];
            if (keys_equal_under_mode(eh->key, e->key, coarse)) {
                found = 1;
                break;
            }
            slot = (slot + 1) & (hashcap - 1);
        }
        if (found) { dedup_skipped++; continue; }
        slot_to_cand[slot] = i;
        selected[n_selected++] = i;
    }
    fprintf(stderr,
        "filter_demos: selected %d demos after dedup-by-key (%d skipped as duplicates)\n",
        n_selected, dedup_skipped);

    int* action_buf =
        (int*)malloc((size_t)max_replay_ticks * (size_t)num_atns * sizeof(int));
    int written = 0;
    for (int rank = 0; rank < n_selected; rank++) {
        Cand* c = &cands[selected[rank]];
        Archive* ar = c->arc;
        int n_ticks =
            archive_replay_actions(ar, c->entry_idx, action_buf, max_replay_ticks);
        if (n_ticks <= 0) continue;
        uint32_t rng_seed = archive_chain_root_rng_seed(ar, c->entry_idx);
        float q = ar->entries[c->entry_idx].quality;

        char path[1024];
        snprintf(path, sizeof(path),
            "%s/demo_%04d_q%.3f_t%d.bin", out_dir, rank, q, n_ticks);
        FILE* f = fopen(path, "wb");
        if (!f) {
            fprintf(stderr, "filter_demos: fopen %s failed\n", path);
            continue;
        }
        size_t expected = (size_t)n_ticks * (size_t)num_atns;
        int ok =
            fwrite(&n_ticks, sizeof(int), 1, f) == 1 &&
            fwrite(&rng_seed, sizeof(uint32_t), 1, f) == 1 &&
            fwrite(action_buf, sizeof(int), expected, f) == expected;
        int close_ok = (fclose(f) == 0);
        if (ok && close_ok) {
            written++;
        } else {
            fprintf(stderr, "filter_demos: short write %s\n", path);
        }
    }
    fprintf(stderr, "filter_demos: %d demos written to %s\n", written, out_dir);

    free(action_buf);
    free(slot_to_cand);
    free(selected);
    free(cands);
    for (int i = 0; i < n_archives; i++) archive_destroy(arcs[i]);
    free(arcs);
    return 0;
}
