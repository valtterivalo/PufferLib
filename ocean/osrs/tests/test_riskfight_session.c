/* Session contracts: every fight starts after a regear, one regear delay follows every end (a rematch the measured M, anything else R scaled by the reputation), a
 * rude teleport loses the opponent and costs reputation, a ragger rags and leaves, rewards stay in [-1, 1] at the largest price, and a session of N ticks ends. Self-play env, both agents driven by the test (agent 1 idles), so every outcome is forced. */
#include "../../osrs_riskfight/osrs_riskfight.h"
#include <assert.h>
#include <stdio.h>

static Env* env;
static float observations[2][RF_OBS_SIZE];
static float actions[2][RF_HEADS];
static float rewards[2], terminals[2], resets[2];
static unsigned char masks[2][RF_MASK_SIZE];

static Env* make_tagged_env(int session_ticks, double price, unsigned rng, int pool, int tag) {
    DictItem items[] = {{.key = "opponent_type", .value = 0}, {.key = "self_play", .value = 1},
        {.key = "session_ticks", .value = session_ticks}, {.key = "curveball_probability", .value = 0},
        {.key = "death_price", .value = price}, {.key = "opponent_pool", .value = pool}};
    Dict kwargs = {.items = items, .size = sizeof(items) / sizeof(*items)};
    Env* e = (Env*)calloc(1, sizeof(*e));
    e->rng = rng;
    puf_init(e, &kwargs);
    e->tag = tag;
    for (int i = 0; i < 2; i++) e->agents[i] = (Agent){
        .observations = observations[i], .actions = actions[i], .rewards = &rewards[i],
        .terminals = &terminals[i], .resets = &resets[i], .action_mask = masks[i], .policy = i};
    puf_reset(e);
    e->training.rep = 0; /* a session that starts with a reputation is tested below */
    riskfight_native_observe(e);
    memset(actions, 0, sizeof(actions));
    return e;
}
static Env* make_pool_env(int session_ticks, double price, unsigned rng, int pool) { return make_tagged_env(session_ticks, price, rng, pool, 0); }
static Env* make_env(int session_ticks, double price, unsigned rng) { return make_pool_env(session_ticks, price, rng, 0); }
/* Seat 1 a frozen policy (tag 1): the drawn or configured price holds; pure self-play (tag 0) fights at price 1 (test_riskfight_mix). */
static Env* make_frozen_env(int session_ticks, double price, unsigned rng) { return make_tagged_env(session_ticks, price, rng, 0, 1); }

static void destroy(void) { puf_close(env); free(env); env = NULL; }

/** Steps through the wait that follows a fight and returns its length: every tick is a step of no choice, the observation says so (flag and ticks left), the
 * mask allows only the no-op, the reward is 0 and the clock advances one tick. The next fight is on the step the wait ends. */
static int wait_out(void) {
    int ticks = 0;
    while (env->wait_left > 0) {
        int left = env->wait_left;
        for (int seat = 0; seat < 2; seat++) {
            const float* block = observations[seat] + RF_SESSION_START;
            assert(block[2] == 1 && fabsf(block[3] - (left >= RF_WAIT_SCALE_TICKS ? 1.0f : (float)left / RF_WAIT_SCALE_TICKS)) < 1e-6f);
            int offset = 0;
            for (int head = 0; head < RF_HEADS; head++) {
                for (int a = 0; a < RF_ACTION_DIMS[head]; a++) assert(masks[seat][offset + a] == (a == 0));
                offset += RF_ACTION_DIMS[head];
            }
        }
        int before = env->session_elapsed;
        puf_step(env);
        assert(rewards[0] == 0 && rewards[1] == 0);
        ticks++;
        if (terminals[0]) break; /* the budget ran out in the wait: the session was reset */
        assert(env->session_elapsed == before + 1);
    }
    return ticks;
}

static int delay_min(const RiskfightDelay* d) { return d->ticks[0]; }
static int delay_max(const RiskfightDelay* d) { return d->ticks[RF_DELAY_DECILES - 1]; }
static RiskfightRepRules default_rules(void) { return riskfight_rep_rules(RF_REP_SEARCH, RF_REP_RAG, RF_REP_HALF_LIFE_TICKS); }
static int count_food(const Player* p) {
    int n = 0;
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++)
        n += osrs_inventory_cell_metadata(&p->inventory_cells[slot])->click_action == OSRS_CLICK_EAT;
    return n;
}
static void empty_slots(Player* p, int from, int count) {
    for (int slot = from; slot < from + count; slot++) p->inventory_cells[slot] = osrs_inventory_cell_from_content_code(0);
}

