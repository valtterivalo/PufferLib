/* Over-eating is rude (the user's account, riskfight_session.h): a meal eaten while our hitpoints at the start of the tick were above his chance range is an overeat;
 * past RF_OVEREAT_PATIENCE of them in a fight he teleports within RF_ANNOYED_LEAVE_TICKS, never rematches, and the exit costs a reputation point. Summer pies are the
 * polite tool: 11 per bite and a 1-tick eat delay (wiki: Summer pie), so a pie can be followed by a triple-eat on the next tick.
 * Seat 0 is us in a self_play 0 env: the script (or OppFit) plays him. */
#include "../../osrs_riskfight/osrs_riskfight.h"
#include <assert.h>
#include <stdio.h>

static Env* env;
static float observations[RF_OBS_SIZE], actions[RF_HEADS], rewards, terminals;
static unsigned char masks[RF_MASK_SIZE];

static Env* make_env(unsigned rng, int live_loadout) {
    DictItem items[] = {{.key = "opponent_type", .value = 0}, {.key = "self_play", .value = 0}, {.key = "session_ticks", .value = 6000},
        {.key = "curveball_probability", .value = 0}, {.key = "death_price", .value = 1}, {.key = "live_loadout", .value = live_loadout},
        {.key = "rep_start_probability", .value = 0}};
    Dict kwargs = {.items = items, .size = sizeof(items) / sizeof(*items)};
    Env* e = (Env*)calloc(1, sizeof(*e));
    e->rng = rng;
    puf_init(e, &kwargs);
    e->agents[0] = (Agent){.observations = observations, .actions = actions, .rewards = &rewards, .terminals = &terminals, .action_mask = masks, .policy = 0};
    puf_reset(e);
    memset(actions, 0, sizeof(actions));
    return e;
}
static void destroy(void) { puf_close(env); free(env); env = NULL; }

static HumanCommand click(int slot) { return (HumanCommand){.kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK, .inventory_slot = slot}; }

static int slot_of(const Player* p, OsrsConsumableKind kind) {
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++)
        if (osrs_inventory_cell_metadata(&p->inventory_cells[slot])->consumable_kind == kind) return slot;
    assert(0 && "no such supply");
    return -1;
}

/** One tick of ours: the commands, and his script. */
static void tick(HumanCommand* commands, int count) {
    HumanCommandQueue ours = {.items = commands, .count = count}, theirs = {0};
    riskfight_step_queues(&env->state, &env->context, &ours, &theirs);
}

/* The definition: strictly above his chance range at the start of the tick; the range is the same one the observation shows. */
static void test_overeat_definition(void) {
    env = make_env(5, 1);
    RiskfightState* s = &env->state;
    Player* us = &s->env.players[0];
    int range = riskfight_chance_range(s, 0);
    assert(range > 0 && range < 121);
    float obs[RF_OBS_SIZE];
    for (int hp = range - 1; hp <= range + 1; hp++) {
        env->state.overeats[0] = 0;
        us->current_hitpoints = hp;
        us->food_timer = 0;
        us->attack_timer = 5;
        riskfight_write_observation(s, 0, obs);
        assert(fabsf(obs[RF_SESSION_START + 6] - (float)(hp - range) / 121.0f) < 1e-6f);
        HumanCommand eat[] = {click(slot_of(us, OSRS_CONSUMABLE_ANGLERFISH))};
        tick(eat, 1);
        assert(s->overeats[0] == (hp > range)); /* at the edge it is not one */
        if (s->env.episode_over) break;
    }
    destroy();
    /* His special energy counts: with a special he has the energy for, the range is wider, and a meal below it is polite. */
    env = make_env(5, 1);
    s = &env->state;
    s->visible[0].spec_est = 0;
    int plain = riskfight_chance_range(s, 0);
    s->visible[0].spec_est = 100;
    int armed = riskfight_chance_range(s, 0);
    assert(armed >= plain);
    destroy();
    /* A triple-eat is one decision: the hitpoints the tick began with set the bar, not the ones after the first meal. */
    env = make_env(6, 1);
    s = &env->state;
    us = &s->env.players[0];
    range = riskfight_chance_range(s, 0);
    us->current_hitpoints = range;
    HumanCommand triple[] = {click(slot_of(us, OSRS_CONSUMABLE_ANGLERFISH)), click(slot_of(us, OSRS_CONSUMABLE_KARAMBWAN)), click(slot_of(us, OSRS_CONSUMABLE_BREW))};
    tick(triple, 3);
    assert(us->ate_food_this_tick && us->ate_karambwan_this_tick && s->overeats[0] == 0);
    destroy();
    /* Brews count (they overheal towards 115), a super combat does not heal and does not count. */
    env = make_env(7, 1);
    s = &env->state;
    us = &s->env.players[0];
    range = riskfight_chance_range(s, 0);
    us->current_hitpoints = range + 1;
    HumanCommand combat[] = {click(slot_of(us, OSRS_CONSUMABLE_SUPER_COMBAT))};
    tick(combat, 1);
    assert(s->overeats[0] == 0);
    us = &s->env.players[0];
    if (!s->env.episode_over) {
        us->current_hitpoints = range + 1;
        us->potion_timer = 0;
        HumanCommand brew[] = {click(slot_of(us, OSRS_CONSUMABLE_BREW))};
        tick(brew, 1);
        assert(s->overeats[0] == 1);
    }
    destroy();
}

