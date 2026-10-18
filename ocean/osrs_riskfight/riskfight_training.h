#pragma once

typedef struct {
    float scripted_probability, omission_initial;
    uint64_t omission_decay_ticks;
    float midfight_probability;
    int midfight_max_ticks;
    int session_ticks; /* game ticks of one session (episode): fights plus the time between them */
    float curveball_probability;
    int randomize;
    float death_price; /* 0: drawn per fight, log-uniform in [0.4, 2.5] (or from the measured table, price_table); else fixed */
    int price_table; /* death_price 0 only: 1 draws the price of a new opponent from the live table (RF_PRICE_TABLE: the first meetings of the user's recordings) instead of the wide log-uniform */
    int fight_starts; /* 1: a fight starts as one of the user's recorded fights did (riskfight_start.h) instead of after the regear; 0 the regear (121 hitpoints, nothing swung) */
    float rep_start_probability; /* the share of sessions that start with a reputation (RF_REP_START_PROBABILITY unless rep_start_probability is set) */
    RiskfightRepRules rep; /* what a reputation costs (riskfight_session.h): env keys rep_search, rep_rag, rep_half_life, defaulting to RF_REP_SEARCH, RF_REP_RAG, RF_REP_HALF_LIFE_TICKS */
    float pool_probability; /* self-play: the share of net-played opponents that wear a pool kit instead of one of our kits (needs opponent_pool) */
    float same_class; /* kit_same_class: the share of new opponents whose kit is drawn among seat 0's kit class (RiskfightKitClass) instead of without a condition */
    float learner_random_kit; /* the share of sessions in which seat 0 wears a pool kit it can play instead of one of our kits (needs live_loadout 3 or 4) */
} RiskfightTrainingConfig;

typedef enum { RF_TRAIN_LEARNED, RF_TRAIN_SCRIPTED } RiskfightTrainingOpponent;
typedef enum { RF_START_FRESH, RF_START_MIDFIGHT, RF_START_PREFIX_TERMINAL } RiskfightTrainingStart;

/** A kit a seat wears: its loadout, and the pool kit it is (NULL for one of ours). */
typedef struct { const RiskfightLoadout* loadout; const RiskfightKit* kit; } RiskfightTrainingKit;

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
    float rep; /* our reputation in this session (riskfight_session.h): rude teleports, decaying; read at every new opponent and by the observation */
    float his_rep; /* seat 1's, kept the same way from its own rude teleports while a net plays it (self-play): it lengthens the waits as ours does */
    int mirror; /* this opponent is the learning policy itself (pure self-play): both seats drawn alike at price 1 (riskfight_training_draw_opponent) */
    /* Seat 1's kit in the mirror and its ring's draw, drawn with ours at the session's start (by our rule, given our class with kit_same_class) and kept for
     * all its opponents, as ours is kept. A kit drawn per opponent while ours is drawn per session gave the seats different kit shares per fight (a kit that
     * ends fights sooner fills more of ours: 54% Dharok fights for seat 0 against 51% for seat 1 under a policy that orbs at random), and one policy then won
     * more kills from one seat. */
    RiskfightTrainingKit mirror_kit;
    uint32_t mirror_roll;
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
    DictItem* table = dict_find(kwargs, "price_table");
    config.price_table = table ? (int)table->value : 0;
    assert((config.price_table == 0 || config.price_table == 1) && (config.price_table == 0 || config.death_price == 0));
    DictItem* starts = dict_find(kwargs, "fight_starts");
    config.fight_starts = starts ? (int)starts->value : 0;
    assert(config.fight_starts == 0 || config.fight_starts == 1);
    DictItem* randomize = dict_find(kwargs, "domain_randomization");
    config.randomize = randomize ? (int)randomize->value : 0;
    assert(config.randomize == 0 || config.randomize == 1);
    DictItem* rep_start = dict_find(kwargs, "rep_start_probability");
    config.rep_start_probability = rep_start ? (float)rep_start->value : RF_REP_START_PROBABILITY;
    assert(config.rep_start_probability >= 0 && config.rep_start_probability <= 1);
    DictItem* rep_search = dict_find(kwargs, "rep_search");
    DictItem* rep_rag = dict_find(kwargs, "rep_rag");
    DictItem* rep_half_life = dict_find(kwargs, "rep_half_life");
    config.rep = riskfight_rep_rules(rep_search ? (float)rep_search->value : RF_REP_SEARCH, rep_rag ? (float)rep_rag->value : RF_REP_RAG,
        rep_half_life ? (float)rep_half_life->value : (float)RF_REP_HALF_LIFE_TICKS);
    assert(config.rep.search >= 0 && config.rep.rag >= 0 && (!rep_half_life || rep_half_life->value > 0));
    DictItem* pool_share = dict_find(kwargs, "selfplay_pool_probability");
    config.pool_probability = pool_share ? (float)pool_share->value : 0;
    assert(config.pool_probability >= 0 && config.pool_probability <= 1);
    DictItem* same_class = dict_find(kwargs, "kit_same_class");
    config.same_class = same_class ? (float)same_class->value : 0;
    assert(config.same_class >= 0 && config.same_class <= 1);
    DictItem* random_kit = dict_find(kwargs, "learner_random_kit");
    config.learner_random_kit = random_kit ? (float)random_kit->value : 0;
    assert(config.learner_random_kit >= 0 && config.learner_random_kit <= 1);
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