/* A kill: he dies on the next tick; the next fight is a regear for both, after R. */
static void test_every_fight_starts_regeared(void) {
    env = make_env(6000, 1, 7);
    Player* us = &env->state.env.players[0];
    int full_food = count_food(us);
    empty_slots(us, 4, 3);
    assert(count_food(us) < full_food);
    us->current_hitpoints = 99;
    us->special_energy = 40;
    us->current_attack = 113;
    env->state.inventory_use[0].vengeance_sacks = 5;
    env->state.env.players[1].current_hitpoints = 0;
    int before = env->session_elapsed;
    puf_step(env);
    assert(terminals[0] == 0 && terminals[1] == 0);
    assert(env->log.kills == 1 && env->log.n == 0 && env->session_fights == 1);
    assert(env->wait_left > 0 && env->state.env.episode_over); /* the wait: the fight's last state stays, the clock runs */
    int delay = wait_out();
    assert(env->session_elapsed - before == 1 + delay);
    assert(delay >= delay_min(&RF_DELAY_RESTOCK) && delay <= delay_max(&RF_DELAY_RESTOCK));
    assert(env->state.env.tick == 0 && !env->state.env.episode_over);
    for (int side = 0; side < 2; side++) {
        const Player* p = &env->state.env.players[side];
        assert(count_food(p) == full_food && p->current_hitpoints == 121 && p->special_energy == 100 && p->current_attack > 99);
        assert(env->state.inventory_use[side].vengeance_sacks > 5);
    }
    assert(env->state.session_ticks_left == 6000 - env->session_elapsed);
    destroy();
}

/* A death: the price is paid, the same regear and the same delay as after a kill. */
static void test_death_is_a_regear_like_any_end(void) {
    env = make_env(6000, 1, 9);
    Player* us = &env->state.env.players[0];
    int full_food = count_food(us);
    empty_slots(us, 4, 6);
    us->special_energy = 10;
    us->current_hitpoints = 0;
    int before = env->session_elapsed;
    puf_step(env);
    assert(terminals[0] == 0 && env->log.deaths == 1);
    int delay = wait_out();
    assert(env->session_elapsed - before == 1 + delay);
    assert(delay >= delay_min(&RF_DELAY_RESTOCK) && delay <= delay_max(&RF_DELAY_RESTOCK));
    us = &env->state.env.players[0];
    assert(count_food(us) == full_food && us->special_energy == 100 && us->current_hitpoints == RISKFIGHT_DEFAULT_LOADOUT.hitpoints);
    destroy();
}

/* The delay a teleport costs, drawn exactly as the env draws it. */
static int expected_teleport_delay(uint32_t rng, int* rematch) {
    RiskfightState tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.outcome[0] = RISKFIGHT_ESCAPE;
    tmp.escaped[0] = 1;
    const RiskfightRepRules rules = default_rules();
    RiskfightTransition t = riskfight_session_transition(&tmp, &rng, 0, &rules);
    *rematch = t.rematch;
    return t.delay;
}

static void test_teleport_costs_a_regear(void) {
    env = make_env(6000, 1, 11);
    Player* us = &env->state.env.players[0];
    int full_food = count_food(us);
    empty_slots(us, 0, 12);
    us->current_hitpoints = 40;
    us->special_energy = 20;
    env->state.env.players[1].special_energy = 20; /* both spent: a kosher teleport, which may be followed by a rematch like the one expected_teleport_delay draws */
    int rematch;
    int expected = expected_teleport_delay(env->training.rng, &rematch);
    int before = env->session_elapsed;
    actions[0][RF_PRIMARY] = RF_TELEPORT;
    puf_step(env);
    assert(terminals[0] == 0 && env->log.escapes == 1 && env->log.self_teleports == 1);
    assert(rewards[0] == 0);
    /* The wait is the restock delay in steps: 1 + expected ticks of the session, one step each, all of them without reward. */
    assert(env->wait_left == expected);
    assert(wait_out() == expected);
    assert(env->session_elapsed - before == 1 + expected);
    assert(expected >= (rematch ? delay_min(&RF_DELAY_REMATCH) : delay_min(&RF_DELAY_RESTOCK)));
    assert(expected <= (rematch ? delay_max(&RF_DELAY_REMATCH) : delay_max(&RF_DELAY_RESTOCK)));
    us = &env->state.env.players[0];
    assert(count_food(us) == full_food && us->special_energy == 100 && us->current_hitpoints == RISKFIGHT_DEFAULT_LOADOUT.hitpoints);
    assert(env->state.env.tick == 0 && !env->state.escaped[0]);
    destroy();
}

/* Many teleports: after a rude one (not kosher, riskfight_norms.h: here a special of either side still unspent) the opponent never returns; after others he does, the
 * same player with the same kit and profile, regeared. The kosher cases are tested in full in test_riskfight_norms.c. */
