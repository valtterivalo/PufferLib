#include "wordle.h"

#define OBS_SIZE WORDLE_OBS_SIZE
#define NUM_ATNS 1
#define ACT_SIZES { WORDLE_NUM_WORDS }
#define OBS_TENSOR_T ByteTensor

#define Env Wordle
#include "vecenv.h"

void my_init(Env* env, Dict* kwargs) {
    env->num_agents = 1;
    env->reward_step   = (float)dict_get(kwargs, "reward_step")->value;
    env->reward_info   = (float)dict_get(kwargs, "reward_info")->value;
    env->reward_win    = (float)dict_get(kwargs, "reward_win")->value;
    env->reward_fail   = (float)dict_get(kwargs, "reward_fail")->value;
    env->reward_repeat = (float)dict_get(kwargs, "reward_repeat")->value;
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