/** One of our kits (context->kit_mix): the recorded live kit, the Voidwaker kit, or the Voidwaker kit with summer pies, evenly (RF_KIT_MIX_VW); with RF_KIT_MIX_DHAROK
 * the Dharok kit half the time and those three a sixth each. */
static const RiskfightLoadout* riskfight_training_draw_kit(RiskfightTraining* training, int kit_mix) {
    static const RiskfightLoadout* const kits[] = {&RISKFIGHT_LIVE_LOADOUT, &RISKFIGHT_VW_LOADOUT, &RISKFIGHT_VW_PIES_LOADOUT};
    assert(kit_mix == RF_KIT_MIX_VW || kit_mix == RF_KIT_MIX_DHAROK);
    if (kit_mix == RF_KIT_MIX_VW) return kits[encounter_rand_int(&training->rng, 3)];
    int draw = encounter_rand_int(&training->rng, 6);
    return draw < 3 ? &RISKFIGHT_DHAROK_LOADOUT : kits[draw - 3];
}

/** Seat 0's kit for a session (context->kit_mix set): with learner_random_kit a pool kit seat 0 can play (riskfight_kit_learner_ok), by its pool weight, else
 * one of our kits (riskfight_training_draw_kit). A net-played seat 1 that wears one of ours draws by the same rule. */
static RiskfightTrainingKit riskfight_training_draw_ours(RiskfightTraining* training, int kit_mix) {
    if (riskfight_training_draw(training, training->config.learner_random_kit)) {
        const RiskfightKit* kit = riskfight_pool_pick_where((float)(xorshift32(&training->rng) >> 8) / 16777216.0f, RF_PICK_LEARNER); /* 24 bits, as the reset's pick */
        return (RiskfightTrainingKit){&RISKFIGHT_POOL_LOADOUT[kit - RISKFIGHT_POOL], kit};
    }
    return (RiskfightTrainingKit){riskfight_training_draw_kit(training, kit_mix), NULL};
}

/** riskfight_training_draw_ours given the class of the kit it returns, by rejection: exact, and it ends because `kit_class` is the class of seat 0's kit,
 * which the same rule drew. */
static RiskfightTrainingKit riskfight_training_draw_ours_of(RiskfightTraining* training, int kit_mix, RiskfightKitClass kit_class) {
    for (;;) {
        RiskfightTrainingKit drawn = riskfight_training_draw_ours(training, kit_mix);
        if (riskfight_loadout_class(drawn.loadout) == kit_class) return drawn;
    }
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
    riskfight_clear_returns(state);
    return RF_START_MIDFIGHT;
}