static void teleports(int our_energy, int his_energy, int he_teleports, int trials, int* rematches, int* rudes) {
    *rematches = *rudes = 0;
    for (int trial = 0; trial < trials; trial++) {
        env = make_env(6000, 1, 100 + trial);
        const RiskfightProfile* his_profile = env->state.opponent_profile;
        env->state.env.players[1].current_hitpoints = 77;
        env->state.env.players[1].special_energy = his_energy;
        env->state.env.players[0].special_energy = our_energy;
        actions[0][RF_PRIMARY] = RF_TELEPORT;
        actions[1][RF_PRIMARY] = he_teleports ? RF_TELEPORT : 0;
        puf_step(env);
        wait_out();
        assert(env->log.self_teleports == 1);
        *rudes += env->log.rude_teleports == 1;
        *rematches += env->log.rematches == 1;
        /* The opponent who is back is the one who was left, regeared like us. */
        const Player* him = &env->state.env.players[1];
        if (env->log.rematches == 1) assert(env->state.opponent_profile == his_profile);
        assert(him->current_hitpoints == 121 && him->special_energy == 100);
        destroy();
    }
}

static void test_rude_teleport_loses_the_opponent(void) {
    int rematches, rudes;
    teleports(0, 100, 0, 300, &rematches, &rudes); /* our special spent, his not: rude */
    assert(rudes == 300 && rematches == 0);
    teleports(100, 100, 0, 300, &rematches, &rudes); /* nobody has specced: rude too */
    assert(rudes == 300 && rematches == 0);
    teleports(100, 0, 0, 300, &rematches, &rudes); /* his spent, ours not: rude */
    assert(rudes == 300 && rematches == 0);
    teleports(0, 0, 0, 300, &rematches, &rudes);   /* both spent: kosher */
    assert(rudes == 0 && rematches > 0);
    int polite = rematches;
    assert(polite > 300 * RF_REMATCH_PROBABILITY / 2 && polite < 300 * (1 + RF_REMATCH_PROBABILITY) / 2 + 1);
    teleports(0, 100, 1, 300, &rematches, &rudes); /* both teleport, ours rude: no rematch */
    assert(rudes == 300 && rematches == 0);
    /* The rule at its source. */
    RiskfightState s;
    memset(&s, 0, sizeof(s));
    s.outcome[0] = RISKFIGHT_ESCAPE;
    s.escaped[0] = 1;
    for (int side = 0; side < 2; side++) {
        s.escape_supplies[0][side].special_energy = 100;
        s.escape_supplies[0][side].minimum_spec_cost = 50;
        s.escape_supplies[0][side].hard_food = s.escape_supplies[0][side].restore_doses = s.escape_supplies[0][side].boost_doses = 1;
    }
    assert(riskfight_rude_teleport(&s));              /* nobody has specced */
    s.escape_supplies[0][0].special_energy = 49;
    assert(riskfight_rude_teleport(&s));              /* ours spent, his could still spec */
    s.escape_supplies[0][1].special_energy = 49;
    assert(!riskfight_rude_teleport(&s));             /* both spent */
    s.escape_supplies[0][1].special_energy = 100;
    s.escape_supplies[0][0].special_energy = 100;
    s.escape_supplies[0][0].hard_food = 0;
    assert(!riskfight_rude_teleport(&s));             /* out of hard food */
    s.escaped[0] = 0;
    s.escaped[1] = 1; /* his teleport is not ours */
    assert(!riskfight_rude_teleport(&s));
    s.escape_supplies[0][0].hard_food = 1; /* stocked again, specials unspent: rude */
    uint32_t rng = 1;
    const RiskfightRepRules rules = default_rules();
    for (int i = 0; i < 200; i++) {
        s.escaped[0] = 1; s.escaped[1] = 0;
        assert(!riskfight_session_transition(&s, &rng, 0, &rules).rematch);
    }
}

static int near_share(double count, double n, double p) { return fabs(count - n * p) <= 4.5 * sqrt(n * p * (1 - p)) + 1; }

