/* Quick archive inspector: archive v2 quality and chain length distribution. */

#include <stdio.h>
#include <stdlib.h>
#include "../src/archive.h"

typedef struct {
    float structural_q;
    float sampling_q;
    int idx;
    int chain;
} Top;

static int cmp_top_desc(const void* a, const void* b) {
    float qa = ((const Top*)a)->structural_q;
    float qb = ((const Top*)b)->structural_q;
    return qa < qb ? 1 : qa > qb ? -1 : 0;
}

int main(int argc, char** argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s <archive.bin>\n", argv[0]); return 1; }

    Archive* a = archive_load(argv[1]);
    if (!a) { fprintf(stderr, "load failed\n"); return 1; }

    fprintf(stderr, "loaded: %d entries, snapshot=%zu, num_atns=%d, hidden=%zu, action_pool_used=%d\n",
        a->num_entries, a->snapshot_size, a->num_atns, a->hidden_state_size,
        a->action_chunk_pool_used_ints);

    int structural_bins[11] = {0};
    int sampling_bins[11] = {0};
    float max_structural_q = -1.0f;
    float max_sampling_q = -1.0f;
    int max_structural_idx = -1;
    int max_sampling_idx = -1;
    for (int i = 0; i < a->num_entries; i++) {
        float structural_q = a->entries[i].structural_quality;
        float sampling_q = a->entries[i].sampling_quality;
        int structural_bin = (int)(structural_q * 10.0f);
        int sampling_bin = (int)(sampling_q * 10.0f);
        if (structural_bin < 0) structural_bin = 0;
        if (structural_bin > 10) structural_bin = 10;
        if (sampling_bin < 0) sampling_bin = 0;
        if (sampling_bin > 10) sampling_bin = 10;
        structural_bins[structural_bin]++;
        sampling_bins[sampling_bin]++;
        if (structural_q > max_structural_q) {
            max_structural_q = structural_q;
            max_structural_idx = i;
        }
        if (sampling_q > max_sampling_q) {
            max_sampling_q = sampling_q;
            max_sampling_idx = i;
        }
    }
    fprintf(stderr, "\nstructural quality histogram (10 bins):\n");
    for (int b = 0; b <= 10; b++) {
        fprintf(stderr, "  [%.1f, %.1f): %d\n",
            b / 10.0f, (b + 1) / 10.0f, structural_bins[b]);
    }
    fprintf(stderr, "  peak structural quality: %.4f at entry %d\n",
        max_structural_q, max_structural_idx);
    fprintf(stderr, "\nsampling quality histogram (10 bins):\n");
    for (int b = 0; b <= 10; b++) {
        fprintf(stderr, "  [%.1f, %.1f): %d\n",
            b / 10.0f, (b + 1) / 10.0f, sampling_bins[b]);
    }
    fprintf(stderr, "  peak sampling quality: %.4f at entry %d\n",
        max_sampling_q, max_sampling_idx);

    int cycle_count = 0, valid_chains = 0, max_chain = 0;
    long total_ticks = 0;
    int chain_bins[8] = {0};
    for (int i = 0; i < a->num_entries; i++) {
        int chain = archive_chain_tick_count(a, i);
        if (chain < 0) { cycle_count++; continue; }
        if (chain == 0) continue;
        valid_chains++;
        total_ticks += chain;
        if (chain > max_chain) max_chain = chain;
        int b = chain < 50 ? 0 : chain < 100 ? 1 : chain < 150 ? 2 : chain < 200 ? 3
              : chain < 300 ? 4 : chain < 500 ? 5 : chain < 1000 ? 6 : 7;
        chain_bins[b]++;
    }
    fprintf(stderr, "\nchains: %d valid, %d cycles, %d empty\n",
        valid_chains, cycle_count, a->num_entries - valid_chains - cycle_count);
    fprintf(stderr, "  mean chain ticks: %.1f\n",
        valid_chains > 0 ? (float)total_ticks / valid_chains : 0.0f);
    fprintf(stderr, "  max chain ticks: %d\n", max_chain);
    const char* bin_names[8] = {"<50", "50-99", "100-149", "150-199", "200-299", "300-499", "500-999", ">=1000"};
    fprintf(stderr, "  chain length bins:\n");
    for (int b = 0; b < 8; b++) fprintf(stderr, "    %s: %d\n", bin_names[b], chain_bins[b]);

    Top* sorted = (Top*)malloc(a->num_entries * sizeof(Top));
    for (int i = 0; i < a->num_entries; i++) {
        sorted[i].structural_q = a->entries[i].structural_quality;
        sorted[i].sampling_q = a->entries[i].sampling_quality;
        sorted[i].idx = i;
        sorted[i].chain = archive_chain_tick_count(a, i);
    }
    qsort(sorted, a->num_entries, sizeof(Top), cmp_top_desc);
    int how_many = a->num_entries < 20 ? a->num_entries : 20;
    fprintf(stderr, "\ntop %d by structural quality:\n", how_many);
    for (int k = 0; k < how_many; k++) {
        fprintf(stderr, "  rank %2d: structural_q=%.4f sampling_q=%.4f idx=%d chain=%d %s\n",
            k, sorted[k].structural_q, sorted[k].sampling_q,
            sorted[k].idx, sorted[k].chain,
            sorted[k].chain < 0 ? "(CYCLE)" : sorted[k].chain == 0 ? "(EMPTY)" : "");
    }
    free(sorted);
    archive_destroy(a);
    return 0;
}