static int riskfight_dr_pick(RiskfightTraining* training, int count) { return encounter_rand_int(&training->rng, count); }
static uint16_t riskfight_dr_scale(RiskfightTraining* training, uint16_t rate) {
    /* Per-mille rates times a factor in [0.5, 1.5], at most 1000. */
    int scaled = (int)rate * (500 + riskfight_dr_pick(training, 1001)) / 1000;
    return (uint16_t)(scaled > 1000 ? 1000 : scaled);
}

/** Domain randomization of his side after a reset: levels, defender, legs, body, amulet, food count, reaction lag and the
 * per-mille rates of his profile. Gear is varied only for the live loadout, whose slots all have fitted alternatives. Never in the mirror: seat 1 is then
 * the learning policy in our kits, and weakening it alone (levels down to 100, a third of the anglerfish and karambwans gone) made seat 0 win 56% of the
 * kills of one policy against itself. */
static void riskfight_training_randomize(RiskfightTraining* training, RiskfightState* state, const RiskfightContext* context) {
    if (!training->config.randomize || training->mirror) return;
    Player* p = &state->env.players[1];
    p->current_attack = 100 + riskfight_dr_pick(training, 19);
    p->current_strength = 100 + riskfight_dr_pick(training, 19);
    p->current_defence = 90 + riskfight_dr_pick(training, 21);
    /* Hitpoints are not randomized: everyone starts a fight at 121 after the anglerfish overheal at the bank (a pool kit that starts lower, the low-hitpoints Dharok's, keeps it); a ranger's level varies below the boosted 112. */
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
 * env is tagged with. The script and OppFit play pool kits (needs opponent_pool); a frozen net wears a pool kit with selfplay_pool_probability, else a kit
 * drawn as seat 0's is (riskfight_training_draw_ours; with no kit mix, live_loadout's). A pure self-play env (self_play, frozen_policy 0) always gives seat 1 to
 * the learning policy: its rows train policy 0, so a script there would teach the policy actions the env ignored.
 * The kit is conditioned on seat 0's kit class (riskfight_loadout_class): with kit_same_class a pool kit comes from the pool's kits of that class, and a kit
 * drawn as seat 0's is drawn given that class; else without a condition. In Dharok's kit one mostly meets Dharok's kits, rarely a Voidwaker or a Soulreaper
 * kit, and the other way round. A ragger keeps his ranged pool kit. */
static void riskfight_training_draw_opponent(RiskfightTraining* training, RiskfightContext* context, int frozen_policy) {
    training->opponent = RF_TRAIN_LEARNED;
    training->scripted = NULL;
    const int learning_seat = context->self_play && frozen_policy == 0;
    /* A ragger is always the script, in a seat 1 no learning policy plays: he wears a ranged pool kit, so the pool has to be on. */
    context->opp_ragger = !learning_seat && context->opponent_pool &&
        riskfight_training_draw(training, riskfight_rag_probability(&training->config.rep, training->rep));
    if (context->opp_ragger) {
        training->opponent = RF_TRAIN_SCRIPTED;
        training->scripted = riskfight_training_profile(training);
    } else if (frozen_policy > 0 && riskfight_training_draw(training, training->config.scripted_probability)) {
        training->opponent = RF_TRAIN_SCRIPTED;
        training->scripted = riskfight_training_profile(training);
    }
    context->opp_driven = training->opponent == RF_TRAIN_SCRIPTED;
    const int driven = !context->self_play || context->opp_driven;
    /* Pure self-play is symmetric: seat 1 wears the kit drawn with ours at the session's start (riskfight_training_reset), by our rule given our class with
     * kit_same_class, and both fight at price 1 (riskfight_training_new_opponent). P(classes a, b) = p(a) ((1 - s) p(b) + s [a = b]) is symmetric in a and b. */
    training->mirror = learning_seat;
    context->loadout[1] = context->opp_default;
    context->opp_chosen = NULL;
    context->opp_class = RF_CLASS_ANY;
    const RiskfightKitClass ours = riskfight_loadout_class(context->loadout[0] ? context->loadout[0] : &RISKFIGHT_DEFAULT_LOADOUT);
    context->opp_plain = training->mirror || (!driven && !(context->opponent_pool && riskfight_training_draw(training, training->config.pool_probability)));
    if (training->mirror) {
        context->loadout[1] = training->mirror_kit.loadout;
        context->opp_chosen = training->mirror_kit.kit;
        context->opp_roll = training->mirror_roll;
    } else if (context->opp_plain && context->kit_mix != RF_KIT_MIX_NONE) {
        RiskfightTrainingKit his = riskfight_training_draw(training, training->config.same_class) ?
            riskfight_training_draw_ours_of(training, context->kit_mix, ours) : riskfight_training_draw_ours(training, context->kit_mix);
        context->loadout[1] = his.loadout;
        context->opp_chosen = his.kit;
        context->opp_roll = xorshift32(&training->rng);
    } else if (!context->opp_plain && context->opponent_pool && !context->opp_ragger && riskfight_training_draw(training, training->config.same_class))
        context->opp_class = ours;
}

