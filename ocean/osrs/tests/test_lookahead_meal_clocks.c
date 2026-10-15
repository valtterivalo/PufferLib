/* A lookahead world's hidden HP and meal clocks are one hypothesis of the HP history, never two independent draws.
 *
 * The bar read below says 1..2 HP after a hit that may have hidden a meal: either no meal (HP 1..2, every food free) or an anglerfish after the bar
 * was read (HP ~18..24, hard food locked three ticks). A world with the second HP and the first branch's free food eats again inside the lock, and the
 * HP history, which models the lock exactly as the sim does, has no support for that evidence ("causal HP evidence has no support").
 *
 *   cc -std=c11 -O2 -I. -o test_lookahead_meal_clocks ocean/osrs/tests/test_lookahead_meal_clocks.c -lm      (cwd = repo root) */
#define main lookahead_server_main
#include "gs_lookahead_server.c"
#undef main

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

enum { DECISION = 101 };

static Observer source;

static void build_history(void) {
    gs_infer_init(&source.inf);
    GsInfer* inf = &source.inf;
    inf->timed = inf->tracker_current = 1;
    inf->c.worn[GS_WORN_WEAPON] = GS_ITEM_VOIDWAKER;
    inf->c.strength = inf->c.attack = inf->c.ranged = inf->c.magic = 99;
    inf->c.opponent_combat_level = 121;
    inf->c.ratio = inf->c.scale = 30;
    gs_eater_observe(inf, 100);
    gs_eater_opponent_hitsplat(inf, DECISION);
    gs_eater_opponent_struck(inf, DECISION, 80);
    inf->c.ratio = 1;
    gs_eater_observe(inf, DECISION);
}

/* Every (HP, clocks) drawn is a pair of one branch: HP above what the bar allows needs a meal, and a meal locks some food clock past DECISION. */
static void test_source_draw_is_joint(void) {
    const GsHealthHistory* health = source.inf.health;
    CHECK(health->base == 82 && health->target.has_belief);
    CHECK(health->earliest_hard_ready <= DECISION); /* the earliest-free branch: the clock the worlds used to take for every draw */
    int fed = 0, fasting = 0;
    uint32_t rng = 1;
    for (int draw = 0; draw < 4000; draw++) {
        RiskfightActorStats stats;
        int hitpoints;
        GsTargetReady clocks;
        source_actor_snapshot(health, 121, 0, &rng, &stats, &hitpoints, &clocks);
        if (hitpoints > 2) {
            CHECK(clocks.hard > DECISION || clocks.potion > DECISION || clocks.fast > DECISION);
            fed++;
        } else {
            fasting += clocks.hard <= DECISION && clocks.potion <= DECISION && clocks.fast <= DECISION; /* no meal, or one that healed nothing */
        }
    }
    CHECK(fed > 0 && fasting > 0);
}

/* The world carries its branch's clocks as the sim's meal timers: HP raised by a meal at DECISION arrives with the lock still running. */
static void test_world_timers_follow_the_branch(void) {
    static Sim base;
    sim_init(&base);
    riskfight_reset((EncounterState*)&base.state, (EncounterContext*)&base.context, 7);
    for (int a = 0; a < 2; a++) base.state.poison_source[a] = OSRS_POISON_CLEAR_PRIOR;
    LaBelief belief = {.hp_low = 1, .hp_high = 24, .energy_low = 100, .energy_high = 100, .vengeance = GS_VENGEANCE_DOWN, .pid = GS_PID_LOCAL,
        .food_ready = source.inf.health->earliest_hard_ready, .bar_ratio = 1, .combat_level = 121, .health = source.inf.health};
    belief.gear = (GsObservedGear){.source = GS_GEAR_COMPOSITION_OBSERVED, .weapon = -1, .shield = -1};
    for (int slot = 0; slot < GS_ARMOR_SLOTS; slot++) belief.gear.armor[slot] = -1;
    static RiskfightState world;
    static Hist hist;
    int locked = 0, free_food = 0;
    for (uint32_t draw = 1; draw <= 400; draw++) {
        uint32_t rng = osrs_lowbias32(draw) | 1;
        la_world(&base.state, &base.hist, &belief, DECISION, &rng, &world, &hist);
        const Player* him = &world.env.players[1];
        if (him->current_hitpoints > 2) {
            /* 13 HP is also a summer pie, whose lock is one tick: free again at the next processed tick */
            CHECK(him->current_hitpoints == 13 || him->food_timer > 0 || him->potion_timer > 0 || him->karambwan_timer > 0);
            locked++;
        } else {
            free_food += him->food_timer == 0 && him->potion_timer == 0 && him->karambwan_timer == 0;
        }
    }
    CHECK(locked > 0 && free_food > 0);
}

int main(void) {
    g_ablation.hp = HP_UNIFORM;
    observer_register_items();
    build_history();
    test_source_draw_is_joint();
    test_world_timers_follow_the_branch();
    puts("Lookahead worlds keep HP and meal clocks of one branch");
    return 0;
}
