#pragma once

typedef struct {
    float scripted_probability, omission_initial;
    uint64_t omission_decay_ticks;
    float midfight_probability;
    int midfight_max_ticks;
} RiskfightTrainingConfig;

typedef enum { RF_TRAIN_LEARNED, RF_TRAIN_SCRIPTED } RiskfightTrainingOpponent;
typedef enum { RF_START_FRESH, RF_START_MIDFIGHT, RF_START_PREFIX_TERMINAL } RiskfightTrainingStart;

typedef struct {
    RiskfightTrainingConfig config;
    uint32_t rng;
    uint64_t ticks;
    RiskfightTrainingOpponent opponent;
    RiskfightOpponent script;
    RiskfightTrainingStart start;
    int start_tick;
    int omissions;
} RiskfightTraining;

static RiskfightTrainingConfig riskfight_training_config(Dict* kwargs) {
    RiskfightTrainingConfig config = {0};
    const char* names[] = {"scripted_probability", "scripted_omission_initial",
        "scripted_omission_decay_ticks", "midfight_probability", "midfight_max_ticks"};
    double values[5] = {0};
    for (int i = 0; i < 5; i++) {
        DictItem* item = dict_find(kwargs, names[i]);
        if (item) values[i] = item->value;
    }
    assert(values[0] >= 0 && values[0] <= 1);
    assert(values[1] >= 0 && values[1] <= 1);
    assert(values[2] >= 0 && values[2] <= INT_MAX && values[2] == floor(values[2]));
    assert(values[3] >= 0 && values[3] <= 1);
    assert(values[4] >= 0 && values[4] <= INT_MAX && values[4] == floor(values[4]));
    assert(values[1] == 0 || values[2] > 0);
    assert(values[3] == 0 || values[4] > 0);
    config.scripted_probability = values[0];
    config.omission_initial = values[1];
    config.omission_decay_ticks = (uint64_t)values[2];
    config.midfight_probability = values[3];
    config.midfight_max_ticks = (int)values[4];
    return config;
}

static float riskfight_training_omission(const RiskfightTraining* training) {
    if (training->ticks >= training->config.omission_decay_ticks) return 0;
    return training->config.omission_initial *
        (1.0 - (double)training->ticks / training->config.omission_decay_ticks);
}

static int riskfight_training_draw(RiskfightTraining* training, float probability) {
    return probability > 0 && encounter_rand_float(&training->rng) < probability;
}

static void riskfight_training_clear_returns(RiskfightState* state) {
    memset(state->rewards, 0, sizeof(state->rewards));
    memset(state->episode_returns, 0, sizeof(state->episode_returns));
    memset(state->damage_rewards, 0, sizeof(state->damage_rewards));
    memset(state->direct_ko_chance_mass, 0, sizeof(state->direct_ko_chance_mass));
    memset(state->chance_rewards, 0, sizeof(state->chance_rewards));
    memset(state->teleport_penalties, 0, sizeof(state->teleport_penalties));
}

static RiskfightTrainingStart riskfight_training_prefix(RiskfightState* state,
    RiskfightContext* context, RiskfightOpponent first, RiskfightOpponent second, int ticks) {
    assert(context->self_play);
    const RiskfightOpponent scripts[2] = {first, second};
    for (int tick = 0; tick < ticks; tick++) {
        int actions[2 * RF_HEADS];
        for (int i = 0; i < 2; i++) {
            float observation[RF_OBS_SIZE];
            riskfight_write_observation(state, i, observation);
            riskfight_script(observation, scripts[i], actions + i * RF_HEADS);
        }
        riskfight_step((EncounterState*)state, (EncounterContext*)context, actions);
        if (state->env.episode_over) {
            riskfight_reset((EncounterState*)state, (EncounterContext*)context, 0);
            return RF_START_PREFIX_TERMINAL;
        }
    }
    riskfight_training_clear_returns(state);
    return RF_START_MIDFIGHT;
}

static RiskfightTrainingStart riskfight_training_reset(RiskfightTraining* training,
    RiskfightState* state, RiskfightContext* context, int frozen_policy) {
    riskfight_reset((EncounterState*)state, (EncounterContext*)context, 0);
    training->opponent = RF_TRAIN_LEARNED;
    if (frozen_policy > 0 && riskfight_training_draw(training, training->config.scripted_probability)) {
        const RiskfightOpponent scripts[] = {RISKFIGHT_TACTICIAN, RISKFIGHT_PRESSURE, RISKFIGHT_FLOOR};
        training->opponent = RF_TRAIN_SCRIPTED;
        training->script = scripts[encounter_rand_int(&training->rng, 3)];
    }
    RiskfightTrainingStart start = RF_START_FRESH;
    if (riskfight_training_draw(training, training->config.midfight_probability)) {
        const RiskfightOpponent scripts[] = {RISKFIGHT_TRADER, RISKFIGHT_CAUTIOUS,
            RISKFIGHT_AGGRESSIVE, RISKFIGHT_TACTICIAN, RISKFIGHT_PRESSURE,
            RISKFIGHT_SURVIVAL, RISKFIGHT_FLOOR};
        RiskfightOpponent first = scripts[encounter_rand_int(&training->rng, 7)];
        RiskfightOpponent second = scripts[encounter_rand_int(&training->rng, 7)];
        int ticks = 1 + encounter_rand_int(&training->rng, training->config.midfight_max_ticks);
        start = riskfight_training_prefix(state, context, first, second, ticks);
    }
    training->start_tick = state->env.tick;
    training->start = start;
    training->omissions = 0;
    return start;
}

static int riskfight_training_actions(RiskfightTraining* training,
    const RiskfightState* state, int frozen_policy, int* actions) {
    if (training->opponent != RF_TRAIN_SCRIPTED) return 0;
    assert(frozen_policy > 0);
    float observation[RF_OBS_SIZE];
    riskfight_write_observation(state, 1, observation);
    int omitted = riskfight_training_draw(training, riskfight_training_omission(training));
    if (omitted) memset(actions + RF_HEADS, 0, RF_HEADS * sizeof(int));
    else riskfight_script(observation, training->script, actions + RF_HEADS);
    return omitted;
}
