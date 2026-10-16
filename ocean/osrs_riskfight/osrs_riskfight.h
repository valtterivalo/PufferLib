#pragma once

#if defined(from_float) && !defined(PRECISION_FLOAT)
typedef precision_t obs_t;
#define RF_BF16_OBS
#else
typedef float obs_t;
#endif
#define PUF_HAS_BOT_POLICY
#include "pufferenv.h"
#if defined(__ARM_NEON)
#include <arm_neon.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#endif
#define Log OsrsSharedLog
#include "../osrs/encounters/encounter_riskfight.h"
#undef Log
#include "riskfight_training.h"
#include "riskfight_oppfit_bridge.h"
#ifdef OSRS_PUFFER_RENDER
#include "../osrs/osrs_puffer_render.h"
#endif

#define OBS_SIZE RF_OBS_SIZE
#define NUM_ATNS RF_HEADS
#define ACT_SIZES RF_ACTION_DIMS_INIT

struct Log {
    float episode_return, episode_length;
    float policy_0_score, draw_rate;
    float damage_reward, direct_ko_chance_mass, chance_reward;
    float kills, deaths, escapes, mutual_deaths, timeouts, ragged, ragger_fights, overeats, annoyed_exits, net_stake, rounds, n; /* timeouts: fights that hit the round limit (a draw); ragged: ragger fights that ended with his teleport */
    float scripted_ticks, scripted_omissions, midfight_ticks, prefix_terminal;
    float self_teleports, opponent_teleports, both_teleports;
    float rude_teleports, rematches, tp_self_spec, tp_opp_spec, tp_both_spec, tp_opp_could_spec, tp_hp, tp_fight_tick;
    float tp_food, tp_potion_doses, tp_self_no_special;
    float self_escape_healing[4], self_escape_no_boost, self_escape_both_no_special;
    float drink_brew, drink_sanfew, drink_combat;
    float eat_marlin, eat_halibut, eat_pie;
    float spec_voidwaker, spec_maul;
    float combat_opp, maul_opp;
    float ticks_tentacle, ticks_axe, ticks_voidwaker, ticks_maul;
    float swings, aligned_swings, adjacent_swings, spec_swings, spec_aligned, spec_adjacent;
    float oppfit_episodes;
    float wait_ticks; /* steps spent waiting between fights */
    float fights_pool, fights_selfplay, fights_frozen, fights_script; /* fights by who played seat 1 (the rest with oppfit_episodes) and by pool kit */
    float stops, ready_idle, vengs, eats, stop_spec, veng_spec, stop_veng_spec, spec_delay, norm_delay, hard_eats, hard_eats_zero;
};
struct Env {
    Log log;
    int num_agents;
    unsigned int rng;
    Agent agents[2];
    int tag, boundary_reached;
    int session_elapsed, session_fights; /* game ticks spent so far (fights and delays), fights ended */
    int wait_left;                       /* ticks of the wait between two fights still to pass: each is a step of no choice, reward 0 */
    int wait_obs_valid;                  /* the buffers hold the full observation and wait mask of this very state: the last full observe was made while waiting and nothing but the session clock has moved since */
    RiskfightTransition wait_transition; /* what the wait leads to */
    float session_stake;                 /* kills minus price times deaths so far, in kill units */
    RiskfightState state;
    RiskfightMaskKey mask_key[2]; /* what each agent's mask buffer holds (riskfight_write_action_mask_keyed) */
    RiskfightContext context;
    RiskfightTraining training;
    RiskfightAlign align;
    void* oppfit;
    float oppfit_probability;
    int oppfit_active;
    uint32_t oppfit_seed;
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
    DictItem* oppfit_p = dict_find(kwargs, "oppfit_probability");
    env->oppfit_probability = oppfit_p ? (float)oppfit_p->value : 0;
    assert(env->oppfit_probability >= 0 && env->oppfit_probability <= 1);
    env->oppfit = env->oppfit_probability > 0 ? oppfit_create() : NULL;
    DictItem* motor = dict_find(kwargs, "motor");
    if (motor) riskfight_put_int((EncounterState*)&env->state, (EncounterContext*)&env->context, "motor", (int)motor->value);
    DictItem* deployable = dict_find(kwargs, "deployable");
    if (deployable) riskfight_put_int((EncounterState*)&env->state, (EncounterContext*)&env->context, "deployable", (int)deployable->value);
    DictItem* live = dict_find(kwargs, "live_loadout");
    if (live) riskfight_put_int((EncounterState*)&env->state, (EncounterContext*)&env->context, "live_loadout", (int)live->value);
    DictItem* pool = dict_find(kwargs, "opponent_pool");
    if (pool) riskfight_put_int((EncounterState*)&env->state, (EncounterContext*)&env->context, "opponent_pool", (int)pool->value);
    const char* reward_keys[] = {"damage_reward_coeff", "chance_reward_coeff"};
    for (int i = 0; i < 2; i++) {
        DictItem* item = dict_find(kwargs, reward_keys[i]);
        if (item) riskfight_put_float((EncounterState*)&env->state,
            (EncounterContext*)&env->context, reward_keys[i], (float)item->value);
    }
    env->num_agents = env->context.self_play ? 2 : 1;
    for (int i = 0; i < env->num_agents; i++) env->agents[i].policy = i;
    env->state.env.rng_state = osrs_env_seed((uint32_t)env->rng, 0);
    env->training = (RiskfightTraining){0};
    env->training.config = riskfight_training_config(kwargs);
    env->training.rng = osrs_env_seed((uint32_t)env->rng, 1);
    /* Without a second seat every opponent is already the script or OppFit, so scripted_probability (the scripted share of the frozen seats) has nothing to
     * choose: a sweep that also searches env.self_play carries a value for it in the self_play 0 trials. Midfight starts need the second seat. */
    if (!env->context.self_play) env->training.config.scripted_probability = 0;
    assert(env->context.self_play || env->training.config.midfight_probability == 0);
    memset(&env->log, 0, sizeof(env->log));
    env->session_elapsed = env->session_fights = 0;
    env->session_stake = 0;
    riskfight_finalize_context((EncounterState*)&env->state, (EncounterContext*)&env->context);
}
static inline void puf_set_bot_policy(Env* env, int bot_policy) {
    assert(bot_policy >= 0 && bot_policy < RISKFIGHT_OPPONENTS);
    env->context.opponent = (RiskfightOpponent)bot_policy;
}

