/* The norms of teleporting (riskfight_norms.h): kosher against rude across the cases (both specials spent, ours only, none, his only, out of hard food, out of prayer
 * restores, brewed down without boosts), in both seats and through the whole session (a rude teleport is no rematch, costs reputation when ours; scripted and
 * hooked opponents are judged by the same rule); the player's reading in the session block of the observation; and the plugin's Piety and Smite from the first tick of a
 * deployed fight, draining prayer as in the game.
 *
 *   built by run_suite.sh (cc -std=gnu11 -O2 -I. -Isrc ..., cwd = repo root) */
#include "../../osrs_riskfight/osrs_riskfight.h"
#include <assert.h>
#include <stdio.h>

static Env* env;
static float observations[2][RF_OBS_SIZE];
static float actions[2][RF_HEADS];
static float rewards[2], terminals[2];
static unsigned char masks[2][RF_MASK_SIZE];

static Env* make_env(int self_play, unsigned rng) {
    DictItem items[] = {{.key = "opponent_type", .value = 0}, {.key = "self_play", .value = self_play},
        {.key = "session_ticks", .value = 6000}, {.key = "curveball_probability", .value = 0},
        {.key = "death_price", .value = 1}, {.key = "opponent_pool", .value = 0}};
    Dict kwargs = {.items = items, .size = sizeof(items) / sizeof(*items)};
    Env* e = (Env*)calloc(1, sizeof(*e));
    e->rng = rng;
    puf_init(e, &kwargs);
    for (int i = 0; i < e->num_agents; i++) e->agents[i] = (Agent){
        .observations = observations[i], .actions = actions[i], .rewards = &rewards[i],
        .terminals = &terminals[i], .action_mask = masks[i], .policy = i};
    puf_reset(e);
    e->training.rep = 0;
    riskfight_native_observe(e);
    memset(actions, 0, sizeof(actions));
    return e;
}
static void destroy(void) { puf_close(env); free(env); env = NULL; }

static int is_hard_food(OsrsConsumableKind k) {
    return k == OSRS_CONSUMABLE_ANGLERFISH || k == OSRS_CONSUMABLE_SHARK_FOOD || k == OSRS_CONSUMABLE_MARLIN || k == OSRS_CONSUMABLE_SUMMER_PIE;
}
static int is_restore(OsrsConsumableKind k) {
    return k == OSRS_CONSUMABLE_PRAYER_RESTORE || k == OSRS_CONSUMABLE_SUPER_RESTORE || k == OSRS_CONSUMABLE_SANFEW;
}
static int is_boost(OsrsConsumableKind k) {
    return k == OSRS_CONSUMABLE_SUPER_COMBAT || k == OSRS_CONSUMABLE_DIVINE_COMBAT || k == OSRS_CONSUMABLE_RANGING || k == OSRS_CONSUMABLE_DIVINE_RANGING ||
        k == OSRS_CONSUMABLE_BASTION;
}
static int count_class(const Player* p, int (*in_class)(OsrsConsumableKind)) {
    int n = 0;
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++)
        n += in_class((OsrsConsumableKind)osrs_inventory_cell_metadata(&p->inventory_cells[slot])->consumable_kind);
    return n;
}
static void remove_class(Player* p, int (*in_class)(OsrsConsumableKind)) {
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++)
        if (in_class((OsrsConsumableKind)osrs_inventory_cell_metadata(&p->inventory_cells[slot])->consumable_kind))
            p->inventory_cells[slot] = osrs_inventory_cell_empty();
}

/* A player with the stock of a fresh regear (the default loadout), special energy as given. */
static Player stocked(int seat, int energy) {
    Env* e = make_env(1, 5 + (unsigned)seat);
    Player p = e->state.env.players[seat];
    p.special_energy = energy;
    puf_close(e);
    free(e);
    return p;
}
/* The world's reading of a teleport of `a` with `b` on the other side. */
static int world_kosher(const Player* a, const Player* b) {
    const OsrsEscapeSupplies own = osrs_escape_supplies(a), other = osrs_escape_supplies(b);
    return riskfight_teleport_kosher(&own, &other);
}

