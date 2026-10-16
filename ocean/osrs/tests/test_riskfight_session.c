/* Session contracts: supplies persist across a fight boundary, a teleport refills and costs time, a rude teleport loses the opponent, rewards stay in [-1, 1]
 * at the largest price, and a session of N ticks ends. Self-play env, both agents driven by the test (agent 1 idles), so every outcome is forced. */
#include "../../osrs_riskfight/osrs_riskfight.h"
#include <assert.h>
#include <stdio.h>

static Env* env;
static float observations[2][RF_OBS_SIZE];
static float actions[2][RF_HEADS];
static float rewards[2], terminals[2];
static unsigned char masks[2][RF_MASK_SIZE];

static Env* make_env(int session_ticks, double price, unsigned rng) {
    DictItem items[] = {{.key = "opponent_type", .value = 0}, {.key = "self_play", .value = 1},
        {.key = "session_ticks", .value = session_ticks}, {.key = "curveball_probability", .value = 0},
        {.key = "death_price", .value = price}};
    Dict kwargs = {.items = items, .size = sizeof(items) / sizeof(*items)};
    Env* e = (Env*)calloc(1, sizeof(*e));
    e->rng = rng;
    puf_init(e, &kwargs);
    for (int i = 0; i < 2; i++) e->agents[i] = (Agent){
        .observations = observations[i], .actions = actions[i], .rewards = &rewards[i],
        .terminals = &terminals[i], .action_mask = masks[i], .policy = i};
    puf_reset(e);
    memset(actions, 0, sizeof(actions));
    return e;
}

static void destroy(void) { puf_close(env); free(env); env = NULL; }

static int delay_min(const RiskfightDelay* d) { return d->ticks[0]; }
static int delay_max(const RiskfightDelay* d) { return d->ticks[RF_DELAY_DECILES - 1]; }
static int count_food(const Player* p) {
    int n = 0;
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++)
        n += osrs_inventory_cell_metadata(&p->inventory_cells[slot])->click_action == OSRS_CLICK_EAT;
    return n;
}
static void empty_slots(Player* p, int from, int count) {
    for (int slot = from; slot < from + count; slot++) p->inventory_cells[slot] = osrs_inventory_cell_from_content_code(0);
}

/* A kill: he dies on the next tick, we keep everything we had. */
static void test_supplies_persist_across_a_fight_boundary(void) {
    env = make_env(6000, 1, 7);
    Player* us = &env->state.env.players[0];
    int full_food = count_food(us);
    empty_slots(us, 4, 3);
    int food = count_food(us);
    assert(food < full_food);
    uint16_t bag[OSRS_INVENTORY_SIZE];
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) bag[i] = us->inventory_cells[i].content_code;
    us->current_hitpoints = 99;
    us->special_energy = 40;
    us->spec_regen_active = 1;
    us->item_effect_state.special_regen_ticks = 0;
    us->current_attack = 113;
    env->state.inventory_use[0].vengeance_sacks = 5;
    env->state.inventory_use[0].divine_combat_ticks = 480;
    env->state.env.players[1].current_hitpoints = 0;
    int before = env->session_elapsed;
    puf_step(env);
    assert(terminals[0] == 0 && terminals[1] == 0);
    assert(env->log.kills == 1 && env->log.n == 0 && env->session_fights == 1);
    int delay = env->session_elapsed - before - 1;
    assert(delay >= delay_min(&RF_DELAY_SEARCH) && delay <= delay_max(&RF_DELAY_SEARCH));
    us = &env->state.env.players[0];
    assert(env->state.env.tick == 0 && !env->state.env.episode_over);
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) assert(us->inventory_cells[i].content_code == bag[i]);
    assert(count_food(us) == food);
    assert(us->current_hitpoints == 99);
    /* The kill tick itself is one more tick of the session than the delay. */
    int aged = delay + 1;
    assert(us->special_energy == (40 + 10 * (aged / 50) > 100 ? 100 : 40 + 10 * (aged / 50)));
    assert(env->state.inventory_use[0].vengeance_sacks == 5);
    assert(env->state.inventory_use[0].divine_combat_ticks == (480 > aged ? 480 - aged : 0));
    /* A new opponent: full supplies, full hitpoints, his own price. */
    const Player* him = &env->state.env.players[1];
    assert(him->current_hitpoints > 0 && count_food(him) == full_food && him->special_energy == 100);
    assert(env->state.session_ticks_left == 6000 - env->session_elapsed);
    destroy();
}

