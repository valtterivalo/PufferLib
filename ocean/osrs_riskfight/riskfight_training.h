#pragma once

typedef struct {
    float scripted_probability, omission_initial;
    uint64_t omission_decay_ticks;
    float midfight_probability;
    int midfight_max_ticks;
    int session_rounds;
    float curveball_probability;
} RiskfightTrainingConfig;

typedef enum { RF_TRAIN_LEARNED, RF_TRAIN_SCRIPTED } RiskfightTrainingOpponent;
typedef enum { RF_START_FRESH, RF_START_MIDFIGHT, RF_START_PREFIX_TERMINAL } RiskfightTrainingStart;

typedef struct {
    RiskfightTrainingConfig config;
    uint32_t rng;
    uint64_t ticks;
    RiskfightTrainingOpponent opponent;
    RiskfightTrainingStart start;
    int start_tick;
    int omissions;
} RiskfightTraining;

static RiskfightTrainingConfig riskfight_training_config(Dict* kwargs) {
    RiskfightTrainingConfig config = {0};
    const char* names[] = {"scripted_probability", "scripted_omission_initial",
        "scripted_omission_decay_ticks", "midfight_probability", "midfight_max_ticks"};
    double values[5] = {0};
    DictItem* rounds = dict_find(kwargs, "session_rounds");
    assert(rounds && rounds->value >= 1 && rounds->value == floor(rounds->value));
    config.session_rounds = (int)rounds->value;
    DictItem* curveball = dict_find(kwargs, "curveball_probability");
    assert(curveball && curveball->value >= 0 && curveball->value <= 1);
    config.curveball_probability = (float)curveball->value;
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

/** A scripted session opponent: a training human, or a synthetic curveball. */
static const RiskfightProfile* riskfight_training_profile(RiskfightTraining* training) {
    RiskfightOpponent pool = RISKFIGHT_HUMAN;
    if (riskfight_training_draw(training, training->config.curveball_probability))
        pool = encounter_rand_int(&training->rng, 2) ? RISKFIGHT_ESCAPER : RISKFIGHT_ALL_IN;
    return riskfight_pick_profile(pool, xorshift32(&training->rng));
}

static RiskfightTrainingStart riskfight_training_prefix(RiskfightState* state,
    RiskfightContext* context, const RiskfightProfile* first, const RiskfightProfile* second, int ticks) {
    assert(context->self_play);
    const RiskfightProfile* scripts[2] = {first, second};
    for (int tick = 0; tick < ticks; tick++) {
        int actions[2 * RF_HEADS];
        for (int i = 0; i < 2; i++) {
            float observation[RF_OBS_SIZE];
            riskfight_write_observation(state, i, observation);
            riskfight_script(observation, scripts[i], state->script_seed[i], state->consume_ticks[i], actions + i * RF_HEADS);
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
    const RiskfightProfile* scripted = NULL;
    if (frozen_policy > 0 && riskfight_training_draw(training, training->config.scripted_probability)) {
        training->opponent = RF_TRAIN_SCRIPTED;
        scripted = riskfight_training_profile(training);
    }
    RiskfightTrainingStart start = RF_START_FRESH;
    if (riskfight_training_draw(training, training->config.midfight_probability)) {
        const RiskfightProfile* first = riskfight_training_profile(training);
        const RiskfightProfile* second = riskfight_training_profile(training);
        int ticks = 1 + encounter_rand_int(&training->rng, training->config.midfight_max_ticks);
        start = riskfight_training_prefix(state, context, first, second, ticks);
    }
    if (scripted) state->opponent_profile = scripted;
    training->start_tick = state->env.tick;
    training->start = start;
    training->omissions = 0;
    return start;
}

/** Fresh round against the same opponent after an escape: full supplies, new priorities and script seeds. */
static void riskfight_training_next_round(RiskfightTraining* training,
    RiskfightState* state, RiskfightContext* context) {
    const RiskfightProfile* profile = state->opponent_profile;
    riskfight_reset((EncounterState*)state, (EncounterContext*)context, 0);
    state->opponent_profile = profile;
    training->start_tick = 0;
}

static int riskfight_training_actions(RiskfightTraining* training,
    const RiskfightState* state, int frozen_policy, int* actions) {
    if (training->opponent != RF_TRAIN_SCRIPTED) return 0;
    assert(frozen_policy > 0);
    float observation[RF_OBS_SIZE];
    riskfight_write_observation(state, 1, observation);
    int omitted = riskfight_training_draw(training, riskfight_training_omission(training));
    if (omitted) memset(actions + RF_HEADS, 0, RF_HEADS * sizeof(int));
    else riskfight_script(observation, state->opponent_profile, state->script_seed[1], state->consume_ticks[1], actions + RF_HEADS);
    return omitted;
}
