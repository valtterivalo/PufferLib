/* Quick manual check: load all .bin files in a dir into a DemoStore. */

#include <stdio.h>
#include <stdlib.h>
#include "../src/demostore.h"

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s <dir> <max_demos> <num_atns>\n", argv[0]);
        return 1;
    }
    int max_demos = atoi(argv[2]);
    int num_atns = atoi(argv[3]);

    DemoStore* s = demostore_create(max_demos);
    int loaded = demostore_load_dir(s, argv[1], num_atns, 1, max_demos);
    if (loaded < 0) {
        fprintf(stderr, "demostore_load_dir(%s) failed\n", argv[1]);
        return 1;
    }

    int min_ticks = 1 << 30, max_ticks = 0;
    long total_ticks = 0;
    float min_q = 1e9f, max_q = -1e9f;
    for (int i = 0; i < s->num_demos; i++) {
        const DemoTrajectory* t = &s->demos[i];
        if (t->length_ticks < min_ticks) min_ticks = t->length_ticks;
        if (t->length_ticks > max_ticks) max_ticks = t->length_ticks;
        total_ticks += t->length_ticks;
        if (t->quality_at_root < min_q) min_q = t->quality_at_root;
        if (t->quality_at_root > max_q) max_q = t->quality_at_root;
    }

    fprintf(stderr,
        "loaded %d demos from %s\n"
        "  ticks: min=%d max=%d mean=%.1f total=%ld\n"
        "  quality: min=%.3f max=%.3f\n",
        s->num_demos, argv[1], min_ticks, max_ticks,
        s->num_demos > 0 ? (float)total_ticks / s->num_demos : 0.0f, total_ticks,
        min_q, max_q);

    demostore_destroy(s);
    return 0;
}
