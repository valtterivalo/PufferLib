#pragma once

typedef float obs_t;
#define PUF_HAS_BOT_POLICY
#include "pufferenv.h"
#define Log OsrsSharedLog
#include "../osrs/encounters/encounter_riskfight.h"
#undef Log
#include "riskfight_training.h"
#ifdef OSRS_PUFFER_RENDER
#include "../osrs/osrs_puffer_render.h"
#endif

#define OBS_SIZE RF_OBS_SIZE
#define NUM_ATNS RF_HEADS
#define ACT_SIZES RF_ACTION_DIMS_INIT

struct Log {
    float episode_return, episode_length;
    float policy_0_score, draw_rate;
    float damage_reward, teleport_penalty, direct_ko_chance_mass, chance_reward;
    float kills, deaths, escapes, mutual_deaths, net_stake, n;
    float scripted_ticks, scripted_omissions, midfight_ticks, prefix_terminal;
    float self_teleports, opponent_teleports, both_teleports;
    float self_escape_healing[4], self_escape_no_boost, self_escape_both_no_special;
};
struct Env {
    Log log;
    int num_agents;
    unsigned int rng;
    Agent agents[2];
    int tag, boundary_reached;
    RiskfightState state;
    RiskfightContext context;
    RiskfightTraining training;
    int escape_trace;
#ifdef OSRS_PUFFER_RENDER
    void* renderer;
#endif
};

void puf_init(Env* env, Dict* kwargs) {
    riskfight_init_context((EncounterContext*)&env->context);
    riskfight_init_state((EncounterState*)&env->state, (EncounterContext*)&env->context);
    const char* keys[] = {"opponent_type", "self_play"};
    for (int i = 0; i < 2; i++) riskfight_put_int((EncounterState*)&env->state,
        (EncounterContext*)&env->context, keys[i], (int)dict_get(kwargs, keys[i]));
    const char* reward_keys[] = {"damage_reward_coeff", "teleport_penalty", "chance_reward_coeff"};
    for (int i = 0; i < 3; i++) {
        DictItem* item = dict_find(kwargs, reward_keys[i]);
        if (item) riskfight_put_float((EncounterState*)&env->state,
            (EncounterContext*)&env->context, reward_keys[i], (float)item->value);
    }
    env->num_agents = env->context.self_play ? 2 : 1;
    for (int i = 0; i < env->num_agents; i++) env->agents[i].policy = i;
    env->state.env.rng_state = env->rng ? env->rng : 1;
    env->training = (RiskfightTraining){0};
    env->training.config = riskfight_training_config(kwargs);
    DictItem* trace = dict_find(kwargs, "escape_trace");
    env->escape_trace = trace ? (int)trace->value : 0;
    assert(!trace || trace->value == 0 || trace->value == 1);
    env->training.rng = env->state.env.rng_state;
    assert(env->context.self_play || (env->training.config.scripted_probability == 0 &&
        env->training.config.midfight_probability == 0));
    memset(&env->log, 0, sizeof(env->log));
    riskfight_finalize_context((EncounterState*)&env->state, (EncounterContext*)&env->context);
}
static inline void puf_set_bot_policy(Env* env, int bot_policy) {
    assert((bot_policy >= RISKFIGHT_TRADER && bot_policy <= RISKFIGHT_AGGRESSIVE) ||
        (bot_policy >= RISKFIGHT_TACTICIAN && bot_policy <= RISKFIGHT_FLOOR));
    env->context.opponent = (RiskfightOpponent)bot_policy;
}