/* One regear delay for every end of a fight: a rematch takes the measured M, anything else the measured R times (1 + RF_REP_SEARCH * rep). */
static void test_one_regear_delay_and_reputation(void) {
    const RiskfightOutcome ends[] = {RISKFIGHT_KILL, RISKFIGHT_DEATH, RISKFIGHT_MUTUAL_DEATH, RISKFIGHT_TIMEOUT, RISKFIGHT_RAGGED};
    const float reps[] = {0, 1, 3};
    const RiskfightRepRules rules = default_rules();
    for (size_t e = 0; e < sizeof(ends) / sizeof(*ends); e++)
        for (size_t r = 0; r < 3; r++) {
            RiskfightState s;
            memset(&s, 0, sizeof(s));
            s.outcome[0] = ends[e];
            for (int trial = 0; trial < 50; trial++) {
                uint32_t a = 5 + 1000 * (uint32_t)trial + (uint32_t)e, b = a;
                RiskfightTransition plain = riskfight_session_transition(&s, &a, 0, &rules), scaled = riskfight_session_transition(&s, &b, reps[r], &rules);
                assert(!plain.rematch && !scaled.rematch && !plain.rude);
                assert(plain.delay >= delay_min(&RF_DELAY_RESTOCK) && plain.delay <= delay_max(&RF_DELAY_RESTOCK));
                assert(scaled.delay == (int)(plain.delay * (1.0f + RF_REP_SEARCH * reps[r]) + 0.5f));
            }
        }
    /* A polite teleport: a rematch (M, unscaled) with the measured probability, else a new opponent after R scaled; a ragger is never rematched. */
    RiskfightState s;
    memset(&s, 0, sizeof(s));
    s.outcome[0] = RISKFIGHT_ESCAPE;
    s.escaped[0] = 1;
    uint32_t rng = 11;
    int rematches = 0, trials = 2000;
    for (int i = 0; i < trials; i++) {
        RiskfightTransition t = riskfight_session_transition(&s, &rng, 3, &rules);
        rematches += t.rematch;
        if (t.rematch) assert(t.delay >= delay_min(&RF_DELAY_REMATCH) && t.delay <= delay_max(&RF_DELAY_REMATCH));
        else assert(t.delay >= (int)(delay_min(&RF_DELAY_RESTOCK) * (1.0f + RF_REP_SEARCH * 3) + 0.5f) && t.delay <= (int)(delay_max(&RF_DELAY_RESTOCK) * (1.0f + RF_REP_SEARCH * 3) + 0.5f));
    }
    assert(near_share(rematches, trials, RF_REMATCH_PROBABILITY));
    s.ragger = 1;
    for (int i = 0; i < 200; i++) assert(!riskfight_session_transition(&s, &rng, 0, &rules).rematch);
    /* The ragging probability: nothing without a reputation, RF_REP_RAG per point, capped. */
    assert(riskfight_rag_probability(&rules, 0) == 0 && riskfight_rag_probability(&rules, 1) == RF_REP_RAG && riskfight_rag_probability(&rules, 100) == RF_REP_RAG_CAP);
    assert(riskfight_outcome_reward(RISKFIGHT_RAGGED, 2.5f) == 0 && riskfight_outcome_reward(RISKFIGHT_TIMEOUT, 2.5f) == 0);
}

/* The env keys rep_search, rep_rag and rep_half_life set the session's reputation rules; without them the rules are the defaults. */
static void test_rep_keys_reach_session_rules(void) {
    env = make_env(6000, 1, 84);
    const RiskfightRepRules defaults = default_rules();
    assert(memcmp(&env->training.config.rep, &defaults, sizeof(defaults)) == 0);
    destroy();
    DictItem items[] = {{.key = "opponent_type", .value = 0}, {.key = "self_play", .value = 1},
        {.key = "session_ticks", .value = 6000}, {.key = "curveball_probability", .value = 0}, {.key = "death_price", .value = 1},
        {.key = "rep_search", .value = 2.0}, {.key = "rep_rag", .value = 0.3}, {.key = "rep_half_life", .value = 6000}};
    Dict kwargs = {.items = items, .size = sizeof(items) / sizeof(*items)};
    env = (Env*)calloc(1, sizeof(*env));
    env->rng = 85;
    puf_init(env, &kwargs);
    for (int i = 0; i < 2; i++) env->agents[i] = (Agent){
        .observations = observations[i], .actions = actions[i], .rewards = &rewards[i],
        .terminals = &terminals[i], .resets = &resets[i], .action_mask = masks[i], .policy = i};
    puf_reset(env);
    const RiskfightRepRules* rules = &env->training.config.rep;
    assert(rules->search == 2.0f && rules->rag == 0.3f);
    assert(riskfight_rag_probability(rules, 1) == 0.3f && riskfight_rag_probability(rules, 3) == RF_REP_RAG_CAP);
    float rep = 2;
    for (int tick = 0; tick < 6000; tick++) rep = riskfight_rep_decay(rules, rep);
    assert(fabsf(rep - 1) < 2e-3f);
    /* The env decays its reputation by these rules every step, and prices the search for a new opponent with them. */
    env->training.rep = 1;
    memset(actions, 0, sizeof(actions));
    puf_step(env);
    assert(env->training.rep == rules->decay);
    RiskfightState s;
    memset(&s, 0, sizeof(s));
    s.outcome[0] = RISKFIGHT_KILL;
    uint32_t a = 9, b = 9;
    RiskfightTransition plain = riskfight_session_transition(&s, &a, 0, rules), scaled = riskfight_session_transition(&s, &b, 1, rules);
    assert(scaled.delay == (int)(plain.delay * 3.0f + 0.5f));
    destroy();
}

/* Reputation: a session starts with none in most cases and up to RF_REP_START_MAX otherwise; our rude teleports add 1 each, polite ones nothing; it halves over
 * RF_REP_HALF_LIFE_TICKS; the policy sees it in the last float of the session block. */