/** A new opponent steps up to a freshly reset fight (riskfight_training_draw_opponent before the reset): his price, who plays him, and his randomized side.
 * A midfight start (a scaffold that is off by default) happens only at the first fight of a session. */
static RiskfightTrainingStart riskfight_training_new_opponent(RiskfightTraining* training, RiskfightState* state,
    RiskfightContext* context, int allow_midfight) {
    state->death_price = training->mirror ? 1.0f : training->config.death_price > 0 ? training->config.death_price :
        training->config.price_table ? RF_PRICE_TABLE[encounter_rand_int(&training->rng, RF_PRICE_TABLE_COUNT)] :
        0.4f * powf(RF_PRICE_MAX_X10 / 4.0f, encounter_rand_float(&training->rng));
    RiskfightTrainingStart start = RF_START_FRESH;
    if (allow_midfight && riskfight_training_draw(training, training->config.midfight_probability)) {
        const RiskfightProfile* first = riskfight_training_profile(training);
        const RiskfightProfile* second = riskfight_training_profile(training);
        int ticks = 1 + encounter_rand_int(&training->rng, training->config.midfight_max_ticks);
        start = riskfight_training_prefix(state, context, first, second, ticks);
    }
    if (training->scripted) state->opponent_profile = training->scripted;
    state->ragger = context->opp_ragger;
    if (state->ragger) state->ragger_limit = RF_RAGGER_MIN_SWINGS + encounter_rand_int(&training->rng, RF_RAGGER_MAX_SWINGS - RF_RAGGER_MIN_SWINGS + 1);
    riskfight_training_randomize(training, state, context);
    return start;
}

/** The start drawn for a fight: the recorded row, and the seat in the user's part. */
typedef struct { const RiskfightStartRecord* row; int us; } RiskfightStartDraw;

/** The start of the fight just reset (fight_starts): one recorded start, drawn from the rematch rows or the first-meeting rows, applied on the kit and opponent drawn for this fight.
 * In a self-play env whose seat 1 is a net the user's part falls to either seat with equal probability (the seats are treated alike); elsewhere it is seat 0. Row NULL: the regear stands. */
static RiskfightStartDraw riskfight_training_start_fight(RiskfightTraining* training, RiskfightState* state, RiskfightContext* context, int rematch) {
    if (!training->config.fight_starts) return (RiskfightStartDraw){NULL, 0};
    const int net = context->self_play && !context->opp_driven;
    const int us = net && riskfight_training_draw(training, 0.5f);
    const RiskfightStartRecord* row = riskfight_start_draw(&training->rng, rematch);
    riskfight_start_apply(state, context, row, us, &training->rng);
    return (RiskfightStartDraw){row, us};
}

/** Both bags of the fight just reset, each permuted uniformly (Fisher-Yates on the training stream): a kit's layout is fixed (food first, then restores, brews,
 * combat), a player's is his own (the user keeps restores in 4 and 8, brews in 13 and 15). The net sees the bag in canonical order (riskfight_bag.h), so the
 * permutation changes nothing it sees: it keeps the scripts honest (they read contents, never positions) and the invariance exercised. */
