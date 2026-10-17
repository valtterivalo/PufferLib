/* The live server feeds the session block's [5] (how annoyed he is: our overeats of this fight over RF_OVEREAT_PATIENCE + 1) from the wire alone: the servings that left the G
 * line's bag between two consecutive requests are the meals of that tick, and they are overeats when the earlier request's hitpoints were strictly above its chance range
 * (riskfight_step.h). [4] (reputation) stays 0.
 *   cc -std=gnu11 -O2 -I. -Isrc -Wno-comment -o /tmp/t ocean/osrs/tests/test_rl_live_overeats.c -lm -lpthread && /tmp/t [REQUESTS...]   (cwd = repo root)
 * Unit part: the servings of a bag, the triple-eat (three meals on one tick share the tick's bar, so three overeats), the edge hp == range, and what is not a meal.
 * Trace part: every request file (default: fixtures/live_swings.requests, plus /tmp/t4.requests when present) is replayed through the server's own path (rt_parse, rt_decide with no
 * net). After each tick an oracle applies the sim's own rule to the same tick: the meals are the bag slots that held a meal and changed (the slotwise reading of
 * scripts/measure_overeats.py, independent of the server's serving totals), they are clicked into a copy of the sim state of the previous request in the order the sim allows
 * (food, brew, karambwan) through riskfight_execute_command with tick_hitpoints = that request's hitpoints, so the sim's own overeat counter is the oracle; the server's count must
 * equal it per tick and per fight, and the observation's [5] must read it. */
#define GS_RL_SERVER_NO_MAIN
#include <unistd.h>
#include "gs_rl_server.c"

#define ANGLERFISH 13441
#define BLIGHTED_ANGLERFISH 24592
#define KARAMBWAN 3144
#define BLIGHTED_KARAMBWAN 24595
#define BREW4 6685
#define BREW3 6687
#define BREW1 6691
#define BLIGHTED_RESTORE4 24598
#define SUPER_COMBAT4 12695
#define SUMMER_PIE 7218
#define HALF_SUMMER_PIE 7220
#define DRAGON_DEFENDER 12954

static void bag_of(int* bag, const int* items, int count) {
    for (int i = 0; i < TAPE_BAG; i++) bag[i] = i < count ? items[i] : -1;
}

static RtServings servings_of(const int* items, int count) {
    int bag[TAPE_BAG];
    bag_of(bag, items, count);
    return rt_bag_servings(bag);
}

static void expect_servings(RtServings s, int hard, int karambwan, int brew) {
    assert(s.hard == hard && s.karambwan == karambwan && s.brew == brew);
}

static void test_servings(void) {
    /* Anglerfish and karambwan by their client ids, blighted or not; a brew counts its doses; a restore, a super combat and gear are not meals. */
    expect_servings(servings_of((int[]){ANGLERFISH, BLIGHTED_ANGLERFISH, KARAMBWAN, BLIGHTED_KARAMBWAN, BREW4, BREW1, -1, DRAGON_DEFENDER}, 8), 2, 2, 5);
    expect_servings(servings_of((int[]){BLIGHTED_RESTORE4, SUPER_COMBAT4, DRAGON_DEFENDER}, 3), 0, 0, 0);
    /* A pie is two servings, half a pie one (a bite is a meal), and the pie dish it leaves behind is not one. */
    expect_servings(servings_of((int[]){SUMMER_PIE}, 1), 2, 0, 0);
    expect_servings(servings_of((int[]){HALF_SUMMER_PIE}, 1), 1, 0, 0);
    expect_servings(servings_of((int[]){2313}, 1), 0, 0, 0);
    /* Moving an item to another slot changes no total. */
    RtServings a = servings_of((int[]){BLIGHTED_ANGLERFISH, -1, BREW3}, 3), b = servings_of((int[]){-1, BREW3, BLIGHTED_ANGLERFISH}, 3);
    assert(a.hard == b.hard && a.karambwan == b.karambwan && a.brew == b.brew);
    puts("servings: client ids, doses, pie bites, rearrangement");
}

static const RtSeen NO_BAR = {-1, 0, 0, 0, 0, 0, -1, -1, 0, 0};

static RtFight fight(void) {
    RtFight f;
    memset(&f, 0, sizeof(f));
    f.net = NULL;
    rt_fight_reset(&f, 100, &NO_BAR);
    return f;
}