/* A death: the price is paid, everything is refilled, the respawn delay D passes, a new opponent. */
static void test_death_refills_and_pays_the_respawn(void) {
    env = make_env(6000, 1, 9);
    Player* us = &env->state.env.players[0];
    int full_food = count_food(us);
    empty_slots(us, 4, 6);
    us->special_energy = 10;
    us->current_hitpoints = 0;
    int before = env->session_elapsed;
    puf_step(env);
    assert(terminals[0] == 0 && env->log.deaths == 1);
    int delay = env->session_elapsed - before - 1;
    assert(delay >= delay_min(&RF_DELAY_RESPAWN) && delay <= delay_max(&RF_DELAY_RESPAWN));
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
    RiskfightTransition t = riskfight_session_transition(&tmp, &rng);
    *rematch = t.rematch;
    return t.delay;
}

static void test_teleport_refills_and_costs_the_restock(void) {
    env = make_env(6000, 1, 11);
    Player* us = &env->state.env.players[0];
    int full_food = count_food(us);
    empty_slots(us, 0, 12);
    us->current_hitpoints = 40;
    us->special_energy = 20;
    int rematch;
    int expected = expected_teleport_delay(env->training.rng, &rematch);
    int before = env->session_elapsed;
    actions[0][RF_PRIMARY] = RF_TELEPORT;
    puf_step(env);
    assert(terminals[0] == 0 && env->log.escapes == 1 && env->log.self_teleports == 1);
    assert(rewards[0] == 0);
    assert(env->session_elapsed - before == 1 + expected);
    assert(expected >= (rematch ? delay_min(&RF_DELAY_REMATCH) : delay_min(&RF_DELAY_RESTOCK)));
    assert(expected <= (rematch ? delay_max(&RF_DELAY_REMATCH) : delay_max(&RF_DELAY_RESTOCK)));
    us = &env->state.env.players[0];
    assert(count_food(us) == full_food && us->special_energy == 100 && us->current_hitpoints == RISKFIGHT_DEFAULT_LOADOUT.hitpoints);
    assert(env->state.env.tick == 0 && !env->state.escaped[0]);
    destroy();
}

/* Many teleports: after a rude one (ours after our special, while he can still spec) the opponent never returns; after others he does, with what he had left. */
static void teleports(int our_specs, int his_energy, int trials, int* rematches, int* rudes) {
    *rematches = *rudes = 0;
    for (int trial = 0; trial < trials; trial++) {
        env = make_env(6000, 1, 100 + trial);
        env->state.env.players[1].current_hitpoints = 77;
        env->state.env.players[1].special_energy = his_energy;
        env->state.specs_used[0] = our_specs;
        actions[0][RF_PRIMARY] = RF_TELEPORT;
        puf_step(env);
        assert(env->log.self_teleports == 1);
        *rudes += env->log.rude_teleports == 1;
        *rematches += env->log.rematches == 1;
        /* The opponent who is back is the one who was left: the hitpoints he had (natural drift aside), not a fresh 99 to 121. */
        int back = env->state.env.players[1].current_hitpoints < 100;
        assert(back == (env->log.rematches == 1));
        destroy();
    }
}