static void riskfight_training_shuffle_bags(RiskfightTraining* training, RiskfightState* state) {
    for (int seat = 0; seat < 2; seat++) {
        OsrsInventoryCell* cells = state->env.players[seat].inventory_cells;
        for (int slot = OSRS_INVENTORY_SIZE - 1; slot > 0; slot--) {
            int other = encounter_rand_int(&training->rng, slot + 1);
            OsrsInventoryCell held = cells[slot];
            cells[slot] = cells[other];
            cells[other] = held;
        }
    }
}

/** The first fight of a session: our kit (drawn once per session), full supplies on both sides, a new opponent. */
static RiskfightTrainingStart riskfight_training_reset(RiskfightTraining* training,
    RiskfightState* state, RiskfightContext* context, int frozen_policy) {
    if (context->kit_mix != RF_KIT_MIX_NONE) {
        RiskfightTrainingKit ours = riskfight_training_draw_ours(training, context->kit_mix);
        context->loadout[0] = ours.loadout;
        context->our_kit = ours.kit;
        context->our_roll = xorshift32(&training->rng);
    }
    if (context->self_play && frozen_policy == 0) {
        training->mirror_kit = (RiskfightTrainingKit){context->loadout[0], NULL};
        if (context->kit_mix != RF_KIT_MIX_NONE)
            training->mirror_kit = riskfight_training_draw(training, training->config.same_class) ?
                riskfight_training_draw_ours_of(training, context->kit_mix, riskfight_loadout_class(context->loadout[0])) : riskfight_training_draw_ours(training, context->kit_mix);
        training->mirror_roll = xorshift32(&training->rng);
    }
    training->rep = riskfight_training_draw(training, training->config.rep_start_probability) ? RF_REP_START_MAX * (1.0f - encounter_rand_float(&training->rng)) : 0;
    training->his_rep = 0;
    riskfight_training_draw_opponent(training, context, frozen_policy);
    riskfight_reset((EncounterState*)state, (EncounterContext*)context, 0);
    riskfight_training_shuffle_bags(training, state);
    RiskfightTrainingStart start = riskfight_training_new_opponent(training, state, context, 1);
    if (start == RF_START_PREFIX_TERMINAL) riskfight_training_shuffle_bags(training, state); /* the prefix ended in a fresh reset */
    training->start_tick = state->env.tick; /* the ticks of a midfight prefix: the swings of a recorded start are the first tick of the fight */
    if (start != RF_START_MIDFIGHT) (void)riskfight_training_start_fight(training, state, context, 0);
    training->start = start;
    training->omissions = 0;
    return start;
}

/** The next fight of the session after the transition `t`: a regear at the bank for both sides (a fresh reset), the same opponent with his kit, profile and
 * price in a rematch, otherwise a new one steps up (a new kit, a new price, a new player; a ragger with the probability the reputation gives). */
static void riskfight_training_next_fight(RiskfightTraining* training, RiskfightState* state,
    RiskfightContext* context, const RiskfightTransition* t, int frozen_policy) {
    const RiskfightProfile* profile = state->opponent_profile;
    float price = state->death_price;
    int opp_lag = state->opp_lag;
    if (!t->rematch) riskfight_training_draw_opponent(training, context, frozen_policy);
    context->keep_opp_kit = t->rematch; /* a rematch keeps his kit and ring; a new opponent draws a new pool kit */
    riskfight_reset((EncounterState*)state, (EncounterContext*)context, 0);
    riskfight_training_shuffle_bags(training, state);
    context->keep_opp_kit = 0;
    if (t->rematch) {
        state->opponent_profile = profile;
        state->death_price = price;
        state->opp_lag = opp_lag;
    } else riskfight_training_new_opponent(training, state, context, 0);
    (void)riskfight_training_start_fight(training, state, context, t->rematch);
    training->start_tick = 0;
    training->start = RF_START_FRESH;
}

/** Whether the script or OppFit playing seat 1 of a self-play env does nothing this tick: scripted_omission_initial, annealed to 0 over
 * scripted_omission_decay_ticks (steps of the env). Only the scripted share of a self-play env has any. */
static int riskfight_training_omit(RiskfightTraining* training) {
    return training->opponent == RF_TRAIN_SCRIPTED && riskfight_training_draw(training, riskfight_training_omission(training));
}