/* The chance range follows what he holds: his Vengeance adds the echo of our hit only after a cast of his was seen that has not proc'd (none seen at the start of a
 * fight, so it is down there), and a weapon he has not shown does not count. */
static void test_chance_range_terms(void) {
    env = make_env(8, 1);
    RiskfightState* s = &env->state;
    RiskfightVisibleOpponent* v = &s->visible[0];
    v->last_cast_tick = v->last_proc_tick = -1000; /* nothing seen: down */
    int unseen = riskfight_chance_range(s, 0);
    v->last_cast_tick = v->last_proc_tick = 5; /* a tie: down */
    int tie = riskfight_chance_range(s, 0);
    v->last_proc_tick = 4; /* cast after the last proc: up */
    int with_veng = riskfight_chance_range(s, 0);
    v->last_proc_tick = 6; /* it proc'd after the cast: down */
    int without = riskfight_chance_range(s, 0);
    assert(with_veng > without && unseen == without && tie == without);
    destroy();
}

/* His patience: PATIENCE overeats and he stays, one more and he teleports within a few ticks; the exit never rematches and costs a reputation point. */
static void test_annoyance_triggers_a_teleport_and_blocks_the_rematch(void) {
    env = make_env(11, 1);
    RiskfightState* s = &env->state;
    s->overeats[0] = RF_OVEREAT_PATIENCE;
    for (int t = 0; t < 60 && !s->env.episode_over; t++) { puf_step(env); assert(!s->annoyed); }
    destroy();
    int exits = 0;
    for (unsigned seed = 1; seed <= 40; seed++) {
        env = make_env(seed, 1);
        s = &env->state;
        s->overeats[0] = RF_OVEREAT_PATIENCE + 1;
        float rep = env->training.rep;
        int steps = 0;
        while (!s->env.episode_over && steps < RF_ANNOYED_LEAVE_TICKS + 2) { puf_step(env); steps++; }
        assert(s->annoyed && s->escaped[1] && s->outcome[0] == RISKFIGHT_ESCAPE && steps <= RF_ANNOYED_LEAVE_TICKS + 1);
        assert(riskfight_annoyed_exit(s) && !riskfight_rude_teleport(s));
        assert(env->log.annoyed_exits == 1 && env->log.overeats == RF_OVEREAT_PATIENCE + 1);
        assert(!env->wait_transition.rematch);
        assert(env->training.rep > rep + 0.99f && env->training.rep < rep + 1.01f);
        exits++;
        destroy();
    }
    /* The transition at its source: no rematch whatever the dice say. */
    RiskfightState t;
    memset(&t, 0, sizeof(t));
    t.outcome[0] = RISKFIGHT_ESCAPE;
    t.escaped[1] = 1;
    t.annoyed = 1;
    uint32_t rng = 3;
    const RiskfightRepRules rules = riskfight_rep_rules(RF_REP_SEARCH, RF_REP_RAG, RF_REP_HALF_LIFE_TICKS);
    for (int i = 0; i < 500; i++) assert(!riskfight_session_transition(&t, &rng, 0, &rules).rematch);
    t.annoyed = 0;
    int rematches = 0;
    for (int i = 0; i < 500; i++) rematches += riskfight_session_transition(&t, &rng, 0, &rules).rematch;
    assert(rematches > 200);
    /* An annoyed opponent who is killed before he leaves costs nothing. */
    t.annoyed = 1;
    t.outcome[0] = RISKFIGHT_KILL;
    t.escaped[1] = 0;
    assert(!riskfight_annoyed_exit(&t));
    printf("overeating: %d of 40 annoyed opponents left within %d ticks, none rematched, each cost a reputation point\n", exits, RF_ANNOYED_LEAVE_TICKS + 1);
}

/* The observation shows how annoyed he is: overeats over PATIENCE + 1, capped. */
static void test_annoyance_is_observed(void) {
    env = make_env(12, 1);
    RiskfightState* s = &env->state;
    float obs[RF_OBS_SIZE];
    for (int n = 0; n <= RF_OVEREAT_PATIENCE + 3; n++) {
        s->overeats[0] = n;
        riskfight_write_observation(s, 0, obs);
        float expected = n >= RF_OVEREAT_PATIENCE + 1 ? 1.0f : (float)n / (RF_OVEREAT_PATIENCE + 1);
        assert(fabsf(obs[RF_SESSION_START + 5] - expected) < 1e-6f);
        for (int i = 0; i < RF_OBS_SIZE; i++) assert(fabsf(obs[i]) <= 1.0001f);
    }
    destroy();
}

