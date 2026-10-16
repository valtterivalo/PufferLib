#pragma once
/* A trainer-shaped vector of osrs_riskfight envs for the env benchmark and the trace-hash test: the sweep's env kwargs (config/osrs_riskfight.ini +
 * scripts/sweep_riskfight_live.sh), the Env array calloc'd like src/pufferl.cu env_setup, one contiguous observation / mask / action / reward / terminal
 * buffer, half the envs tagged as facing a frozen policy (vec.hist_policy_percent 0.5). Observations are bf16 like the trainer's unless TRACE_FLOAT.
 *
 *   cc -std=gnu11 -O2 -I. -Isrc -I$RAYLIB_INC ... riskfight_oppfit_bridge.o (RF_OPPFIT_MODEL / RF_OPPFIT_EAT name ocean/osrs/tests/fixtures/{model,eat_mixture}.fit) */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <time.h>
#ifndef TRACE_FLOAT
typedef uint16_t precision_t;
#define from_float(x) (x) /* only its definition matters: osrs_riskfight.h then stores observations as precision_t */
#endif
#include "../../osrs_riskfight/osrs_riskfight.h"

typedef struct {
    Env* envs;
    int count, agents;
    obs_t* obs;
    float* actions;
    float* rewards;
    float* terminals;
    unsigned char* mask;
    uint32_t* action_rng; /* one xorshift stream per env, for the random policy */
} HarnessVec;

static double harness_now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e9 + t.tv_nsec;
}

/** self_play 1: the sweep's training env (two seats). self_play 0: the bot-eval env of the sweep's score (one seat, the opponent is the script or OppFit). */
static void harness_vec_create_variant(HarnessVec* v, int count, int rng_base, int self_play) {
    DictItem items[] = {
        {.key = "opponent_type", .value = 0}, {.key = "self_play", .value = self_play}, {.key = "live_loadout", .value = 3}, {.key = "opponent_pool", .value = 1},
        {.key = "selfplay_pool_probability", .value = 0.5}, {.key = "deployable", .value = 1}, {.key = "motor", .value = 1}, {.key = "death_price", .value = 0},
        {.key = "oppfit_probability", .value = 0.5}, {.key = "domain_randomization", .value = 1}, {.key = "damage_reward_coeff", .value = 0},
        {.key = "chance_reward_coeff", .value = 0}, {.key = "scripted_probability", .value = 0.5}, {.key = "scripted_omission_initial", .value = 0.8},
        {.key = "scripted_omission_decay_ticks", .value = 150000}, {.key = "midfight_probability", .value = 0}, {.key = "midfight_max_ticks", .value = 128},
        {.key = "session_ticks", .value = 3000}, {.key = "curveball_probability", .value = 0.1}};
    Dict kwargs = {.items = items, .size = sizeof(items) / sizeof(*items)};
    v->count = count;
    v->envs = calloc(count, sizeof(Env));
    for (int i = 0; i < count; i++) {
        v->envs[i].rng = (unsigned)(rng_base + i);
        puf_init(&v->envs[i], &kwargs);
    }
    v->agents = 0;
    for (int i = 0; i < count; i++) v->agents += v->envs[i].num_agents;
    v->obs = calloc((size_t)v->agents * RF_OBS_SIZE, sizeof(obs_t));
    v->actions = calloc((size_t)v->agents * RF_HEADS, sizeof(float));
    v->rewards = calloc(v->agents, sizeof(float));
    v->terminals = calloc(v->agents, sizeof(float));
    v->mask = malloc((size_t)v->agents * RF_MASK_SIZE);
    memset(v->mask, 1, (size_t)v->agents * RF_MASK_SIZE);
    v->action_rng = calloc(count, sizeof(uint32_t));
    int phys = 0;
    for (int i = 0; i < count; i++) {
        Env* e = &v->envs[i];
        e->tag = self_play && i >= count - count / 2; /* the frozen half */
        v->action_rng[i] = 0x9e3779b9u * (uint32_t)(rng_base + i + 1) | 1;
        for (int s = 0; s < e->num_agents; s++, phys++) {
            Agent* a = &e->agents[s];
            a->observations = v->obs + (size_t)phys * RF_OBS_SIZE;
            a->actions = v->actions + (size_t)phys * RF_HEADS;
            a->rewards = v->rewards + phys;
            a->terminals = v->terminals + phys;
            a->action_mask = v->mask + (size_t)phys * RF_MASK_SIZE;
        }
    }
    for (int i = 0; i < count; i++) puf_reset(&v->envs[i]);
}

static void harness_vec_create(HarnessVec* v, int count, int rng_base) { harness_vec_create_variant(v, count, rng_base, 1); }

static void harness_vec_destroy(HarnessVec* v) {
    for (int i = 0; i < v->count; i++) puf_close(&v->envs[i]);
    free(v->envs); free(v->obs); free(v->actions); free(v->rewards); free(v->terminals); free(v->mask); free(v->action_rng);
}

typedef enum { POLICY_RANDOM, POLICY_SCRIPT, POLICY_MIXED } HarnessPolicy;

/** Fills env i's actions for the tick about to be stepped. A random policy draws uniformly among the entries each head's mask allows; a script plays the
 * human profiles of the sim's own scripted fighters (riskfight_script), so fights last as long as real ones. Mixed: even envs random, odd envs script. */
static void harness_choose_actions(HarnessVec* v, int i, HarnessPolicy policy) {
    Env* e = &v->envs[i];
    if (e->wait_left > 0) return; /* the tick is a step of no choice; the actions are not read */
    const int script = policy == POLICY_SCRIPT || (policy == POLICY_MIXED && (i & 1));
    for (int s = 0; s < e->num_agents; s++) {
        float* out = e->agents[s].actions;
        if (script) {
            float obs[RF_OBS_SIZE];
            int actions[RF_HEADS];
            riskfight_write_observation(&e->state, s, obs);
            riskfight_script(obs, &RISKFIGHT_HUMAN_PROFILES[(i + s) % (RISKFIGHT_HUMAN_TRAIN_PROFILES + RISKFIGHT_HUMAN_HELDOUT_PROFILES)],
                e->state.script_seed[s], e->state.consume_ticks[s], actions);
            for (int h = 0; h < RF_HEADS; h++) out[h] = (float)actions[h];
        } else {
            const unsigned char* mask = e->agents[s].action_mask;
            uint32_t* rng = &v->action_rng[i];
            for (int h = 0; h < RF_HEADS; h++) {
                const unsigned char* m = mask + RF_HEAD_OFFSET[h];
                int allowed = 0;
                for (int a = 0; a < RF_ACTION_DIMS[h]; a++) allowed += m[a] != 0;
                int pick = allowed > 1 ? (int)(xorshift32(rng) % (uint32_t)allowed) : 0, chosen = 0;
                for (int a = 0; a < RF_ACTION_DIMS[h]; a++) if (m[a] && pick-- == 0) { chosen = a; break; }
                out[h] = (float)chosen;
            }
        }
    }
}

/** 64-bit multiply-xor hash over 8-byte words (the buffers are multiples of 8 bytes or the tail is folded byte-wise). */
static uint64_t harness_hash(uint64_t h, const void* p, size_t n) {
    const unsigned char* b = p;
    while (n >= 8) {
        uint64_t w;
        memcpy(&w, b, 8);
        h = (h ^ w) * UINT64_C(0x9E3779B97F4A7C15);
        h ^= h >> 29;
        b += 8; n -= 8;
    }
    while (n--) h = (h ^ *b++) * UINT64_C(1099511628211);
    return h;
}