static void test_rude_teleports_build_reputation(void) {
    env = make_env(6000, 1, 81);
    int started = 0, sessions = 3000;
    float most = 0;
    for (int i = 0; i < sessions; i++) {
        puf_reset(env);
        assert(env->training.rep >= 0 && env->training.rep <= RF_REP_START_MAX);
        started += env->training.rep > 0;
        if (env->training.rep > most) most = env->training.rep;
        assert(observations[0][RF_SESSION_START + 4] == env->training.rep / RF_REP_OBS_SCALE);
    }
    assert(near_share(started, sessions, RF_REP_START_PROBABILITY) && most > 2.5f);
    destroy();
    /* A kosher teleport leaves it as it is, a rude one (nobody has specced) adds 1, to the reputation of the seat that left: each seat sees its own. */
    for (int seat = 0; seat < 2; seat++)
        for (int rude = 0; rude < 2; rude++) {
            env = make_env(6000, 1, 82);
            assert(env->context.self_play && !env->context.opp_driven); /* a net in seat 1 keeps its own reputation */
            env->state.env.players[0].special_energy = rude ? 100 : 0; /* rude: nobody has specced; polite: both have */
            env->state.env.players[1].special_energy = rude ? 100 : 0;
            memset(actions, 0, sizeof(actions));
            actions[seat][RF_PRIMARY] = RF_TELEPORT;
            puf_step(env);
            const float own = seat ? env->training.his_rep : env->training.rep, other = seat ? env->training.rep : env->training.his_rep;
            assert(seat || env->log.rude_teleports == rude);
            assert(fabsf(own - (float)rude) < 2e-3f && other == 0);
            assert(fabsf(observations[seat][RF_SESSION_START + 4] - rude / RF_REP_OBS_SCALE) < 1e-3f);
            assert(observations[1 - seat][RF_SESSION_START + 4] == 0);
            destroy();
        }
    float rep = 2;
    const RiskfightRepRules rules = default_rules();
    for (int tick = 0; tick < RF_REP_HALF_LIFE_TICKS; tick++) rep = riskfight_rep_decay(&rules, rep);
    assert(fabsf(rep - 1) < 2e-3f);
    /* The observation saturates at RF_REP_OBS_SCALE, each seat on its own reputation. */
    env = make_env(6000, 1, 83);
    env->training.rep = 10;
    env->training.his_rep = 0;
    riskfight_native_observe(env);
    assert(observations[0][RF_SESSION_START + 4] == 1 && observations[1][RF_SESSION_START + 4] == 0);
    env->training.his_rep = 10;
    riskfight_native_observe(env);
    assert(observations[0][RF_SESSION_START + 4] == 1 && observations[1][RF_SESSION_START + 4] == 1);
    destroy();
}

/* A ragger: with the reputation's probability a new opponent is a ranged pool kit played by the script who swings a few times and teleports away. The fight is
 * RISKFIGHT_RAGGED: no stake, no rematch, and the wait is the regear R scaled by the reputation. A ragger only steps into a seat 1 no learning policy plays: the
 * env is tagged with a frozen policy (test_riskfight_mix has the pure self-play env, where he never comes). */
