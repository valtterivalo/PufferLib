#ifndef GS_TARGET_ORACLE_H
#define GS_TARGET_ORACLE_H
#include <errno.h>

static void gs_target_clear(GsTarget* target) {
    for (int i = 0; i < target->history_count; i++) free(target->history[i].hp);
    for (int i = 0; i < target->belief_count; i++) free(target->belief[i].hp);
    free(target->history);
    free(target->belief);
    memset(target, 0, sizeof(*target));
}

static int gs_target_int(const char** p) {
    char* end;
    errno = 0;
    long value = strtol(*p, &end, 10);
    if (end == *p || errno || value < INT_MIN || value > INT_MAX) {
        fputs("Invalid target oracle integer\n", stderr);
        exit(EXIT_FAILURE);
    }
    *p = end;
    return (int)value;
}

static int gs_target_read_line(GsTarget* target, const char* tag, const char* p) {
    if (!strcmp(tag, "T")) {
        if (target->base_hp != 0) { fputs("Duplicate target oracle header\n", stderr); exit(EXIT_FAILURE); }
        target->has_history = gs_target_int(&p);
        target->has_belief = gs_target_int(&p);
        target->base_hp = gs_target_int(&p);
        if ((target->has_history != 0 && target->has_history != 1) ||
                (target->has_belief != 0 && target->has_belief != 1) ||
                target->base_hp <= 0 || target->base_hp > GS_OVERHEALED_HP) {
            fputs("Invalid target oracle header\n", stderr);
            exit(EXIT_FAILURE);
        }
    } else if (!strcmp(tag, "TH") || !strcmp(tag, "TB")) {
        int history = !strcmp(tag, "TH");
        if (!(history ? target->has_history : target->has_belief)) {
            fputs("Target oracle branch without presence flag\n", stderr);
            exit(EXIT_FAILURE);
        }
        GsTargetBranch branch;
        branch.ready.hard = gs_target_int(&p);
        branch.ready.potion = gs_target_int(&p);
        branch.ready.fast = gs_target_int(&p);
        branch.levels = gs_target_int(&p);
        if (branch.levels < 0 || branch.levels > GS_OVERHEALED_HP + 1) {
            fputs("Invalid target oracle HP length\n", stderr);
            exit(EXIT_FAILURE);
        }
        branch.hp = branch.levels ? malloc((size_t)branch.levels * sizeof(double)) : NULL;
        if (branch.levels && !branch.hp) { perror("Allocating target HP mass"); exit(EXIT_FAILURE); }
        for (int hp = 0; hp < branch.levels; hp++) {
            char* end;
            errno = 0;
            double mass = strtod(p, &end);
            if (end == p || (errno && !(errno == ERANGE && mass > 0)) || !isfinite(mass) || mass < 0 || (history && mass != 0 && mass != 1)) {
                fputs("Invalid target oracle HP mass\n", stderr);
                exit(EXIT_FAILURE);
            }
            branch.hp[hp] = mass;
            p = end;
        }
        GsTargetBranch** branches = history ? &target->history : &target->belief;
        int* count = history ? &target->history_count : &target->belief_count;
        GsTargetBranch* grown = realloc(*branches, (size_t)(*count + 1) * sizeof(**branches));
        if (!grown) { perror("Allocating target oracle branches"); exit(EXIT_FAILURE); }
        *branches = grown;
        grown[(*count)++] = branch;
    } else return 0;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p) { fputs("Extra target oracle fields\n", stderr); exit(EXIT_FAILURE); }
    return 1;
}
static void gs_target_validate(const GsTarget* target) {
    if (!target->base_hp) { fputs("Missing target oracle header\n", stderr); exit(EXIT_FAILURE); }
    int supported = 0;
    double mass = 0;
    for (int kind = 0; kind < 2; kind++) {
        const GsTargetBranch* branches = kind ? target->belief : target->history;
        int count = kind ? target->belief_count : target->history_count;
        for (int b = 0; b < count; b++) {
            GsTargetBranch branch = branches[b];
            for (int previous = 0; previous < b; previous++) {
                GsTargetReady ready = branches[previous].ready;
                if (ready.hard == branch.ready.hard && ready.potion == branch.ready.potion && ready.fast == branch.ready.fast) {
                    fputs("Duplicate target oracle readiness branch\n", stderr);
                    exit(EXIT_FAILURE);
                }
            }
            if (kind && branch.levels && branch.hp[0] != 0) {
                fputs("Target belief must condition on live HP\n", stderr);
                exit(EXIT_FAILURE);
            }
            for (int hp = 1; hp < branch.levels; hp++) {
                if (kind) mass += branch.hp[hp];
                else supported |= branch.hp[hp] != 0;
            }
        }
    }
    if ((target->has_history && !supported) || (target->has_belief && fabs(mass - 1) > 1e-12)) {
        fputs("Target oracle requires live support and normalized belief\n", stderr);
        exit(EXIT_FAILURE);
    }
}

#endif