/* The supply classes, defined from the inventory kinds: the default kit holds every one, and each is counted from its own kinds only. */
static void test_supply_classes(void) {
    Player p = stocked(0, 100);
    OsrsEscapeSupplies s = osrs_escape_supplies(&p);
    assert(!osrs_escape_no_hard_food(&s) && !osrs_escape_no_restore(&s) && !osrs_escape_no_boost(&s));
    assert(s.hard_food == count_class(&p, is_hard_food) && s.hard_food > 0);
    /* Karambwan and halibut (the combo clock) are not hard food: without the rest of the food the class is out although they remain. */
    assert(s.halibut > 0);
    remove_class(&p, is_hard_food);
    s = osrs_escape_supplies(&p);
    assert(osrs_escape_no_hard_food(&s) && s.halibut > 0 && s.food > 0);
    /* A summer pie with one bite left is still hard food. */
    p.inventory_cells[0] = osrs_inventory_cell_from_content_code(osrs_inventory_content_code_from_consumable(OSRS_CONSUMABLE_SUMMER_PIE, 1));
    s = osrs_escape_supplies(&p);
    assert(!osrs_escape_no_hard_food(&s) && s.hard_food == 1);
    /* Restores: prayer potion, super restore and sanfew, by doses. Brews are not restores. */
    p = stocked(0, 100);
    assert(osrs_escape_supplies(&p).restore_doses == 2 * 4);
    remove_class(&p, is_restore);
    s = osrs_escape_supplies(&p);
    assert(osrs_escape_no_restore(&s) && s.brew_doses > 0);
    p.inventory_cells[1] = osrs_inventory_cell_from_content_code(osrs_inventory_content_code_from_consumable(OSRS_CONSUMABLE_PRAYER_RESTORE, 1));
    s = osrs_escape_supplies(&p);
    assert(!osrs_escape_no_restore(&s) && s.restore_doses == 1);
    /* Boosts: out only with no potion left AND nothing still boosted. */
    p = stocked(0, 100);
    assert(p.current_attack > p.base_attack);
    remove_class(&p, is_boost);
    s = osrs_escape_supplies(&p);
    assert(s.boost_doses == 0 && !osrs_escape_no_boost(&s)); /* the boost is still on: not out */
    p.current_attack = p.base_attack; p.current_strength = p.base_strength; p.current_ranged = p.base_ranged;
    s = osrs_escape_supplies(&p);
    assert(osrs_escape_no_boost(&s));
    p.current_attack = p.base_attack - 10; /* brewed down below base: still out */
    s = osrs_escape_supplies(&p);
    assert(osrs_escape_no_boost(&s));
    p.inventory_cells[2] = osrs_inventory_cell_from_content_code(osrs_inventory_content_code_from_consumable(OSRS_CONSUMABLE_SUPER_COMBAT, 1));
    s = osrs_escape_supplies(&p);
    assert(!osrs_escape_no_boost(&s));
    p.inventory_cells[2] = osrs_inventory_cell_from_content_code(osrs_inventory_content_code_from_consumable(OSRS_CONSUMABLE_RANGING, 2));
    s = osrs_escape_supplies(&p);
    assert(s.boost_doses == 2 && !osrs_escape_no_boost(&s)); /* a ranger's boost is a boost */
}

/* The world's rule across the cases, in both seats. */
static void test_classification(void) {
    for (int seat = 0; seat < 2; seat++) {
        Player us = stocked(seat, 100), him = stocked(1 - seat, 100);
        const int spent = 0; /* below every spec cost of the kit (the cheapest is 50) */
        /* None spent: a normal opening, nobody has specced. Rude. */
        assert(!world_kosher(&us, &him));
        /* Our spec only: ours spent, his not. The old rude case, kept inside the definition. Rude. */
        us.special_energy = spent;
        assert(!world_kosher(&us, &him));
        /* His only: he specced, we left with ours unused. Rude too. */
        us.special_energy = 100; him.special_energy = spent;
        assert(!world_kosher(&us, &him));
        /* Both spent. Kosher, whoever leaves. */
        us.special_energy = spent;
        assert(world_kosher(&us, &him) && world_kosher(&him, &us));
        /* Energy short of the cheapest special counts as spent, the cheapest itself does not. */
        const int cheapest = osrs_escape_supplies(&us).minimum_spec_cost;
        assert(cheapest == 50);
        us.special_energy = cheapest - 1; him.special_energy = cheapest - 1;
        assert(world_kosher(&us, &him));
        him.special_energy = cheapest;
        assert(!world_kosher(&us, &him));
        him.special_energy = 100; us.special_energy = 100;
        /* Out of hard food: kosher with both specials full. The other side's supplies do not matter. */
        Player out = us;
        remove_class(&out, is_hard_food);
        assert(world_kosher(&out, &him) && !world_kosher(&him, &out));
        /* Out of prayer restores. */
        out = us; remove_class(&out, is_restore);
        assert(world_kosher(&out, &him) && !world_kosher(&him, &out));
        /* Brewed down with no boost left: kosher; boosted still, or a potion in the bag, not. */
        out = us; remove_class(&out, is_boost);
        assert(!world_kosher(&out, &him));
        out.current_attack = out.base_attack; out.current_strength = out.base_strength;
        assert(world_kosher(&out, &him) && !world_kosher(&him, &out));
        out = us; out.current_attack = out.base_attack; out.current_strength = out.base_strength;
        assert(!world_kosher(&out, &him)); /* a potion is still in the bag */
    }
}

