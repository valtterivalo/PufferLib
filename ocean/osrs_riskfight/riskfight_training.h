#pragma once

typedef struct {
    float scripted_probability, omission_initial;
    uint64_t omission_decay_ticks;
    float midfight_probability;
    int midfight_max_ticks;
    int session_ticks; /* game ticks of one session (episode): fights plus the time between them */
    float curveball_probability;
    int randomize;
    float death_price; /* 0: drawn per fight, log-uniform in [0.4, 2.5]; else fixed */
    float pool_probability; /* self-play: the share of net-played opponents that wear a pool kit instead of one of our kits (needs opponent_pool) */
} RiskfightTrainingConfig;

typedef enum { RF_TRAIN_LEARNED, RF_TRAIN_SCRIPTED } RiskfightTrainingOpponent;
typedef enum { RF_START_FRESH, RF_START_MIDFIGHT, RF_START_PREFIX_TERMINAL } RiskfightTrainingStart;

typedef struct {
    RiskfightTrainingConfig config;
    RiskfightProfile profile;
    uint32_t rng;
    uint64_t ticks;
    RiskfightTrainingOpponent opponent;
    const RiskfightProfile* scripted; /* the profile of a scripted opponent, drawn with the opponent and put on him after the reset */
    RiskfightTrainingStart start;
    int start_tick;
    int omissions;
} RiskfightTraining;

static RiskfightTrainingConfig riskfight_training_config(Dict* kwargs) {
    RiskfightTrainingConfig config = {0};
    const char* names[] = {"scripted_probability", "scripted_omission_initial",
        "scripted_omission_decay_ticks", "midfight_probability", "midfight_max_ticks"};
    double values[5] = {0};
    DictItem* ticks = dict_find(kwargs, "session_ticks");
    assert(ticks && ticks->value >= 1 && ticks->value <= INT_MAX && ticks->value == floor(ticks->value));
    config.session_ticks = (int)ticks->value;
    DictItem* curveball = dict_find(kwargs, "curveball_probability");
    assert(curveball && curveball->value >= 0 && curveball->value <= 1);
    config.curveball_probability = (float)curveball->value;
    DictItem* price = dict_find(kwargs, "death_price");
    config.death_price = price ? (float)price->value : 1.0f;
    assert(config.death_price >= 0 && config.death_price <= RF_PRICE_MAX_X10 / 10.0f);
    DictItem* randomize = dict_find(kwargs, "domain_randomization");
    config.randomize = randomize ? (int)randomize->value : 0;
    assert(config.randomize == 0 || config.randomize == 1);
    DictItem* pool_share = dict_find(kwargs, "selfplay_pool_probability");
    config.pool_probability = pool_share ? (float)pool_share->value : 0;
    assert(config.pool_probability >= 0 && config.pool_probability <= 1);
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
    /* Both sides are scripts here, whoever will play him afterwards. */
    const int driven = context->opp_driven;
    void (*const hook)(void*, const void*, int*) = context->opp_hook;
    context->opp_driven = 0;
    context->opp_hook = NULL;
    const RiskfightProfile* scripts[2] = {first, second};
    for (int tick = 0; tick < ticks; tick++) {
        int actions[2 * RF_HEADS];
        for (int i = 0; i < 2; i++) {
            float observation[RF_OBS_SIZE];
            riskfight_write_script_observation(state, i, observation);
            riskfight_script(observation, scripts[i], state->script_seed[i], state->consume_ticks[i], actions + i * RF_HEADS);
        }
        riskfight_step((EncounterState*)state, (EncounterContext*)context, actions);
        if (state->env.episode_over) {
            context->opp_driven = driven;
            context->opp_hook = hook;
            context->keep_opp_kit = 1; /* the opponent of the prefix is the one that steps up */
            riskfight_reset((EncounterState*)state, (EncounterContext*)context, 0);
            context->keep_opp_kit = 0;
            return RF_START_PREFIX_TERMINAL;
        }
    }
    context->opp_driven = driven;
    context->opp_hook = hook;
    riskfight_training_clear_returns(state);
    return RF_START_MIDFIGHT;
}


static int riskfight_dr_pick(RiskfightTraining* training, int count) { return encounter_rand_int(&training->rng, count); }
static uint16_t riskfight_dr_scale(RiskfightTraining* training, uint16_t rate) {
    /* Per-mille rates times a factor in [0.5, 1.5], at most 1000. */
    int scaled = (int)rate * (500 + riskfight_dr_pick(training, 1001)) / 1000;
    return (uint16_t)(scaled > 1000 ? 1000 : scaled);
}