static void test_triple_eat_counting(void) {
    RtFight f = fight();
    RtServings bag = {3, 2, 4};
    rt_note_meals(&f, &bag, 100, 50); /* first request of the fight: nothing to compare with */
    assert(f.meals == 0 && f.overeats == 0);
    /* Food, karambwan and brew all on one tick, from 100 hitpoints against a range of 50: three meals, one bar, three overeats. */
    bag = (RtServings){2, 1, 3};
    rt_note_meals(&f, &bag, 80, 50);
    assert(f.meals == 3 && f.overeats == 3 && f.bag_unexplained == 0);
    /* The request after it carries the same bag: no meal. */
    rt_note_meals(&f, &bag, 80, 50);
    assert(f.meals == 3 && f.overeats == 3);
    /* The tick began at 80, above 50: a double-eat (food and karambwan) is two more overeats. The tick after it began at 50 = range: edge, polite. */
    bag = (RtServings){1, 0, 3};
    rt_note_meals(&f, &bag, 50, 50);
    assert(f.meals == 5 && f.overeats == 5);
    bag = (RtServings){0, 0, 2};
    rt_note_meals(&f, &bag, 40, 50);
    assert(f.meals == 7 && f.overeats == 5);
    /* The next tick began at 40, under the range: a brew is polite. And the bar is the previous request's hitpoints, not the ones after the meal (60 > 50 here changes nothing). */
    bag = (RtServings){0, 0, 1};
    rt_note_meals(&f, &bag, 60, 50);
    assert(f.meals == 8 && f.overeats == 5);
    bag = (RtServings){0, 0, 0};
    rt_note_meals(&f, &bag, 70, 50);
    assert(f.meals == 9 && f.overeats == 6);
    /* Two servings of one class gone at once is not eating (a class has one delay clock): not a meal, and it is reported. A serving that appears is not one either. */
    bag = (RtServings){4, 0, 0};
    rt_note_meals(&f, &bag, 70, 50);
    assert(f.meals == 9 && f.overeats == 6 && f.bag_unexplained == 0);
    bag = (RtServings){1, 0, 0};
    rt_note_meals(&f, &bag, 70, 50);
    assert(f.meals == 9 && f.overeats == 6 && f.bag_unexplained == 1);
    /* A pie bite is one serving. */
    f = fight();
    bag = (RtServings){2, 0, 0};
    rt_note_meals(&f, &bag, 99, 60);
    bag = (RtServings){1, 0, 0};
    rt_note_meals(&f, &bag, 99, 60);
    assert(f.meals == 1 && f.overeats == 1);
    /* A new fight starts at zero. */
    rt_fight_reset(&f, 500, &NO_BAR);
    assert(f.meals == 0 && f.overeats == 0 && !f.have_previous);
    puts("triple-eat: one bar for the tick's meals, hp == range is polite, two servings of a class at once is not a meal");
}

/* ---- the traces ---- */
static int slot_code(int raw) { return rt_content_code(raw); }

static int is_meal_code(int code) { return rt_meal_class(code) != RT_MEAL_NONE; }

static HumanCommand click_slot(int slot) { return (HumanCommand){.kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK, .inventory_slot = slot}; }

static int timers_jumped(const Request* before, const Request* after) {
    return after->food_timer > (before->food_timer > 0 ? before->food_timer - 1 : 0) || after->potion_timer > (before->potion_timer > 0 ? before->potion_timer - 1 : 0) ||
        after->karambwan_timer > (before->karambwan_timer > 0 ? before->karambwan_timer - 1 : 0);
}

typedef struct { int requests, unavailable, fights, meals, overeats, polite, oracle_overeats, no_timer, unexplained, max_per_tick; } Totals;