static void riskfight_native_observe(Env* env) {
    float mask[RF_MASK_SIZE];
    for (int i = 0; i < env->num_agents; i++) {
        riskfight_write_observation(&env->state, i, env->agents[i].observations);
        riskfight_write_action_mask(&env->state, i, mask);
        for (int j = 0; j < RF_MASK_SIZE; j++) env->agents[i].action_mask[j] = (unsigned char)mask[j];
    }
}
static void riskfight_native_reset(Env* env) {
    riskfight_training_reset(&env->training, &env->state, &env->context, env->tag);
}
void puf_reset(Env* env) {
    riskfight_native_reset(env);
    riskfight_native_observe(env);
    for (int i = 0; i < env->num_agents; i++) {
        env->agents[i].rewards[0] = 0;
        env->agents[i].terminals[0] = 0;
    }
}
void puf_step(Env* env) {
    int actions[2 * RF_HEADS] = {0};
    for (int i = 0; i < env->num_agents; i++)
        for (int j = 0; j < RF_HEADS; j++) actions[i * RF_HEADS + j] = (int)env->agents[i].actions[j];
    env->training.omissions += riskfight_training_actions(&env->training,
        &env->state, env->tag, actions);
    env->training.ticks++;
    riskfight_step((EncounterState*)&env->state, (EncounterContext*)&env->context, actions);
    for (int i = 0; i < env->num_agents; i++) {
        env->agents[i].rewards[0] = env->state.rewards[i];
        env->agents[i].terminals[0] = env->state.env.episode_over;
    }
    if (env->state.env.episode_over) {
        env->log.self_teleports += env->state.escaped[0];
        env->log.opponent_teleports += env->state.escaped[1];
        env->log.both_teleports += env->state.escaped[0] && env->state.escaped[1];
        for (int actor = 0; actor < 2; actor++) {
            if (!env->state.escaped[actor]) continue;
            const OsrsEscapeSupplies* supplies = &env->state.escape_supplies[actor][actor];
            const OsrsEscapeSupplies* other = &env->state.escape_supplies[actor][1 - actor];
            int both_no_special = osrs_escape_no_special(supplies) && osrs_escape_no_special(other);
            if (actor == 0) {
                env->log.self_escape_healing[supplies->healing]++;
                env->log.self_escape_no_boost += osrs_escape_no_boost(supplies);
                env->log.self_escape_both_no_special += both_no_special;
            }
            if (env->escape_trace) printf("RF_ESCAPE {\"actor\":%d,\"tick\":%d,\"hp\":%d,\"healing\":%d,"
                "\"marlins\":%d,\"brew_doses\":%d,\"halibut\":%d,\"pie_bites\":%d,\"boost_doses\":%d,"
                "\"attack\":%d,\"strength\":%d,\"no_boost\":%d,\"special_energy\":%d,\"minimum_spec_cost\":%d,"
                "\"opponent_special_energy\":%d,\"opponent_minimum_spec_cost\":%d,\"both_no_special\":%d,"
                "\"food_timer\":%d,\"potion_timer\":%d,\"combo_timer\":%d}\n",
                actor, env->state.escape_tick[actor], supplies->hp, supplies->healing,
                supplies->marlins, supplies->brew_doses, supplies->halibut, supplies->pie_bites, supplies->boost_doses,
                supplies->attack, supplies->strength, osrs_escape_no_boost(supplies), supplies->special_energy,
                supplies->minimum_spec_cost, other->special_energy, other->minimum_spec_cost, both_no_special,
                supplies->food_timer, supplies->potion_timer, supplies->combo_timer);
        }
        RiskfightOutcome outcome = env->state.outcome[0];
        env->log.kills += outcome == RISKFIGHT_KILL;
        env->log.deaths += outcome == RISKFIGHT_DEATH;
        env->log.escapes += outcome == RISKFIGHT_ESCAPE;
        env->log.mutual_deaths += outcome == RISKFIGHT_MUTUAL_DEATH;
        float net_stake = riskfight_outcome_reward(outcome);
        env->log.policy_0_score += 0.5f * (net_stake + 1.0f);
        env->log.draw_rate += net_stake == 0;
        env->log.net_stake += net_stake;
        env->log.episode_return += env->state.episode_returns[0];
        env->log.damage_reward += env->state.damage_rewards[0];
        env->log.direct_ko_chance_mass += env->state.direct_ko_chance_mass[0];
        env->log.chance_reward += env->state.chance_rewards[0];
        env->log.teleport_penalty += env->state.teleport_penalties[0];
        env->log.episode_length += env->state.env.tick - env->training.start_tick;
        if (env->training.opponent == RF_TRAIN_SCRIPTED)
            env->log.scripted_ticks += env->state.env.tick - env->training.start_tick;
        env->log.scripted_omissions += env->training.omissions;
        env->log.midfight_ticks += env->training.start_tick;
        env->log.prefix_terminal += env->training.start == RF_START_PREFIX_TERMINAL;
        env->log.n++;
        riskfight_native_reset(env);
    }
    riskfight_native_observe(env);
}
void puf_render(Env* env) {
#ifdef OSRS_PUFFER_RENDER
    if (!env->renderer) env->renderer = osrs_puffer_render_create(&ENCOUNTER_RISKFIGHT,
        (EncounterState*)&env->state, (EncounterContext*)&env->context);
    osrs_puffer_render_draw(env->renderer);
#else
    (void)env;
#endif
}
void puf_close(Env* env) {
#ifdef OSRS_PUFFER_RENDER
    if (env->renderer) osrs_puffer_render_destroy(env->renderer);
#endif
    riskfight_destroy_context((EncounterContext*)&env->context);
}
void puf_log(Log* log, Dict* out) {
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "policy_0_score", log->policy_0_score);
    dict_set(out, "draw_rate", log->draw_rate);
    dict_set(out, "damage_reward", log->damage_reward);
    dict_set(out, "direct_ko_chance_mass", log->direct_ko_chance_mass);
    dict_set(out, "chance_reward", log->chance_reward);
    dict_set(out, "teleport_penalty", log->teleport_penalty);
    dict_set(out, "kills", log->kills);
    dict_set(out, "deaths", log->deaths);
    dict_set(out, "escapes", log->escapes);
    dict_set(out, "mutual_deaths", log->mutual_deaths);
    dict_set(out, "net_stake", log->net_stake);
    dict_set(out, "score", log->net_stake);
    dict_set(out, "perf", log->net_stake);
    dict_set(out, "scripted_ticks", log->scripted_ticks);
    dict_set(out, "scripted_omissions", log->scripted_omissions);
    dict_set(out, "midfight_ticks", log->midfight_ticks);
    dict_set(out, "prefix_terminal", log->prefix_terminal);
    dict_set(out, "self_teleports", log->self_teleports);
    dict_set(out, "opponent_teleports", log->opponent_teleports);
    dict_set(out, "both_teleports", log->both_teleports);
    const char* healing_keys[] = {"self_escape_no_healing", "self_escape_other_healing",
        "self_escape_double_eats", "self_escape_triple_eats"};
    for (int i = 0; i < 4; i++) dict_set(out, healing_keys[i], log->self_escape_healing[i]);
    dict_set(out, "self_escape_no_boost", log->self_escape_no_boost);
    dict_set(out, "self_escape_both_no_special", log->self_escape_both_no_special);
}