static void test_ragger_rags_and_leaves(void) {
    env = make_pool_env(6000, 1, 71, 1);
    env->tag = 1;
    env->training.rep = 10; /* the cap: RF_REP_RAG_CAP */
    int n = 2000, ragged = 0;
    for (int i = 0; i < n; i++) {
        RiskfightTransition t = {.delay = 100};
        riskfight_training_next_fight(&env->training, &env->state, &env->context, &t, env->tag);
        ragged += env->state.ragger;
        if (!env->state.ragger) continue;
        assert(env->context.opp_driven && env->context.opp_kit);
        assert(get_item_attack_style(env->state.env.players[1].equipped[GEAR_SLOT_WEAPON]) == ATTACK_STYLE_RANGED);
        assert(env->state.ragger_limit >= RF_RAGGER_MIN_SWINGS && env->state.ragger_limit <= RF_RAGGER_MAX_SWINGS);
    }
    assert(near_share(ragged, n, RF_REP_RAG_CAP));
    destroy();
    /* Without the pool there is no ranged kit to wear: nobody rags. */
    env = make_pool_env(6000, 1, 72, 0);
    env->tag = 1;
    env->training.rep = 10;
    for (int i = 0; i < 300; i++) {
        RiskfightTransition t = {.delay = 100};
        riskfight_training_next_fight(&env->training, &env->state, &env->context, &t, env->tag);
        assert(!env->state.ragger);
    }
    destroy();
    /* The fight itself: we stand idle, he swings his share and leaves. */
    env = make_pool_env(6000, 1, 73, 1);
    env->tag = 1;
    env->training.rep = 10;
    for (int i = 0; !env->state.ragger; i++) {
        assert(i < 200);
        RiskfightTransition t = {.delay = 100};
        riskfight_training_next_fight(&env->training, &env->state, &env->context, &t, env->tag);
    }
    riskfight_native_observe(env);
    int limit = env->state.ragger_limit;
    float stake = env->session_stake;
    int steps = 0;
    while (!env->state.env.episode_over) {
        assert(++steps < RISKFIGHT_ROUND_TICKS);
        puf_step(env);
        assert(terminals[0] == 0);
    }
    assert(env->state.outcome[0] == RISKFIGHT_RAGGED && env->state.outcome[1] == RISKFIGHT_RAGGED);
    assert(rewards[0] == 0 && rewards[1] == 0 && env->session_stake == stake);
    assert(env->state.ragger_swings >= limit && env->state.escaped[1] && !env->state.escaped[0]);
    assert(env->log.ragged == 1 && env->log.ragger_fights == 1 && env->log.kills == 0 && env->log.deaths == 0 && env->log.escapes == 0 && env->log.timeouts == 0);
    float rep_at_end = env->training.rep / 0.99953773f;
    assert(!env->wait_transition.rematch);
    assert(env->wait_left >= (int)(delay_min(&RF_DELAY_RESTOCK) * (1.0f + RF_REP_SEARCH * rep_at_end) - 1) &&
        env->wait_left <= (int)(delay_max(&RF_DELAY_RESTOCK) * (1.0f + RF_REP_SEARCH * rep_at_end) + 2));
    printf("ragger: swung %d of %d, left; ragged outcome, no stake, wait %d ticks at rep %.2f\n", env->state.ragger_swings, limit, env->wait_left, rep_at_end);
    destroy();
}

/* Seat 1's death price is the inverse of seat 0's (death_price is us / him: his is him / us), clamped to the price range [0.1, 2.5]: his reward of a death is minus
 * it, of a mutual death 1 minus it, and his observation reads it where seat 0's reads death_price. */
static void test_seat_one_price_mirrors(void) {
    env = make_frozen_env(6000, 2.0, 23);
    assert(riskfight_seat_price(&env->state, 0) == 2.0f && riskfight_seat_price(&env->state, 1) == 0.5f);
    float obs[RF_OBS_SIZE];
    riskfight_write_observation(&env->state, 0, obs);
    assert(obs[RF_PRICE] == 2.0f * 10.0f / RF_PRICE_MAX_X10);
    riskfight_write_observation(&env->state, 1, obs);
    assert(obs[RF_PRICE] == 0.5f * 10.0f / RF_PRICE_MAX_X10);
    assert(observations[1][RF_PRICE] == obs[RF_PRICE] && observations[0][RF_PRICE] == 2.0f * 10.0f / RF_PRICE_MAX_X10);
    env->state.env.players[1].current_hitpoints = 0;
    puf_step(env);
    assert(rewards[0] == 1 * RF_REWARD_SCALE && rewards[1] == -0.5f * RF_REWARD_SCALE);
    wait_out();
    env->state.env.players[0].current_hitpoints = 0;
    puf_step(env);
    assert(rewards[0] == -2.0f * RF_REWARD_SCALE && rewards[1] == 1 * RF_REWARD_SCALE);
    wait_out();
    env->state.env.players[0].current_hitpoints = 0;
    env->state.env.players[1].current_hitpoints = 0;
    puf_step(env);
    assert(rewards[0] == (1 - 2.0f) * RF_REWARD_SCALE && rewards[1] == (1 - 0.5f) * RF_REWARD_SCALE);
    /* The clamp: at the lowest price seat 1's inverse (10) is held at the highest, 2.5; at the highest seat 1's is 0.4, inside the range. */
    env->state.death_price = 0.1f;
    assert(riskfight_seat_price(&env->state, 1) == RF_PRICE_MAX_X10 / 10.0f);
    riskfight_write_observation(&env->state, 1, obs);
    assert(obs[RF_PRICE] == 1.0f);
    env->state.death_price = RF_PRICE_MAX_X10 / 10.0f;
    assert(fabsf(riskfight_seat_price(&env->state, 1) - 0.4f) < 1e-6f);
    destroy();
    puts("price mirror: seat 1 reads and is paid 1 / price, clamped to [0.1, 2.5]");
}

