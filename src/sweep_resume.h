#ifndef PUFFER_SWEEP_RESUME_H
#define PUFFER_SWEEP_RESUME_H
#include "ini.h"
#include <math.h>

static void sweep_resume_require_equal(Ini* current, Ini* previous,
        const char* section, const char* key) {
    DictItem* a = dict_find(puf_ini_section(current, section, 0), key);
    DictItem* b = dict_find(puf_ini_section(previous, section, 0), key);
    double av, bv;
    int equal = a && b && ((puf_ini_parse_val(a->str, &av) &&
        puf_ini_parse_val(b->str, &bv)) ? av == bv : strcmp(a->str, b->str) == 0);
    if (!equal) {
        fprintf(stderr, "sweep resume mismatch: [%s] %s\n", section, key);
        exit(1);
    }
}

typedef enum {
    SWEEP_RESUME_SUCCESS,
    SWEEP_RESUME_FAILED,
} SweepResumeStatus;

static SweepResumeStatus sweep_resume_validate(Ini* current, Ini* previous) {
    const char* objective = puf_ini_get_str(current, "sweep", "objective_id");
    if (!objective[0] || strcmp(objective,
            puf_ini_get_str(previous, "resume", "objective_id")) != 0) {
        fprintf(stderr, "sweep resume objective_id mismatch\n");
        exit(1);
    }
    const char* objective_keys[] = {"goal", "metric", "metric_distribution"};
    for (int i = 0; i < 3; i++)
        sweep_resume_require_equal(current, previous, "sweep", objective_keys[i]);
    const char* ladder_keys[] = {"eval_bots", "eval_bot_games", "eval_bot_envs", "eval_bot_threads"};
    Dict* selfplay = NULL;
    for (int i = 0; i < current->num_sections; i++)
        if (strcmp(current->sections[i].name, "selfplay") == 0) selfplay = &current->sections[i];
    if (selfplay && dict_find(selfplay, "eval_bots"))
        for (int i = 0; i < 4; i++)
            sweep_resume_require_equal(current, previous, "selfplay", ladder_keys[i]);
    const char* space_keys[] = {"distribution", "min", "max", "scale"};
    int current_spaces = 0, previous_spaces = 0;
    for (int i = 0; i < current->num_sections; i++) {
        const char* section = current->sections[i].name;
        if (strncmp(section, "sweep.", 6) != 0) continue;
        current_spaces++;
        for (int k = 0; k < 4; k++)
            sweep_resume_require_equal(current, previous, section, space_keys[k]);
    }
    for (int i = 0; i < previous->num_sections; i++)
        previous_spaces += strncmp(previous->sections[i].name, "sweep.", 6) == 0;
    if (previous_spaces != current_spaces) {
        fprintf(stderr, "sweep resume search parameter count mismatch\n");
        exit(1);
    }
    const char* status = puf_ini_get_str(previous, "resume", "status");
    SweepResumeStatus outcome;
    if (strcmp(status, "success") == 0) outcome = SWEEP_RESUME_SUCCESS;
    else if (strcmp(status, "failed") == 0) outcome = SWEEP_RESUME_FAILED;
    else {
        fprintf(stderr, "sweep resume status must be success or failed\n");
        exit(1);
    }
    double cost = puf_ini_get(previous, "resume", "cost");
    if (!isfinite(cost) || cost <= 0) {
        fprintf(stderr, "sweep resume requires positive finite cost\n");
        exit(1);
    }
    if (outcome == SWEEP_RESUME_SUCCESS &&
            !isfinite(puf_ini_get(previous, "resume", "score"))) {
        fprintf(stderr, "successful sweep resume requires finite score\n");
        exit(1);
    }
    return outcome;
}
#endif