/** Domain randomization of his side after a reset: levels, hitpoints, defender, legs, body, amulet, food count, reaction lag and the
 * per-mille rates of his profile. Gear is varied only for the live loadout, whose slots all have fitted alternatives. */
static void riskfight_training_randomize(RiskfightTraining* training, RiskfightState* state, const RiskfightContext* context) {
    if (!training->config.randomize) return;
    Player* p = &state->env.players[1];
    p->current_attack = 100 + riskfight_dr_pick(training, 19);
    p->current_strength = 100 + riskfight_dr_pick(training, 19);
    p->current_defence = 90 + riskfight_dr_pick(training, 21);
    /* A pool kit keeps the hitpoints it starts with when they are below the account's 99 (the low-hitpoints Dharok's); a ranger's level varies below the boosted 112. */
    if (!context->opp_kit || context->opp_kit->hitpoints >= 99) p->current_hitpoints = 99 + riskfight_dr_pick(training, 23);
    if (context->opp_kit && context->opp_kit->ranged_level) p->current_ranged = 100 + riskfight_dr_pick(training, 13);
    if (context->loadout[1] == &RISKFIGHT_LIVE_LOADOUT) {
        static const uint8_t shields[] = {ITEM_AVERNIC_DEFENDER, ITEM_DRAGON_DEFENDER, ITEM_RUNE_DEFENDER};
        static const uint8_t legs[] = {ITEM_DRAGON_PLATELEGS, ITEM_RUNE_PLATELEGS};
        static const uint8_t bodies[] = {ITEM_FIGHTER_TORSO, ITEM_BANDOS_CHESTPLATE};
        static const uint8_t necks[] = {ITEM_AMULET_OF_STRENGTH, ITEM_AMULET_OF_TORTURE, ITEM_FURY};
        p->equipped[GEAR_SLOT_SHIELD] = shields[riskfight_dr_pick(training, 3)];
        p->equipped[GEAR_SLOT_LEGS] = legs[riskfight_dr_pick(training, 2)];
        p->equipped[GEAR_SLOT_BODY] = bodies[riskfight_dr_pick(training, 2)];
        p->equipped[GEAR_SLOT_NECK] = necks[riskfight_dr_pick(training, 3)];
        osrs_refresh_player_equipment(p);
        pvp_refresh_visible_gear(p);
    }
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        OsrsConsumableKind kind = (OsrsConsumableKind)osrs_inventory_cell_metadata(&p->inventory_cells[slot])->consumable_kind;
        if ((kind == OSRS_CONSUMABLE_ANGLERFISH || kind == OSRS_CONSUMABLE_KARAMBWAN) && riskfight_dr_pick(training, 100) < 35)
            p->inventory_cells[slot] = osrs_inventory_cell_from_content_code(0);
    }
    state->opp_lag = riskfight_dr_pick(training, 4);
    RiskfightProfile* profile = &training->profile;
    *profile = *state->opponent_profile;
    for (int i = 0; i < RISKFIGHT_CONSUME_BANDS; i++) profile->consume_pm[i] = riskfight_dr_scale(training, profile->consume_pm[i]);
    profile->vw_pm = riskfight_dr_scale(training, profile->vw_pm);
    profile->maul_pm = riskfight_dr_scale(training, profile->maul_pm);
    profile->veng_pm = riskfight_dr_scale(training, profile->veng_pm);
    for (int i = 0; i < RISKFIGHT_AXE_BANDS; i++) profile->axe_pm[i] = riskfight_dr_scale(training, profile->axe_pm[i]);
    state->opponent_profile = profile;
}

/** Who plays the next new opponent and what he wears, drawn before his reset. He is played by the script or OppFit (the lone opponent of a self_play 0 env;
 * in a self-play env with a frozen policy, with probability scripted_probability) or by a net in seat 1: the learning policy, or the frozen checkpoint the
 * env is tagged with. The script and OppFit play pool kits (needs opponent_pool); a net wears a pool kit with selfplay_pool_probability, else one of our
 * kits, drawn as ours are (live_loadout 3). */