/** One float as the observation buffer holds it: round-to-nearest-even bf16, bit-identical to __float2bfloat16 for finite values, or the float itself. */
static inline obs_t riskfight_obs_one(float value) {
#ifdef RF_BF16_OBS
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return (uint16_t)((bits + 0x7FFFu + ((bits >> 16) & 1u)) >> 16);
#else
    return value;
#endif
}
/** Writes obs as round-to-nearest-even bf16, bit-identical to __float2bfloat16 for finite values: (bits + 0x7FFF + bit 16) >> 16, in 32-bit wrapping integer
 * arithmetic. Vectorized by hand: the compilers' -O2 cost models do not vectorize a loop whose length is not a multiple of the vector width. */
static inline void riskfight_obs_store(obs_t* out, const float* obs) {
#ifdef RF_BF16_OBS
    int j = 0;
#if defined(__ARM_NEON)
    const uint32x4_t bias = vdupq_n_u32(0x7FFFu), one = vdupq_n_u32(1u);
    for (; j + 8 <= RF_OBS_SIZE; j += 8) {
        uint32x4_t a = vreinterpretq_u32_f32(vld1q_f32(obs + j)), b = vreinterpretq_u32_f32(vld1q_f32(obs + j + 4));
        a = vaddq_u32(a, vaddq_u32(bias, vandq_u32(vshrq_n_u32(a, 16), one)));
        b = vaddq_u32(b, vaddq_u32(bias, vandq_u32(vshrq_n_u32(b, 16), one)));
        vst1q_u16(out + j, vcombine_u16(vshrn_n_u32(a, 16), vshrn_n_u32(b, 16)));
    }
#elif defined(__SSE2__)
    const __m128i bias = _mm_set1_epi32(0x7FFF), one = _mm_set1_epi32(1);
    for (; j + 8 <= RF_OBS_SIZE; j += 8) {
        __m128i a = _mm_castps_si128(_mm_loadu_ps(obs + j)), b = _mm_castps_si128(_mm_loadu_ps(obs + j + 4));
        a = _mm_add_epi32(a, _mm_add_epi32(bias, _mm_and_si128(_mm_srli_epi32(a, 16), one)));
        b = _mm_add_epi32(b, _mm_add_epi32(bias, _mm_and_si128(_mm_srli_epi32(b, 16), one)));
        /* the high halves as signed 16-bit values (sign-extend the low 16 bits of a >> 16), so the signed saturating pack passes all 16 bits through */
        a = _mm_srai_epi32(_mm_slli_epi32(_mm_srli_epi32(a, 16), 16), 16);
        b = _mm_srai_epi32(_mm_slli_epi32(_mm_srli_epi32(b, 16), 16), 16);
        _mm_storeu_si128((__m128i*)(out + j), _mm_packs_epi32(a, b));
    }
#endif
    for (; j < RF_OBS_SIZE; j++) out[j] = riskfight_obs_one(obs[j]);
#else
    memcpy(out, obs, RF_OBS_SIZE * sizeof(float));
#endif
}
static void riskfight_native_observe(Env* env) {
    env->state.session_ticks_left = env->training.config.session_ticks - env->session_elapsed;
    env->state.session_wait_left = env->wait_left;
    env->state.session_rep = env->training.rep;
    for (int i = 0; i < env->num_agents; i++) {
        float obs[RF_OBS_SIZE];
        riskfight_write_observation(&env->state, i, obs);
        riskfight_obs_store(env->agents[i].observations, obs);
        if (env->wait_left > 0) {
            riskfight_write_wait_mask(env->agents[i].action_mask);
            env->mask_key[i].valid = 0;
        } else riskfight_write_action_mask_keyed(&env->state, i, &env->mask_key[i], env->agents[i].action_mask);
    }
    env->wait_obs_valid = env->wait_left > 0;
}
/** The observation of a tick of the wait between fights after the one that wrote it in full: the sim is not stepped while waiting, so every float of the
 * observation and every mask byte stays as it is except the session clock (ticks left, wait left, reputation), which is rewritten here. */