/* The player's reading from what he knows: his bag, his special energy, and his estimate of the other side's against the weapons of the threat block. */
static void test_player_reading(void) {
    Player us = stocked(0, 100);
    float threat[RF_THREAT_SIZE] = {0};
    /* No spec weapon known or likely for him: only our own energy decides, and with a special in our own bag we have not spent it: rude. */
    assert(riskfight_threat_cheapest_spec(threat) == 0);
    assert(!riskfight_teleport_kosher_now(&us, threat, 100));
    us.special_energy = 0;
    assert(riskfight_teleport_kosher_now(&us, threat, 100));
    /* A wielded spec weapon (p 1, cost 50): he could spec at 50 and above. */
    threat[0] = 1; threat[8] = 0.5f;
    assert(riskfight_threat_cheapest_spec(threat) == 50);
    assert(!riskfight_teleport_kosher_now(&us, threat, 100) && !riskfight_teleport_kosher_now(&us, threat, 50));
    assert(riskfight_teleport_kosher_now(&us, threat, 49) && riskfight_teleport_kosher_now(&us, threat, 0));
    /* A likely weapon (p at least RF_KOSHER_LIKELY) counts and the cheaper one decides; an unlikely one does not. */
    float* second = threat + RF_THREAT_HIS_WIDTH;
    second[0] = RF_KOSHER_LIKELY; second[8] = 0.25f;
    assert(riskfight_threat_cheapest_spec(threat) == 25);
    assert(!riskfight_teleport_kosher_now(&us, threat, 25) && riskfight_teleport_kosher_now(&us, threat, 24));
    second[0] = RF_KOSHER_LIKELY - 0.01f;
    assert(riskfight_threat_cheapest_spec(threat) == 50);
    assert(riskfight_teleport_kosher_now(&us, threat, 25));
    /* A special that costs no energy (a Behead) is no reason to stay. */
    second[0] = 1; second[8] = 0;
    assert(riskfight_threat_cheapest_spec(threat) == 50);
    /* Our own supplies override: out of hard food, kosher with both specials full. */
    us.special_energy = 100;
    assert(!riskfight_teleport_kosher_now(&us, threat, 100));
    remove_class(&us, is_hard_food);
    assert(riskfight_teleport_kosher_now(&us, threat, 100));
}

/* The session block carries the reading, written by the same code in the sim and in gs_rl_server. */
static void test_observation(void) {
    assert(RF_SESSION_SIZE == 8 && RF_OBS_SIZE == RF_SESSION_START + RF_SESSION_SIZE);
    env = make_env(1, 11);
    RiskfightState* s = &env->state;
    float obs[RF_OBS_SIZE];
    riskfight_write_observation(s, 0, obs);
    assert(obs[RF_SESSION_START + 7] == 0); /* a fresh regear: nothing spent, every class stocked */
    assert(obs[RF_SESSION_START + 7] == (float)riskfight_teleport_kosher_now(&s->env.players[0], obs + RF_THREAT_START, s->visible[0].spec_est));
    /* Our specials spent and his estimated spent: kosher. His estimate comes from the specials seen, ours from our own bar. */
    s->env.players[0].special_energy = 0;
    s->visible[0].spec_est = 0;
    riskfight_write_observation(s, 0, obs);
    assert(obs[RF_SESSION_START + 7] == 1);
    s->visible[0].spec_est = 100;
    riskfight_write_observation(s, 0, obs);
    assert(obs[RF_SESSION_START + 7] == (float)riskfight_teleport_kosher_now(&s->env.players[0], obs + RF_THREAT_START, 100));
    /* Seat 1 reads its own side. */
    s->env.players[1].special_energy = 0;
    s->visible[1].spec_est = 0;
    riskfight_write_observation(s, 1, obs);
    assert(obs[RF_SESSION_START + 7] == 1);
    remove_class(&s->env.players[0], is_hard_food);
    s->visible[0].spec_est = 100;
    s->env.players[0].special_energy = 100;
    riskfight_write_observation(s, 0, obs);
    assert(obs[RF_SESSION_START + 7] == 1);
    destroy();
}