static Totals replay(const char* path) {
    FILE* in = fopen(path, "r");
    if (!in) { perror(path); exit(2); }
    Totals t = {0};
    static Request q, last;
    static RiskfightState before;
    RtSeen seen;
    int new_fight, fight_start, obs_only;
    int have_last = 0, fight_meals = 0, fight_overeats = 0, fight_oracle = 0, fight_requests = 0, fight_index = -1, last_start = -1000000, fight_unexplained = 0;
    while (rt_parse(in, &q, &seen, &new_fight, &fight_start, &obs_only)) {
        request_declare_priors(&q);
        const char* unavailable = server_unavailable(&q);
        if (unavailable) { t.unavailable++; continue; }
        RtRecord records[GS_WIRE_MAX_RECORDS];
        int count;
        rt_decide(&q, &seen, new_fight, fight_start, records, &count);
        t.requests++;
        RiskfightState* s = &g_base.state;
        int fresh = g_fight.start_tick != last_start;
        if (fresh) {
            if (fight_index >= 0) printf("  fight %d: %d requests, meals %d, overeats %d (sim oracle %d)\n", fight_index, fight_requests, fight_meals, fight_overeats, fight_oracle);
            fight_index++;
            t.fights++;
            fight_meals = fight_overeats = fight_oracle = fight_requests = fight_unexplained = 0;
            assert(g_fight.meals == 0 && g_fight.overeats == 0);
        }
        fight_requests++;
        /* The session block as the net reads it. */
        float obs[RF_OBS_SIZE];
        riskfight_write_observation(s, 0, obs);
        assert(obs[RF_SESSION_START + 4] == 0);
        assert(fabsf(obs[RF_SESSION_START + 5] - fminf(1.0f, (float)g_fight.overeats / (RF_OVEREAT_PATIENCE + 1))) < 1e-6f);
        assert(s->overeats[0] == g_fight.overeats);
        int range = riskfight_chance_range(s, 0);
        assert(range == g_fight.previous_range && s->env.players[0].current_hitpoints == g_fight.previous_hitpoints);
        assert(fabsf(obs[RF_SESSION_START + 6] - fmaxf(-1.0f, fminf(1.0f, (float)(s->env.players[0].current_hitpoints - range) / 121.0f))) < 1e-6f);
        if (!fresh && have_last) {
            /* The oracle: the slots that held a meal and changed since the previous request, clicked into the sim state that request left (the sim state before this tick's meals). */
            int hard_slot = -1, brew_slot = -1, karambwan_slot = -1, meals = 0;
            for (int slot = 0; slot < TAPE_BAG; slot++) {
                int previous = slot_code(last.bag[slot]);
                if (last.bag[slot] == q.bag[slot] || !is_meal_code(previous)) continue;
                meals++;
                switch (rt_meal_class(previous)) {
                    case RT_MEAL_HARD: hard_slot = slot; break;
                    case RT_MEAL_BREW: brew_slot = slot; break;
                    default: karambwan_slot = slot; break;
                }
            }
            int meals_seen = g_fight.meals - fight_meals;
            assert(meals_seen == meals);
            assert(meals <= 3);
            RiskfightState* sim = &before;
            int hitpoints_at_start = before.env.players[0].current_hitpoints, range_at_start = riskfight_chance_range(&before, 0);
            sim->overeats[0] = 0;
            sim->tick_hitpoints[0] = hitpoints_at_start;
            int slots[3] = {hard_slot, brew_slot, karambwan_slot};
            for (int i = 0; i < 3; i++) {
                if (slots[i] < 0) continue;
                Player* p = &sim->env.players[0];
                p->food_timer = p->potion_timer = p->karambwan_timer = 0;
                HumanCommand c = click_slot(slots[i]);
                assert(riskfight_execute_command(sim, &g_base.context, 0, &c) && "the sim ate the meal the bag shows gone");
            }
            int oracle = sim->overeats[0];
            int by_definition = hitpoints_at_start > range_at_start ? meals : 0; /* strictly above, at the start of the tick */
            assert(oracle == by_definition);
            assert(g_fight.overeats - fight_overeats == oracle);
            fight_oracle += oracle;
            t.oracle_overeats += oracle;
            if (meals > t.max_per_tick) t.max_per_tick = meals;
            if (meals && !timers_jumped(&last, &q)) t.no_timer++;
            t.meals += meals;
            t.overeats += oracle;
            t.polite += meals - oracle;
        }
        fight_meals = g_fight.meals;
        fight_overeats = g_fight.overeats;
        t.unexplained += g_fight.bag_unexplained - fight_unexplained;
        fight_unexplained = g_fight.bag_unexplained;
        memcpy(&before, s, sizeof(before));
        last = q;
        have_last = 1;
        last_start = g_fight.start_tick;
    }
    printf("  fight %d: %d requests, meals %d, overeats %d (sim oracle %d)\n", fight_index, fight_requests, fight_meals, fight_overeats, fight_oracle);
    assert(t.overeats == t.oracle_overeats);
    fclose(in);
    return t;
}

int main(int argc, char** argv) {
    g_fight.net = NULL;
    g_fight.rng = 0x9e3779b9u;
    g_fight.hard = 1;
    riskfight_deployable = 1;
    g_ablation.hp = HP_UNIFORM; g_ablation.escape_model = 1; g_ablation.his_defence = 99; g_ablation.panic = 0.8; g_ablation.panic_hp = 25;
    observer_register_items();
    sim_init(&g_base);
    test_servings();
    test_triple_eat_counting();
    const char* fixture[] = {"ocean/osrs/tests/fixtures/live_swings.requests", "/tmp/t4.requests"};
    int explicit_paths = argc > 1;
    int ran = 0;
    for (int i = 0; i < (explicit_paths ? argc - 1 : 2); i++) {
        const char* path = explicit_paths ? argv[i + 1] : fixture[i];
        if (!explicit_paths && i > 0 && access(path, R_OK) != 0) continue; /* the second default is optional */
        printf("%s\n", path);
        Totals t = replay(path);
        printf("  %d requests (%d unavailable), %d fights: %d meals, %d overeats, %d polite; most meals on one tick %d; meals without a timer jump %d; bag losses that were no meal %d\n", t.requests,
            t.unavailable, t.fights, t.meals, t.overeats, t.polite, t.max_per_tick, t.no_timer, t.unexplained);
        assert(t.requests > 0 && t.meals > 0);
        ran++;
    }
    assert(ran > 0);
    puts("Live overeat contracts passed");
    return 0;
}
