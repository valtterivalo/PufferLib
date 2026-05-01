/* Quick manual check: load all .bin files in a dir into a DemoStore. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "../src/demostore.h"

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s <dir> <max_demos> <num_atns>\n", argv[0]);
        return 1;
    }
    const char* dir_path = argv[1];
    int max_demos = atoi(argv[2]);
    int num_atns = atoi(argv[3]);

    DIR* d = opendir(dir_path);
    if (!d) { fprintf(stderr, "opendir %s failed\n", dir_path); return 1; }

    DemoStore* s = demostore_create(max_demos);

    char path[1024];
    char* names[1024];
    int n_names = 0;
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        if (n_names >= 1024) break;
        const char* name = ent->d_name;
        size_t len = strlen(name);
        if (len < 4 || strcmp(name + len - 4, ".bin") != 0) continue;
        names[n_names++] = strdup(name);
    }
    closedir(d);

    /* Sort filenames so we get demo_0000_*.bin first. */
    for (int i = 0; i < n_names; i++) {
        for (int j = i + 1; j < n_names; j++) {
            if (strcmp(names[i], names[j]) > 0) {
                char* tmp = names[i]; names[i] = names[j]; names[j] = tmp;
            }
        }
    }

    int loaded = 0;
    int min_ticks = 1 << 30;
    int max_ticks = 0;
    long total_ticks = 0;
    float min_q = 1e9f, max_q = -1e9f;
    for (int i = 0; i < n_names; i++) {
        snprintf(path, sizeof(path), "%s/%s", dir_path, names[i]);
        int id = demostore_load_demo(s, path, num_atns, /*parse_filename_q=*/1);
        if (id >= 0) {
            loaded++;
            const DemoTrajectory* t = &s->demos[id];
            if (t->length_ticks < min_ticks) min_ticks = t->length_ticks;
            if (t->length_ticks > max_ticks) max_ticks = t->length_ticks;
            total_ticks += t->length_ticks;
            if (t->quality_at_root < min_q) min_q = t->quality_at_root;
            if (t->quality_at_root > max_q) max_q = t->quality_at_root;
        }
        free(names[i]);
    }

    fprintf(stderr,
        "loaded %d demos from %s\n"
        "  ticks: min=%d max=%d mean=%.1f total=%ld\n"
        "  quality: min=%.3f max=%.3f\n",
        loaded, dir_path, min_ticks, max_ticks,
        loaded > 0 ? (float)total_ticks / loaded : 0.0f, total_ticks,
        min_q, max_q);

    demostore_destroy(s);
    return 0;
}