/* One teleport through the whole session: rude never rematches, kosher does at about RF_REMATCH_PROBABILITY; reputation moves by our rude teleports only. */
typedef struct { int our_specs_left, his_specs_left, we_teleport, he_teleports, we_out_of_food; } Case;
static void run_case(const Case* c, int trials, int* rematches, int* ours_rude, int* his_rude, float* rep) {
    *rematches = *ours_rude = *his_rude = 0;
    *rep = 0;
    for (int trial = 0; trial < trials; trial++) {
        env = make_env(1, 200 + (unsigned)trial);
        Player* us = &env->state.env.players[0];
        Player* him = &env->state.env.players[1];
        us->special_energy = c->our_specs_left ? 100 : 0;
        him->special_energy = c->his_specs_left ? 100 : 0;
        if (c->we_out_of_food) remove_class(us, is_hard_food);
        him->current_hitpoints = 77;
        actions[0][RF_PRIMARY] = c->we_teleport ? RF_TELEPORT : 0;
        actions[1][RF_PRIMARY] = c->he_teleports ? RF_TELEPORT : 0;
        puf_step(env);
        assert(env->state.escaped[0] == c->we_teleport && env->state.escaped[1] == c->he_teleports);
        *ours_rude += env->log.rude_teleports == 1;
        *his_rude += env->log.opponent_rude_teleports == 1;
        *rep += env->training.rep;
        for (int guard = 0; env->wait_left > 0 && guard < 5000; guard++) puf_step(env);
        *rematches += env->log.rematches == 1;
        destroy();
    }
}
static void test_rematch_and_reputation(void) {
    const int trials = 300;
    int rematches, ours, his;
    float rep;
    /* Rude: none spent, ours only, his only. Never a rematch; +1 reputation each. */
    const Case rude[] = {{1, 1, 1, 0, 0}, {0, 1, 1, 0, 0}, {1, 0, 1, 0, 0}};
    for (size_t i = 0; i < sizeof(rude) / sizeof(*rude); i++) {
        run_case(&rude[i], trials, &rematches, &ours, &his, &rep);
        assert(ours == trials && his == 0 && rematches == 0);
        assert(fabsf(rep - (float)trials) < 0.01f * trials);
    }
    /* Kosher: both spent, or out of food with specials full. A rematch comes at about its probability; no reputation. */
    const Case kosher[] = {{0, 0, 1, 0, 0}, {1, 1, 1, 0, 1}};
    for (size_t i = 0; i < sizeof(kosher) / sizeof(*kosher); i++) {
        run_case(&kosher[i], trials, &rematches, &ours, &his, &rep);
        assert(ours == 0 && his == 0 && rep == 0);
        assert(rematches > trials * RF_REMATCH_PROBABILITY / 2 && rematches < trials * (1 + RF_REMATCH_PROBABILITY) / 2 + 1);
    }
    /* He teleports rudely (seat 1, a second agent here): the same rule, no rematch, and our reputation is untouched. */
    const Case his_rude = {1, 1, 0, 1, 0};
    run_case(&his_rude, trials, &rematches, &ours, &his, &rep);
    assert(ours == 0 && his == trials && rematches == 0 && rep == 0);
    /* He teleports kosher: both spent. */
    const Case his_kosher = {0, 0, 0, 1, 0};
    run_case(&his_kosher, trials, &rematches, &ours, &his, &rep);
    assert(ours == 0 && his == 0 && rematches > trials * RF_REMATCH_PROBABILITY / 2);
    /* Both leave at once, one of them rudely (we have our special, he has spent his): rude, no rematch. */
    const Case both = {1, 0, 1, 1, 0};
    run_case(&both, trials, &rematches, &ours, &his, &rep);
    assert(ours == trials && his == trials && rematches == 0);
}

/* A scripted or fitted opponent (driven through the hook the way OppFit is) is judged by the same rule, from his true supplies. */
static int hook_calls;
static void hook_teleport(void* user, const void* state, int* out) {
    (void)user;
    const RiskfightState* s = state;
    memset(out, 0, RF_HEADS * sizeof(int));
    hook_calls++;
    if (s->env.tick >= 3) out[RF_PRIMARY] = RF_TELEPORT;
}
static void test_scripted_opponent(void) {
    for (int spent = 0; spent < 2; spent++) {
        int rude = 0, rematches = 0;
        for (int trial = 0; trial < 200; trial++) {
            env = make_env(0, 700 + (unsigned)trial);
            env->context.opp_hook = hook_teleport;
            env->state.env.players[1].special_energy = spent ? 0 : 100;
            env->state.env.players[0].special_energy = spent ? 0 : 100;
            for (int t = 0; t < 10 && !env->state.escaped[1]; t++) puf_step(env);
            assert(env->state.escaped[1] && !env->state.escaped[0]);
            assert(riskfight_rude_teleport_of(&env->state, 1) == !spent);
            rude += env->log.opponent_rude_teleports == 1;
            assert(env->training.rep == 0); /* ours is how people treat us: his rudeness does not touch it */
            for (int guard = 0; env->wait_left > 0 && guard < 5000; guard++) puf_step(env);
            rematches += env->log.rematches == 1;
            destroy();
        }
        if (spent) assert(rude == 0 && rematches > 0);
        else assert(rude == 200 && rematches == 0);
    }
    assert(hook_calls > 0);
}