static void riskfight_training_draw_opponent(RiskfightTraining* training, RiskfightContext* context, int frozen_policy) {
    training->opponent = RF_TRAIN_LEARNED;
    training->scripted = NULL;
    if (frozen_policy > 0 && riskfight_training_draw(training, training->config.scripted_probability)) {
        training->opponent = RF_TRAIN_SCRIPTED;
        training->scripted = riskfight_training_profile(training);
    }
    context->opp_driven = training->opponent == RF_TRAIN_SCRIPTED;
    const int driven = !context->self_play || context->opp_driven;
    context->loadout[1] = context->opp_default;
    context->opp_plain = 0;
    if (!driven) {
        context->opp_plain = !riskfight_training_draw(training, training->config.pool_probability);
        if (context->kit_mix) context->loadout[1] = encounter_rand_int(&training->rng, 2) ? &RISKFIGHT_VW_LOADOUT : &RISKFIGHT_LIVE_LOADOUT;
    }
}

/** A new opponent steps up to a freshly reset fight (riskfight_training_draw_opponent before the reset): his price, who plays him, and his randomized side.
 * A midfight start (a scaffold that is off by default) happens only at the first fight of a session. */
static RiskfightTrainingStart riskfight_training_new_opponent(RiskfightTraining* training, RiskfightState* state,
    RiskfightContext* context, int allow_midfight) {
    state->death_price = training->config.death_price > 0 ? training->config.death_price :
        0.4f * powf(RF_PRICE_MAX_X10 / 4.0f, encounter_rand_float(&training->rng));
    RiskfightTrainingStart start = RF_START_FRESH;
    if (allow_midfight && riskfight_training_draw(training, training->config.midfight_probability)) {
        const RiskfightProfile* first = riskfight_training_profile(training);
        const RiskfightProfile* second = riskfight_training_profile(training);
        int ticks = 1 + encounter_rand_int(&training->rng, training->config.midfight_max_ticks);
        start = riskfight_training_prefix(state, context, first, second, ticks);
    }
    if (training->scripted) state->opponent_profile = training->scripted;
    riskfight_training_randomize(training, state, context);
    return start;
}

/** The first fight of a session: our kit (drawn once per session), full supplies on both sides, a new opponent. */
static RiskfightTrainingStart riskfight_training_reset(RiskfightTraining* training,
    RiskfightState* state, RiskfightContext* context, int frozen_policy) {
    if (context->kit_mix) context->loadout[0] = encounter_rand_int(&training->rng, 2) ? &RISKFIGHT_VW_LOADOUT : &RISKFIGHT_LIVE_LOADOUT;
    riskfight_training_draw_opponent(training, context, frozen_policy);
    riskfight_reset((EncounterState*)state, (EncounterContext*)context, 0);
    RiskfightTrainingStart start = riskfight_training_new_opponent(training, state, context, 1);
    training->start_tick = state->env.tick;
    training->start = start;
    training->omissions = 0;
    return start;
}

/** The next fight of the session after the transition `t`: our supplies persist unless we died or teleported; the opponent persists, with his kit and
 * supplies, only in a rematch, otherwise a new one steps up (a new kit, a new price, a new player). */
static void riskfight_training_next_fight(RiskfightTraining* training, RiskfightState* state,
    RiskfightContext* context, const RiskfightTransition* t, int frozen_policy) {
    RiskfightCarry carry[2];
    for (int side = 0; side < 2; side++) riskfight_carry_save(state, side, &carry[side]);
    const RiskfightProfile* profile = state->opponent_profile;
    float price = state->death_price;
    int opp_lag = state->opp_lag;
    if (!t->rematch) riskfight_training_draw_opponent(training, context, frozen_policy);
    context->keep_opp_kit = t->rematch; /* a rematch keeps his kit and ring; a new opponent draws a new pool kit */
    riskfight_reset((EncounterState*)state, (EncounterContext*)context, 0);
    context->keep_opp_kit = 0;
    riskfight_session_begin_fight(state, carry, t);
    if (t->rematch) {
        state->opponent_profile = profile;
        state->death_price = price;
        state->opp_lag = opp_lag;
    } else riskfight_training_new_opponent(training, state, context, 0);
    training->start_tick = 0;
    training->start = RF_START_FRESH;
}

/** Whether the script or OppFit playing seat 1 of a self-play env does nothing this tick: scripted_omission_initial, annealed to 0 over
 * scripted_omission_decay_ticks (steps of the env). Only the scripted share of a self-play env has any. */
static int riskfight_training_omit(RiskfightTraining* training) {
    return training->opponent == RF_TRAIN_SCRIPTED && riskfight_training_draw(training, riskfight_training_omission(training));
}