static void riskfight_native_observe_wait_tick(Env* env) {
    env->state.session_ticks_left = env->training.config.session_ticks - env->session_elapsed;
    env->state.session_wait_left = env->wait_left;
    env->state.session_rep = env->training.rep;
    float clock[3];
    riskfight_session_clock(&env->state, clock);
    for (int i = 0; i < env->num_agents; i++) {
        obs_t* out = env->agents[i].observations + RF_SESSION_START;
        out[0] = riskfight_obs_one(clock[0]);
        out[3] = riskfight_obs_one(clock[1]);
        out[4] = riskfight_obs_one(clock[2]);
    }
}
/** Who plays the opponent of a fight just reset: when the script or OppFit drives him (always in a self_play 0 env, in a self-play env when the session's
 * opponent was drawn scripted), the fitted human with probability oppfit_probability on the kits it was fitted on, else the script; drawn per opponent (a
 * rematch keeps the draw and the seed). A net in seat 1 (self-play, or the frozen checkpoint) gets no hook. */
static void riskfight_native_opponent_hooks(Env* env, int new_opponent) {
    env->context.opp_hook = NULL;
    env->context.opp_after = NULL;
    env->context.opp_user = NULL;
    if (!env->oppfit) return;
    const int driven = !env->context.self_play || env->context.opp_driven;
    if (new_opponent) {
        /* OppFit plays the melee kits whose specials it was fitted on (VW, maul, AGS, claws); every other pool kit is left to the script. */
        env->oppfit_active = driven && (!env->context.opp_kit || env->context.opp_kit->oppfit_ok) && encounter_rand_float(&env->training.rng) < env->oppfit_probability;
        if (env->oppfit_active) env->oppfit_seed = xorshift32(&env->training.rng) | 1;
    }
    if (!env->oppfit_active) return;
    oppfit_reset(env->oppfit, &env->state, env->oppfit_seed);
    env->context.opp_user = env->oppfit;
    env->context.opp_hook = oppfit_actions;
    env->context.opp_after = oppfit_after;
}
/** A new session: full supplies, the first opponent, the whole budget. */
static void riskfight_native_reset(Env* env) {
    riskfight_align_reset(&env->align);
    riskfight_training_reset(&env->training, &env->state, &env->context, env->tag);
    riskfight_native_opponent_hooks(env, 1);
    env->session_elapsed = env->session_fights = 0;
    env->session_stake = 0;
    env->wait_left = 0;
}
void puf_reset(Env* env) {
    riskfight_native_reset(env);
    riskfight_native_observe(env);
    for (int i = 0; i < env->num_agents; i++) {
        env->agents[i].rewards[0] = 0;
        env->agents[i].terminals[0] = 0;
    }
}
/** Adds one finished fight's tallies to the log; the session totals (n, rounds, net_stake) are added when the session ends. */
static void riskfight_native_log_round(Env* env) {
    const RiskfightState* s = &env->state;
    env->log.self_teleports += s->escaped[0];
    env->log.opponent_teleports += s->escaped[1];
    env->log.both_teleports += s->escaped[0] && s->escaped[1];
    if (s->escaped[0]) {
        const OsrsEscapeSupplies* supplies = &s->escape_supplies[0][0];
        env->log.self_escape_healing[supplies->healing]++;
        env->log.self_escape_no_boost += osrs_escape_no_boost(supplies);
        env->log.self_escape_both_no_special += osrs_escape_no_special(supplies) &&
            osrs_escape_no_special(&s->escape_supplies[0][1]);
        env->log.rude_teleports += riskfight_rude_teleport(s);
        /* Specials each side had used when we teleported, and whether he could still spec (by his true energy), our hitpoints and the fight tick. */
        env->log.tp_self_spec += s->specs_used[0] > 0;
        env->log.tp_opp_spec += s->specs_used[1] > 0;
        env->log.tp_both_spec += s->specs_used[0] > 0 && s->specs_used[1] > 0;
        env->log.tp_opp_could_spec += !osrs_escape_no_special(&s->escape_supplies[0][1]);
        env->log.tp_hp += supplies->hp;
        env->log.tp_food += supplies->food;
        env->log.tp_potion_doses += supplies->potion_doses;
        env->log.tp_self_no_special += osrs_escape_no_special(supplies);
        env->log.tp_fight_tick += s->escape_tick[0];
    }
    if (env->session_fights == 0) {
        env->log.midfight_ticks += env->training.start_tick;
        env->log.prefix_terminal += env->training.start == RF_START_PREFIX_TERMINAL;
    }
    env->log.episode_return += s->episode_returns[0];
    env->log.damage_reward += s->damage_rewards[0];
    env->log.direct_ko_chance_mass += s->direct_ko_chance_mass[0];
    env->log.chance_reward += s->chance_rewards[0];
    {
        RiskfightOutcome outcome = s->outcome[0];
        env->log.kills += outcome == RISKFIGHT_KILL;
        env->log.deaths += outcome == RISKFIGHT_DEATH;
        env->log.escapes += outcome == RISKFIGHT_ESCAPE;
        env->log.mutual_deaths += outcome == RISKFIGHT_MUTUAL_DEATH;
        env->log.timeouts += outcome == RISKFIGHT_TIMEOUT;
        env->log.ragged += outcome == RISKFIGHT_RAGGED;
        env->log.overeats += s->overeats[0];
        env->log.annoyed_exits += riskfight_annoyed_exit(s);
        env->log.ragger_fights += s->ragger;
        float stake = riskfight_outcome_reward(outcome, s->death_price);
        env->log.policy_0_score += 0.5f * (riskfight_outcome_reward(outcome, 1) + 1.0f);
        env->log.draw_rate += stake == 0;
        env->session_stake += stake;
    }
    env->log.episode_length += s->env.tick - env->training.start_tick;
    if (env->training.opponent == RF_TRAIN_SCRIPTED)
        env->log.scripted_ticks += s->env.tick - env->training.start_tick;
    {
        const RiskfightAlign* a = &env->align;
        env->log.swings += a->swings; env->log.aligned_swings += a->aligned; env->log.adjacent_swings += a->adjacent;
        env->log.spec_swings += a->spec_swings; env->log.spec_aligned += a->spec_aligned; env->log.spec_adjacent += a->spec_adjacent;
        env->log.stops += a->stops; env->log.ready_idle += a->ready_idle; env->log.vengs += a->vengs; env->log.eats += a->eats;
        env->log.hard_eats += a->hard_eats; env->log.hard_eats_zero += a->hard_eats_zero;
        env->log.stop_spec += a->stop_spec; env->log.veng_spec += a->veng_spec; env->log.stop_veng_spec += a->stop_veng_spec; env->log.spec_delay += a->spec_delay; env->log.norm_delay += a->norm_delay;
    }
    env->log.oppfit_episodes += env->context.opp_hook != NULL;
    env->log.fights_pool += env->context.opp_kit != NULL;
    {
        const int net = env->context.self_play && !env->context.opp_driven;
        env->log.fights_selfplay += net && env->tag == 0;
        env->log.fights_frozen += net && env->tag > 0;
        env->log.fights_script += !net && env->context.opp_hook == NULL;
    }
    env->log.drink_brew += s->drink_brew[0];
    env->log.drink_sanfew += s->drink_sanfew[0];
    env->log.drink_combat += s->drink_combat[0];
    env->log.eat_marlin += s->eat_marlin[0];
    env->log.eat_halibut += s->eat_halibut[0];
    env->log.eat_pie += s->eat_pie[0];
    env->log.spec_voidwaker += s->spec_voidwaker[0];
    env->log.spec_maul += s->spec_maul[0];
}
/** The fight after a wait: the transition's rematch or new opponent, with the supplies aged by the wait. */
static void riskfight_native_next_fight(Env* env) {
    const RiskfightTransition* t = &env->wait_transition;
    env->log.rematches += t->rematch;
    riskfight_align_reset(&env->align);
    riskfight_training_next_fight(&env->training, &env->state, &env->context, t, env->tag);
    riskfight_native_opponent_hooks(env, !t->rematch);
}
/** The end of a step of either kind: the terminals, and the session's totals and a fresh session when its budget is spent. */
static void riskfight_native_finish_step(Env* env, int wait_step) {
    env->training.rep = riskfight_rep_decay(env->training.rep);
    const int budget = env->training.config.session_ticks;
    int session_over = env->session_elapsed >= budget;
    for (int i = 0; i < env->num_agents; i++) env->agents[i].terminals[0] = session_over;
    if (session_over) {
        env->log.rounds += env->session_fights;
        /* kills minus price times deaths per hour of session, so sessions of different length score alike */
        env->log.net_stake += env->session_stake * (RF_TICKS_PER_HOUR / (float)budget);
        env->log.scripted_omissions += env->training.omissions;
        env->log.n++;
#ifdef OSRS_PUFFER_RENDER
        if (!env->renderer) riskfight_native_reset(env);
        else { env->state.env.episode_over = 1; env->wait_left = 0; }
#else
        riskfight_native_reset(env);
#endif
    }
    if (wait_step && !session_over && env->wait_left > 0 && env->wait_obs_valid) riskfight_native_observe_wait_tick(env);
    else riskfight_native_observe(env);
}