static void test_reward_scale_stays_within_one(void) {
    assert(RF_REWARD_SCALE * (RF_PRICE_MAX_X10 / 10.0f) == 1.0f);
    /* The largest price: our death is -1, a kill 1 / Pmax, a mutual death 1 - 2.5 = -1.5 / 2.5; seat 1's price is 0.4, his death -0.4 / 2.5. */
    env = make_frozen_env(6000, RF_PRICE_MAX_X10 / 10.0, 21);
    env->state.env.players[0].current_hitpoints = 0;
    puf_step(env);
    assert(rewards[0] == -1.0f && rewards[1] == 1 * RF_REWARD_SCALE);
    wait_out();
    env->state.env.players[1].current_hitpoints = 0;
    puf_step(env);
    assert(rewards[0] == 1 * RF_REWARD_SCALE && fabsf(rewards[1] + 0.4f * RF_REWARD_SCALE) < 1e-6f);
    wait_out();
    env->state.env.players[0].current_hitpoints = 0;
    env->state.env.players[1].current_hitpoints = 0;
    puf_step(env);
    assert(fabsf(rewards[0] - (1 - 2.5f) * RF_REWARD_SCALE) < 1e-6f && fabsf(rewards[1] - (1 - 0.4f) * RF_REWARD_SCALE) < 1e-6f);
    destroy();
    /* Drawn prices over many fights of a session: every reward of every outcome stays within [-1, 1], and one scale serves every fight. */
    env = make_frozen_env(60000, 0, 22);
    float most = 0;
    for (int fight = 0; fight < 400; fight++) {
        env->state.env.players[fight & 1].current_hitpoints = 0;
        float price = env->state.death_price;
        assert(price >= 0.4f - 1e-6f && price <= 2.5f + 1e-6f);
        puf_step(env);
        for (int i = 0; i < 2; i++) {
            assert(fabsf(rewards[i]) <= 1.0f);
            if (fabsf(rewards[i]) > most) most = fabsf(rewards[i]);
        }
        float expected = (fight & 1) ? 1 * RF_REWARD_SCALE : -price * RF_REWARD_SCALE;
        assert(fabsf(rewards[0] - expected) < 1e-6f);
        if (!terminals[0]) wait_out();
        if (terminals[0]) break;
    }
    assert(most >= RF_REWARD_SCALE);
    destroy();
}

static void test_session_of_n_ticks_ends(void) {
    const int budget = 300;
    env = make_env(budget, 1, 31);
    for (int tick = 0; tick < budget - 1; tick++) {
        puf_step(env);
        assert(terminals[0] == 0 && terminals[1] == 0 && env->log.n == 0);
        assert(env->state.session_ticks_left == budget - env->session_elapsed);
    }
    puf_step(env);
    assert(terminals[0] == 1 && terminals[1] == 1 && env->log.n == 1);
    assert(env->session_elapsed == 0 && env->session_fights == 0); /* the next session has begun */
    assert(env->state.session_ticks_left == budget);
    destroy();
    /* A fight that ends on the last tick counts; the session then ends with it. */
    env = make_env(budget, 1, 32);
    env->session_elapsed = budget - 1;
    env->state.env.players[1].current_hitpoints = 0;
    puf_step(env);
    assert(terminals[0] == 1 && rewards[0] == RF_REWARD_SCALE && env->log.kills == 1 && env->log.n == 1);
    assert(env->log.net_stake == (float)RF_TICKS_PER_HOUR / budget);
    destroy();
    /* A delay that outruns the budget ends the session at the fight's end: no further fight is started. */
    env = make_env(budget, 1, 33);
    env->session_elapsed = budget - 2;
    assert(delay_min(&RF_DELAY_RESTOCK) >= 1);
    env->state.env.players[1].current_hitpoints = 0;
    puf_step(env);
    assert(terminals[0] == 0 && env->wait_left > 0 && env->log.kills == 1);
    assert(wait_out() == 1 && terminals[0] == 1 && env->log.n == 1 && env->log.kills == 1);
    destroy();
    /* A fight still running when the budget runs out earns nothing. */
    env = make_env(budget, 1, 34);
    env->session_elapsed = budget - 1;
    puf_step(env);
    assert(terminals[0] == 1 && rewards[0] == 0 && env->log.kills == 0 && env->log.deaths == 0 && env->log.n == 1 && env->log.net_stake == 0);
    destroy();
}

/* A wait is real ticks: a 256-tick restock is 256 steps of zero reward, and a session takes as many steps as it has ticks. The state reset (the trainer zeroes
 * the net's recurrent state, no terminal) is set exactly on the step whose observation is a fight's first: never mid-fight, never in a wait. */