static void hook_idle(void* user, const void* state, int* out) { (void)user; (void)state; memset(out, 0, RF_HEADS * sizeof(int)); }

/* Prayers on from the first tick of a deployed fight: Piety for us, Smite overhead, draining as in the game; his script keeps its own. */
static void test_plugin_prayers(void) {
    /* Undeployed, a net's action 0 is "off": nothing is on, as before. */
    riskfight_deployable = 0;
    env = make_env(0, 31);
    puf_step(env);
    assert(env->state.env.players[0].offensive_prayer == OFFENSIVE_PRAYER_NONE && env->state.env.players[0].prayer == PRAYER_NONE);
    destroy();

    riskfight_deployable = 1;
    env = make_env(0, 31);
    env->context.opp_hook = hook_idle; /* he stands there, so the fight outlasts the drain */
    Player* us = &env->state.env.players[0];
    assert(us->offensive_prayer == OFFENSIVE_PRAYER_NONE && us->prayer == PRAYER_NONE); /* the regear starts them off: the plugin turns them on with the fight */
    const int start = us->current_prayer;
    assert(start == us->base_prayer);
    puf_step(env);
    assert(us->offensive_prayer == OFFENSIVE_PRAYER_PIETY && us->prayer == PRAYER_SMITE);
    assert(observations[0][15] == (float)OFFENSIVE_PRAYER_PIETY / 4.0f);
    assert(env->state.env.players[1].prayer != PRAYER_SMITE); /* the net's seat only */
    /* Drain: Piety 24 + Smite 12 per tick against a resistance of 60 + 2 x the prayer bonus of the gear, one point whenever the counter passes it. */
    const int bonus = encounter_player_prayer_bonus(us);
    const int resistance = 60 + 2 * bonus, per_tick = 24 + 12, ticks = 60;
    for (int t = 1; t < ticks && !env->state.env.episode_over; t++) puf_step(env);
    assert(!env->state.env.episode_over);
    const int drained = start - us->current_prayer;
    const int expected = (ticks * per_tick) / resistance;
    assert(drained >= expected - 1 && drained <= expected + 1 && drained > 0);
    /* The prayers hold while there is prayer, go off at 0 and stay off. */
    us->current_prayer = 1;
    us->prayer_drain_counter = resistance;
    puf_step(env);
    assert(us->current_prayer == 0 && us->offensive_prayer == OFFENSIVE_PRAYER_NONE && us->prayer == PRAYER_NONE);
    puf_step(env);
    assert(us->offensive_prayer == OFFENSIVE_PRAYER_NONE && us->prayer == PRAYER_NONE);
    /* A restore brings the prayer back and the plugin the prayers. */
    int slot = -1;
    for (int i = 0; i < OSRS_INVENTORY_SIZE && slot < 0; i++)
        if (is_restore((OsrsConsumableKind)osrs_inventory_cell_metadata(&us->inventory_cells[i])->consumable_kind)) slot = i;
    assert(slot >= 0);
    us->potion_timer = 0;
    actions[0][RF_DRINK] = (float)(slot + 1);
    puf_step(env);
    actions[0][RF_DRINK] = 0;
    for (int t = 0; t < 4 && us->current_prayer == 0; t++) puf_step(env);
    assert(us->current_prayer > 0);
    for (int t = 0; t < 3 && !(us->offensive_prayer == OFFENSIVE_PRAYER_PIETY && us->prayer == PRAYER_SMITE); t++) puf_step(env);
    assert(us->offensive_prayer == OFFENSIVE_PRAYER_PIETY && us->prayer == PRAYER_SMITE);
    destroy();
    riskfight_deployable = 0;
}

int main(void) {
    test_supply_classes();
    test_classification();
    test_player_reading();
    test_observation();
    test_rematch_and_reputation();
    test_scripted_opponent();
    test_plugin_prayers();
    puts("Kosher and rude teleports, supply classes, the player's reading, rematch and reputation, scripted opponents, plugin prayers passed");
    return 0;
}
