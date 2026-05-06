/**
 * @file inspect_demostore.c
 * @brief Load Phase2Demo files and print basic trajectory statistics.
 */

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/demostore.h"

static int parse_positive_int_arg(const char* name, const char* value) {
    char* end = NULL;
    long parsed = strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed <= 0 || parsed > INT_MAX) {
        fprintf(stderr, "%s must be a positive integer, got %s\n", name, value);
        exit(1);
    }
    return (int)parsed;
}

static uint32_t parse_positive_u32_arg(const char* name, const char* value) {
    char* end = NULL;
    unsigned long parsed = strtoul(value, &end, 10);
    if (end == value || *end != '\0' || parsed == 0 || parsed > UINT32_MAX) {
        fprintf(stderr, "%s must be a positive uint32, got %s\n", name, value);
        exit(1);
    }
    return (uint32_t)parsed;
}

int main(int argc, char** argv) {
    if (argc != 5) {
        fprintf(stderr, "usage: %s <dir> <max_demos> <num_atns> <snapshot_size>\n", argv[0]);
        return 1;
    }
    int max_demos = parse_positive_int_arg("max_demos", argv[2]);
    int num_atns = parse_positive_int_arg("num_atns", argv[3]);
    uint32_t snap_size = parse_positive_u32_arg("snapshot_size", argv[4]);

    DemoStore* s = demostore_create(max_demos);
    if (!s) {
        fprintf(stderr, "demostore_create(%d) failed\n", max_demos);
        return 1;
    }

    int loaded = demostore_load_dir(s, argv[1], num_atns, 1, max_demos, snap_size);
    if (loaded < 0) {
        fprintf(stderr, "demostore_load_dir(%s) failed\n", argv[1]);
        demostore_destroy(s);
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