void puf_step(Env* env) {
#ifdef OSRS_PUFFER_RENDER
    if (env->renderer && env->state.env.episode_over && env->wait_left == 0) {
        for (int i = 0; i < env->num_agents; i++) {
            env->agents[i].rewards[0] = 0;
            env->agents[i].terminals[0] = 1;
        }
        riskfight_native_observe(env);
        return;
    }
#endif
    if (env->wait_left > 0) {
        /* A tick of the wait between fights: nothing is chosen (the mask allows only the no-op), nothing is earned, the clock runs. The next fight starts, with
         * what the wait did to the supplies (special energy regenerating, eating up), on the tick the wait ends. */
        for (int i = 0; i < env->num_agents; i++) env->agents[i].rewards[0] = 0;
        env->session_elapsed++;
        env->log.wait_ticks++;
        if (--env->wait_left == 0 && env->session_elapsed < env->training.config.session_ticks) riskfight_native_next_fight(env);
        riskfight_native_finish_step(env, 1);
        return;
    }
    int actions[2 * RF_HEADS] = {0};
    for (int i = 0; i < env->num_agents; i++)
        for (int j = 0; j < RF_HEADS; j++) actions[i * RF_HEADS + j] = (int)env->agents[i].actions[j];
    int stop_issued = actions[RF_PRIMARY] == RF_STOP;
    env->context.opp_omit = riskfight_training_omit(&env->training);
    env->training.omissions += env->context.opp_omit;
    env->training.ticks++;
    riskfight_step((EncounterState*)&env->state, (EncounterContext*)&env->context, actions);
    riskfight_align_observe(&env->state, 0, stop_issued, &env->align);
    env->log.ticks_tentacle += env->state.ticks_tentacle[0];
    env->log.ticks_axe += env->state.ticks_axe[0];
    env->log.ticks_voidwaker += env->state.ticks_voidwaker[0];
    env->log.ticks_maul += env->state.ticks_maul[0];
    env->log.combat_opp += env->state.combat_opp[0];
    env->log.maul_opp += env->state.maul_opp[0];
    env->state.ticks_tentacle[0] = env->state.ticks_axe[0] = 0;
    env->state.ticks_voidwaker[0] = env->state.ticks_maul[0] = 0;
    env->state.ticks_tentacle[1] = env->state.ticks_axe[1] = 0;
    env->state.ticks_voidwaker[1] = env->state.ticks_maul[1] = 0;
    env->state.combat_opp[0] = env->state.maul_opp[0] = 0;
    env->state.combat_opp[1] = env->state.maul_opp[1] = 0;
    for (int i = 0; i < env->num_agents; i++) env->agents[i].rewards[0] = env->state.rewards[i];
    const int budget = env->training.config.session_ticks;
    env->session_elapsed++;
    if (env->state.env.episode_over) {
        riskfight_native_log_round(env);
        env->session_fights++;
        if (env->session_elapsed < budget) {
            env->training.rep += riskfight_rude_teleport(&env->state) + riskfight_annoyed_exit(&env->state); /* each rude teleport of ours, and each exit of his over our overeating, is remembered */
            env->wait_transition = riskfight_session_transition(&env->state, &env->training.rng, env->training.rep);
            env->wait_left = env->wait_transition.delay;
            if (env->wait_left == 0) riskfight_native_next_fight(env);
        }
    }
    riskfight_native_finish_step(env, 0);
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
    if (env->oppfit) oppfit_destroy(env->oppfit);
#ifdef OSRS_PUFFER_RENDER
    if (env->renderer) osrs_puffer_render_destroy(env->renderer);
#endif
    riskfight_destroy_context((EncounterContext*)&env->context);
}
void puf_log(Log* log, Dict* out) {
    dict_set(out, "kills", log->kills);
    dict_set(out, "deaths", log->deaths);
    dict_set(out, "escapes", log->escapes);
    dict_set(out, "mutual_deaths", log->mutual_deaths);
    dict_set(out, "timeouts", log->timeouts);
    dict_set(out, "ragged", log->ragged);
    dict_set(out, "overeats", log->overeats);
    dict_set(out, "annoyed_exits", log->annoyed_exits);
    dict_set(out, "ragger_fights", log->ragger_fights);
    dict_set(out, "net_stake", log->net_stake);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "oppfit_episodes", log->oppfit_episodes);
    dict_set(out, "wait_ticks", log->wait_ticks);
    dict_set(out, "fights_pool", log->fights_pool);
    dict_set(out, "fights_selfplay", log->fights_selfplay);
    dict_set(out, "fights_frozen", log->fights_frozen);
    dict_set(out, "fights_script", log->fights_script);
    dict_set(out, "swings", log->swings);
    dict_set(out, "aligned_swings", log->aligned_swings);
    dict_set(out, "adjacent_swings", log->adjacent_swings);
    dict_set(out, "spec_swings", log->spec_swings);
    dict_set(out, "spec_aligned", log->spec_aligned);
    dict_set(out, "spec_adjacent", log->spec_adjacent);
    dict_set(out, "stops", log->stops);
    dict_set(out, "ready_idle", log->ready_idle);
    dict_set(out, "vengs", log->vengs);
    dict_set(out, "eats", log->eats);
    dict_set(out, "hard_eats", log->hard_eats);
    dict_set(out, "hard_eats_zero", log->hard_eats_zero);
    dict_set(out, "stop_spec", log->stop_spec);
    dict_set(out, "veng_spec", log->veng_spec);
    dict_set(out, "stop_veng_spec", log->stop_veng_spec);
    dict_set(out, "spec_delay", log->spec_delay);
    dict_set(out, "norm_delay", log->norm_delay);
    dict_set(out, "eat_marlin", log->eat_marlin);
    dict_set(out, "eat_halibut", log->eat_halibut);
    dict_set(out, "eat_pie", log->eat_pie);
    dict_set(out, "drink_brew", log->drink_brew);
    dict_set(out, "drink_sanfew", log->drink_sanfew);
    dict_set(out, "drink_combat", log->drink_combat);
    dict_set(out, "spec_voidwaker", log->spec_voidwaker);
    dict_set(out, "spec_maul", log->spec_maul);
    dict_set(out, "self_teleports", log->self_teleports);
    dict_set(out, "opponent_teleports", log->opponent_teleports);
    dict_set(out, "rounds", log->rounds);
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "policy_0_score", log->policy_0_score);
    dict_set(out, "draw_rate", log->draw_rate);
    dict_set(out, "damage_reward", log->damage_reward);
    dict_set(out, "direct_ko_chance_mass", log->direct_ko_chance_mass);
    dict_set(out, "chance_reward", log->chance_reward);
    dict_set(out, "rude_teleports", log->rude_teleports);
    dict_set(out, "rematches", log->rematches);
    dict_set(out, "tp_self_spec", log->tp_self_spec);
    dict_set(out, "tp_opp_spec", log->tp_opp_spec);
    dict_set(out, "tp_both_spec", log->tp_both_spec);
    dict_set(out, "tp_opp_could_spec", log->tp_opp_could_spec);
    dict_set(out, "tp_hp", log->tp_hp);
    dict_set(out, "tp_fight_tick", log->tp_fight_tick);
    dict_set(out, "tp_food", log->tp_food);
    dict_set(out, "tp_potion_doses", log->tp_potion_doses);
    dict_set(out, "tp_self_no_special", log->tp_self_no_special);
    dict_set(out, "score", log->net_stake);
    dict_set(out, "perf", log->net_stake);
    dict_set(out, "scripted_ticks", log->scripted_ticks);
    dict_set(out, "scripted_omissions", log->scripted_omissions);
    dict_set(out, "midfight_ticks", log->midfight_ticks);
    dict_set(out, "prefix_terminal", log->prefix_terminal);
    dict_set(out, "both_teleports", log->both_teleports);
    dict_set(out, "self_escape_no_boost", log->self_escape_no_boost);
    dict_set(out, "self_escape_both_no_special", log->self_escape_both_no_special);
    dict_set(out, "combat_opp", log->combat_opp);
    dict_set(out, "maul_opp", log->maul_opp);
    dict_set(out, "ticks_tentacle", log->ticks_tentacle);
    dict_set(out, "ticks_axe", log->ticks_axe);
    dict_set(out, "ticks_voidwaker", log->ticks_voidwaker);
    dict_set(out, "ticks_maul", log->ticks_maul);
    const char* healing_keys[] = {"self_escape_no_healing", "self_escape_other_healing",
        "self_escape_double_eats", "self_escape_triple_eats"};
    for (int i = 0; i < 4; i++) dict_set(out, healing_keys[i], log->self_escape_healing[i]);
}
