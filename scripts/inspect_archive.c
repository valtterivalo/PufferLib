/* Quick archive inspector: peak quality, chain length distribution, cycle count. */

#include <stdio.h>
#include <stdlib.h>
#include "../src/archive.h"

int main(int argc, char** argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s <archive.bin>\n", argv[0]); return 1; }

    Archive* a = archive_load(argv[1]);
    if (!a) { fprintf(stderr, "load failed\n"); return 1; }

    fprintf(stderr, "loaded: %d entries, snapshot=%zu, num_atns=%d, hidden=%zu, action_pool_used=%d\n",
        a->num_entries, a->snapshot_size, a->num_atns, a->hidden_state_size,
        a->action_chunk_pool_used_ints);

    /* quality histogram (10 bins) */
    int qbins[11] = {0};
    int max_qbin_idx = 0;
    float max_q = -1.0f;
    int max_q_idx = -1;
    for (int i = 0; i < a->num_entries; i++) {
        float q = a->entries[i].quality;
        int b = (int)(q * 10.0f);
        if (b < 0) b = 0;
        if (b > 10) b = 10;
        qbins[b]++;
        if (q > max_q) { max_q = q; max_q_idx = i; }
    }
    fprintf(stderr, "\nquality histogram (10 bins):\n");
    for (int b = 0; b <= 10; b++) {
        fprintf(stderr, "  [%.1f, %.1f): %d\n", b / 10.0f, (b + 1) / 10.0f, qbins[b]);
    }
    fprintf(stderr, "  peak quality: %.4f at entry %d\n", max_q, max_q_idx);

    /* chain length distribution + cycle count */
    int cycle_count = 0;
    int valid_chains = 0;
    long total_ticks = 0;
    int max_chain = 0;
    int chain_bins[8] = {0}; /* 0-49, 50-99, 100-149, 150-199, 200-299, 300-499, 500-999, 1000+ */
    for (int i = 0; i < a->num_entries; i++) {
        int chain = archive_chain_tick_count(a, i);
        if (chain < 0) { cycle_count++; continue; }
        if (chain == 0) continue;
        valid_chains++;
        total_ticks += chain;
        if (chain > max_chain) max_chain = chain;
        int b;
        if (chain < 50) b = 0;
        else if (chain < 100) b = 1;
        else if (chain < 150) b = 2;
        else if (chain < 200) b = 3;
        else if (chain < 300) b = 4;
        else if (chain < 500) b = 5;
        else if (chain < 1000) b = 6;
        else b = 7;
        chain_bins[b]++;
    }
    fprintf(stderr, "\nchains: %d valid, %d cycles, %d empty\n",
        valid_chains, cycle_count, a->num_entries - valid_chains - cycle_count);
    fprintf(stderr, "  mean chain ticks: %.1f\n", valid_chains > 0 ? (float)total_ticks / valid_chains : 0.0f);
    fprintf(stderr, "  max chain ticks: %d\n", max_chain);
    const char* bin_names[8] = {"<50", "50-99", "100-149", "150-199", "200-299", "300-499", "500-999", ">=1000"};
    fprintf(stderr, "  chain length bins:\n");
    for (int b = 0; b < 8; b++) fprintf(stderr, "    %s: %d\n", bin_names[b], chain_bins[b]);

    /* top 10 by quality with chain status */
    typedef struct { float q; int idx; int chain; } Top;
    Top* sorted = (Top*)malloc(a->num_entries * sizeof(Top));
    for (int i = 0; i < a->num_entries; i++) {
        sorted[i].q = a->entries[i].quality;
        sorted[i].idx = i;
        sorted[i].chain = archive_chain_tick_count(a, i);
    }
    /* simple partial sort: bubble top 20 */
    for (int k = 0; k < 20 && k < a->num_entries; k++) {
        for (int j = a->num_entries - 1; j > k; j--) {
            if (sorted[j].q > sorted[j-1].q) {
                Top t = sorted[j]; sorted[j] = sorted[j-1]; sorted[j-1] = t;
            }
        }
    }
    fprintf(stderr, "\ntop 20 by quality:\n");
    int how_many = a->num_entries < 20 ? a->num_entries : 20;
    for (int k = 0; k < how_many; k++) {
        fprintf(stderr, "  rank %2d: q=%.4f idx=%d chain=%d %s\n",
            k, sorted[k].q, sorted[k].idx, sorted[k].chain,
            sorted[k].chain < 0 ? "(CYCLE)" : sorted[k].chain == 0 ? "(EMPTY)" : "");
    }
    free(sorted);

    archive_destroy(a);
    return 0;
}
