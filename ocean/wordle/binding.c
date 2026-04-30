#include "wordle.h"

#define OBS_SIZE WORDLE_OBS_SIZE
#define NUM_ATNS 1
#define ACT_SIZES { WORDLE_NUM_WORDS }
#define OBS_TENSOR_T ByteTensor

#define Env Wordle
#include "vecenv.h"

static inline float kwarg_or(Dict* kwargs, const char* key, float fallback) {
    DictItem* item = dict_get_unsafe(kwargs, key);
    return item ? (float)item->value : fallback;
}

void my_init(Env* env, Dict* kwargs) {
    env->num_agents = 1;
    env->reward_step   = kwarg_or(kwargs, "reward_step",   -0.01f);
    env->reward_info   = kwarg_or(kwargs, "reward_info",    0.10f);
    env->reward_win    = kwarg_or(kwargs, "reward_win",     1.00f);
    env->reward_fail   = kwarg_or(kwargs, "reward_fail",   -0.20f);
    env->reward_repeat = kwarg_or(kwargs, "reward_repeat", -0.05f);
    env->client = NULL;
}

void my_log(Log* log, Dict* out) {
    dict_set(out, "perf",                  log->perf);
    dict_set(out, "score",                 log->score);
    dict_set(out, "episode_return",        log->episode_return);
    dict_set(out, "episode_length",        log->episode_length);
    dict_set(out, "win_rate",              log->win_rate);
    dict_set(out, "guesses",               log->guesses);
    dict_set(out, "repeat_rate",           log->repeat_rate);
    dict_set(out, "info_bits",             log->info_bits);
    dict_set(out, "final_log2_candidates", log->final_log2_candidates);
    dict_set(out, "mean_greens",           log->mean_greens);
    dict_set(out, "mean_yellows",          log->mean_yellows);
}