static void test_rude_teleport_loses_the_opponent(void) {
    int rematches, rudes;
    teleports(1, 100, 300, &rematches, &rudes);
    assert(rudes == 300 && rematches == 0);
    teleports(0, 100, 300, &rematches, &rudes); /* no special of ours: polite */
    assert(rudes == 0 && rematches > 0);
    int polite = rematches;
    teleports(1, 0, 300, &rematches, &rudes);   /* he has nothing left to spec with: polite */
    assert(rudes == 0 && rematches > 0);
    assert(polite > 300 * RF_REMATCH_PROBABILITY / 2 && polite < 300 * (1 + RF_REMATCH_PROBABILITY) / 2 + 1);
    /* The rule at its source. */
    RiskfightState s;
    memset(&s, 0, sizeof(s));
    s.outcome[0] = RISKFIGHT_ESCAPE;
    s.escaped[0] = 1;
    s.specs_used[0] = 1;
    s.escape_supplies[0][1].special_energy = 100;
    s.escape_supplies[0][1].minimum_spec_cost = 50;
    assert(riskfight_rude_teleport(&s));
    s.escape_supplies[0][1].special_energy = 49;
    assert(!riskfight_rude_teleport(&s));
    s.escape_supplies[0][1].special_energy = 100;
    s.specs_used[0] = 0;
    assert(!riskfight_rude_teleport(&s));
    s.specs_used[0] = 1;
    s.escaped[0] = 0;
    s.escaped[1] = 1; /* his teleport is not ours */
    assert(!riskfight_rude_teleport(&s));
    uint32_t rng = 1;
    for (int i = 0; i < 200; i++) {
        s.escaped[0] = 1; s.escaped[1] = 0;
        assert(!riskfight_session_transition(&s, &rng).rematch);
    }
}

static void test_reward_scale_stays_within_one(void) {
    assert(RF_REWARD_SCALE * (RF_PRICE_MAX_X10 / 10.0f) == 1.0f);
    /* The largest price: death is -1, kill 1 / Pmax, a mutual death 1 - 1 = -0.6 / 2.5. */
    env = make_env(6000, RF_PRICE_MAX_X10 / 10.0, 21);
    env->state.env.players[0].current_hitpoints = 0;
    puf_step(env);
    assert(rewards[0] == -1.0f && rewards[1] == 1 * RF_REWARD_SCALE);
    env->state.env.players[1].current_hitpoints = 0;
    puf_step(env);
    assert(rewards[0] == 1 * RF_REWARD_SCALE && rewards[1] == -1.0f);
    env->state.env.players[0].current_hitpoints = 0;
    env->state.env.players[1].current_hitpoints = 0;
    puf_step(env);
    assert(fabsf(rewards[0] - (1 - 2.5f) * RF_REWARD_SCALE) < 1e-6f);
    destroy();
    /* Drawn prices over many fights of a session: every reward of every outcome stays within [-1, 1], and one scale serves every fight. */
    env = make_env(60000, 0, 22);
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
    assert(delay_min(&RF_DELAY_SEARCH) >= 1);
    env->state.env.players[1].current_hitpoints = 0;
    puf_step(env);
    assert(terminals[0] == 1 && env->log.n == 1 && env->log.kills == 1);
    destroy();
    /* A fight still running when the budget runs out earns nothing. */
    env = make_env(budget, 1, 34);
    env->session_elapsed = budget - 1;
    puf_step(env);
    assert(terminals[0] == 1 && rewards[0] == 0 && env->log.kills == 0 && env->log.deaths == 0 && env->log.n == 1 && env->log.net_stake == 0);
    destroy();
}

static void test_session_observation(void) {
    env = make_env(6000, 1, 41);
    float* block = observations[0] + RF_SESSION_START;
    assert(RF_OBS_SIZE == RF_SESSION_START + RF_SESSION_SIZE);
    assert(block[0] == 1 && block[1] == 1);
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
    riskfight_special_used(&s, 1, ITEM_VOIDWAKER);
    riskfight_write_observation(&s, 0, obs);
    assert(obs[RF_SESSION_START + 1] == 0.5f && s.specs_used[1] == 1);
    riskfight_write_observation(&s, 1, obs);
    assert(obs[RF_SESSION_START + 1] == 1); /* we have not specced: he sees 100 of ours */
    for (int tick = 0; tick < OSRS_SPEC_REGEN_INTERVAL; tick++) riskfight_spec_est_regen(&s.visible[0]);
    riskfight_write_observation(&s, 0, obs);
    assert(obs[RF_SESSION_START + 1] == 0.6f);
}

int main(void) {
    test_supplies_persist_across_a_fight_boundary();
    test_death_refills_and_pays_the_respawn();
    test_teleport_refills_and_costs_the_restock();
    test_rude_teleport_loses_the_opponent();
    test_reward_scale_stays_within_one();
    test_session_of_n_ticks_ends();
    test_session_observation();
    puts("Riskfight session contracts passed");
}