static void test_waits_are_steps(void) {
    env = make_env(6000, 1, 51);
    assert(resets[0] == 1 && resets[1] == 1); /* the session's first fight */
    puf_step(env);
    assert(resets[0] == 0 && resets[1] == 0 && terminals[0] == 0 && env->wait_left == 0);
    actions[0][RF_PRIMARY] = RF_TELEPORT;
    puf_step(env);
    assert(env->wait_left > 0);
    assert(resets[0] == 0 && resets[1] == 0);
    env->wait_left = env->wait_transition.delay = 256;
    int before = env->session_elapsed, steps = 0;
    memset(actions, 0, sizeof(actions));
    while (env->wait_left > 0) {
        puf_step(env);
        steps++;
        assert(rewards[0] == 0 && rewards[1] == 0 && terminals[0] == 0 && terminals[1] == 0);
        assert(resets[0] == (env->wait_left == 0) && resets[1] == resets[0]); /* the next fight's first observation */
        assert(env->session_elapsed == before + steps);
        assert(env->wait_left == 0 ? observations[0][RF_SESSION_START + 2] == 0 : observations[0][RF_SESSION_START + 2] == 1);
    }
    assert(steps == 256 && env->state.env.tick == 0 && !env->state.env.episode_over);
    assert(env->log.wait_ticks == 256);
    destroy();
    /* A whole session of teleports and waits: one step per tick, the last of them terminal. */
    const int budget = 3000;
    env = make_env(budget, 0, 52);
    int total = 0, fight_resets = 0;
    float reward_in_waits = 0;
    while (!terminals[0]) {
        actions[0][RF_PRIMARY] = RF_TELEPORT;
        int waiting = env->wait_left > 0, fights = env->session_fights;
        puf_step(env);
        total++;
        if (waiting) reward_in_waits += fabsf(rewards[0]) + fabsf(rewards[1]);
        if (!terminals[0]) {
            fight_resets += resets[0] == 1;
            assert(resets[0] == (waiting || env->session_fights != fights) * (env->wait_left == 0)); /* a reset only where a wait or a fight ended into a fresh fight */
        }
        assert(total <= budget);
    }
    assert(resets[0] == 1 && resets[1] == 1); /* the terminal step's observation is the next session's first fight */
    /* every fight after the first began with a reset; the last one may still have been running when the budget ran out */
    assert(fight_resets == env->log.rounds || fight_resets == env->log.rounds - 1);
    assert(total == budget && reward_in_waits == 0 && env->log.n == 1);
    assert(env->log.wait_ticks > 0 && env->log.wait_ticks + env->log.episode_length >= budget - 1);
    printf("waits: a session of %d ticks is %d steps, %.0f of them waiting between %.0f fights, %d fight-start resets\n", budget, total, env->log.wait_ticks, env->log.rounds, fight_resets);
    destroy();
}

static void test_session_observation(void) {
    env = make_env(6000, 1, 41);
    float* block = observations[0] + RF_SESSION_START;
    assert(RF_TIMING_START == RF_SESSION_START + RF_SESSION_SIZE && RF_TACTICS_START == RF_TIMING_START + RF_TIMING_SIZE && RF_OBS_SIZE == RF_TACTICS_START + RF_TACTICS_SIZE);
    assert(block[0] == 1 && block[1] == 1 && block[2] == 0 && block[3] == 0 && block[4] == 0);
    env->session_elapsed = 6000 - RF_SESSION_HORIZON_TICKS / 2;
    puf_step(env);
    assert(fabsf(block[0] - (RF_SESSION_HORIZON_TICKS / 2 - 1) / (float)RF_SESSION_HORIZON_TICKS) < 1e-3f);
    for (int i = 0; i < RF_OBS_SIZE; i++) assert(fabsf(observations[0][i]) <= 1 && fabsf(observations[1][i]) <= 1);
    destroy();
    /* His special energy as a player counts it: every special of his seen costs its price, the regeneration is 10 per 50 ticks, and our own are counted for him. */
    RiskfightState s;
    riskfight_reset((EncounterState*)&s, (EncounterContext*)&(RiskfightContext){0}, 5);
    float obs[RF_OBS_SIZE];
    riskfight_write_observation(&s, 0, obs);
    assert(obs[RF_SESSION_START] == 1 && obs[RF_SESSION_START + 1] == 1);
    riskfight_special_used(&s, 1, ITEM_VOIDWAKER, 1);
    riskfight_write_observation(&s, 0, obs);
    assert(obs[RF_SESSION_START + 1] == 0.5f && s.specs_used[1] == 1);
    riskfight_write_observation(&s, 1, obs);
    assert(obs[RF_SESSION_START + 1] == 1); /* we have not specced: he sees 100 of ours */
    for (int tick = 0; tick < OSRS_SPEC_REGEN_INTERVAL; tick++) riskfight_spec_est_regen(&s.visible[0]);
    riskfight_write_observation(&s, 0, obs);
    assert(obs[RF_SESSION_START + 1] == 0.6f);
}

int main(void) {
    test_every_fight_starts_regeared();
    test_death_is_a_regear_like_any_end();
    test_teleport_costs_a_regear();
    test_one_regear_delay_and_reputation();
    test_rep_keys_reach_session_rules();
    test_rude_teleports_build_reputation();
    test_ragger_rags_and_leaves();
    test_rude_teleport_loses_the_opponent();
    test_seat_one_price_mirrors();
    test_reward_scale_stays_within_one();
    test_session_of_n_ticks_ends();
    test_waits_are_steps();
    test_session_observation();
    puts("Riskfight session contracts passed");
}