static void wear_pie_kit(Player* p) {
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) p->inventory_cells[i] = osrs_inventory_cell_from_content_code(RISKFIGHT_VW_PIES_LOADOUT.inventory[i]);
    p->food_timer = p->potion_timer = p->karambwan_timer = 0;
}

/* Summer pies: 11 per bite, 1-tick eat delay, so a pie on one tick and a triple-eat on the next. */
static void test_pie_delay_and_the_triple_eat(void) {
    /* The kit: the VW kit with RF_VW_PIES of its fourteen anglerfish swapped for pies; the plain VW kit carries none. */
    int pies = 0, fish = 0, plain_pies = 0, plain_fish = 0;
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) {
        OsrsConsumableKind kind = osrs_item_content_metadata(RISKFIGHT_VW_PIES_LOADOUT.inventory[i])->consumable_kind;
        pies += kind == OSRS_CONSUMABLE_SUMMER_PIE;
        fish += kind == OSRS_CONSUMABLE_ANGLERFISH;
        kind = osrs_item_content_metadata(RISKFIGHT_VW_LOADOUT.inventory[i])->consumable_kind;
        plain_pies += kind == OSRS_CONSUMABLE_SUMMER_PIE;
        plain_fish += kind == OSRS_CONSUMABLE_ANGLERFISH;
    }
    assert(pies == RF_VW_PIES && fish == 14 - RF_VW_PIES && plain_pies == 0 && plain_fish == 14);
    assert(RISKFIGHT_VW_PIES_LOADOUT.equipment[GEAR_SLOT_WEAPON] == ITEM_VOIDWAKER);
    /* The bite: 11 hitpoints, a 1-tick food delay (3 for anything else), 3 ticks of attack delay. */
    env = make_env(23, 3);
    Player* us = &env->state.env.players[0];
    wear_pie_kit(us);
    us->current_hitpoints = 73;
    OsrsPlayerEatResult bite = osrs_player_eat_food_effects_with_clock(us, FOOD_SUMMER_PIE, OSRS_FOOD_CONTINUOUS_CLOCK);
    assert(bite.consumed && bite.hp_healed == 11 && us->current_hitpoints == 84 && us->food_timer == 1 && bite.attack_delay_ticks == 3);
    us->food_timer = 0;
    assert(osrs_player_eat_food_effects_with_clock(us, FOOD_ANGLERFISH, OSRS_FOOD_CONTINUOUS_CLOCK).consumed && us->food_timer == 3);
    destroy();
    /* A pie from the bag, then food, karambwan and brew together on the very next tick. */
    env = make_env(24, 3);
    RiskfightState* s = &env->state;
    us = &s->env.players[0];
    wear_pie_kit(us);
    us->current_hitpoints = 73;
    HumanCommand first[] = {click(slot_of(us, OSRS_CONSUMABLE_SUMMER_PIE))};
    tick(first, 1);
    assert(!s->env.episode_over && us->ate_food_this_tick && us->food_timer == 0 && us->current_hitpoints >= 73 + 11 - 40); /* the delay is spent by the end of the tick: food again next tick */
    HumanCommand second[] = {click(slot_of(us, OSRS_CONSUMABLE_ANGLERFISH)), click(slot_of(us, OSRS_CONSUMABLE_KARAMBWAN)), click(slot_of(us, OSRS_CONSUMABLE_BREW))};
    tick(second, 3);
    assert(!s->env.episode_over && us->ate_food_this_tick && us->ate_karambwan_this_tick && us->potion_timer > 0);
    destroy();
    /* Other food keeps the next tick closed: an anglerfish, then another one on the next tick, is refused. */
    env = make_env(25, 3);
    s = &env->state;
    us = &s->env.players[0];
    wear_pie_kit(us);
    us->current_hitpoints = 40;
    HumanCommand fish_cmd[] = {click(slot_of(us, OSRS_CONSUMABLE_ANGLERFISH))};
    tick(fish_cmd, 1);
    assert(us->ate_food_this_tick && us->food_timer == 2);
    tick(fish_cmd, 1);
    assert(!us->ate_food_this_tick);
    destroy();
    puts("pies: 11 per bite, 1-tick delay (other food 3), and a triple-eat on the tick after a pie");
}

int main(void) {
    test_overeat_definition();
    test_chance_range_terms();
    test_annoyance_triggers_a_teleport_and_blocks_the_rematch();
    test_annoyance_is_observed();
    test_pie_delay_and_the_triple_eat();
    puts("Riskfight over-eating contracts passed");
}
