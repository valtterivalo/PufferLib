#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "../encounters/encounter_riskfight.h"
#include "riskfight_motor.h"

/* Tick-exact regressions of mechanics measured live (calib README VALIDATED FACTS), one per mechanic the audit found wrong:
 * the consumable lock matrix with karambwan to karambwan at T+3, the motor clock of Fact 20, the maul's spec cost. Meal-before-damage by PID
 * (Fact 3) is pinned in test_riskfight_decision_phases.c. */

static RiskfightState state;
static RiskfightContext context;

enum { SLOT_KARB_A, SLOT_KARB_B, SLOT_HARD, SLOT_BREW_A, SLOT_BREW_B, SLOT_COUNT };

static uint16_t initial[SLOT_COUNT];

static void reset(void) {
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 7);
    state.env.pid_holder = 0;
    for (int side = 0; side < 2; side++) {
        Player* p = &state.env.players[side];
        p->current_hitpoints = 40;
        p->equipped[GEAR_SLOT_WEAPON] = ITEM_VOIDWAKER;
        osrs_refresh_player_equipment(p);
        p->attack_timer = p->attack_timer_uncapped = 100;
        p->has_attack_timer = 1;
        p->num_pending_hits = 0;
        osrs_interaction_clear(&p->interaction);
        for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) p->inventory_cells[slot] = osrs_inventory_cell_empty();
    }
    Player* us = &state.env.players[0];
    us->inventory_cells[SLOT_KARB_A] = us->inventory_cells[SLOT_KARB_B] =
        osrs_inventory_cell_from_content_code(riskfight_supply_code(OSRS_CONSUMABLE_KARAMBWAN, 0));
    us->inventory_cells[SLOT_HARD] = osrs_inventory_cell_from_content_code(riskfight_supply_code(OSRS_CONSUMABLE_ANGLERFISH, 0));
    us->inventory_cells[SLOT_BREW_A] = us->inventory_cells[SLOT_BREW_B] =
        osrs_inventory_cell_from_content_code(riskfight_supply_code(OSRS_CONSUMABLE_BREW, 4));
    for (int slot = 0; slot < SLOT_COUNT; slot++) initial[slot] = us->inventory_cells[slot].content_code;
    state.env.players[1].x = us->x + 1;
    state.env.players[1].y = us->y;
}

static void step(const int* slots, int n) {
    HumanCommand clicks[SLOT_COUNT];
    for (int i = 0; i < n; i++)
        clicks[i] = (HumanCommand){.kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK, .inventory_slot = slots[i]};
    HumanCommandQueue ours = {.items = clicks, .count = n};
    HumanCommandQueue theirs = {0};
    state.env.players[0].current_hitpoints = 40;
    riskfight_step_queues(&state, &context, &ours, &theirs);
}

/* A slot is consumed when its content changed: a meal empties it, a potion loses a dose. */
static int consumed(int slot) { return state.env.players[0].inventory_cells[slot].content_code != initial[slot]; }

/* Ticks after a first meal at T until a second consumable is processed (clicking it every tick), -1 within 8 ticks never. */
static int earliest_second(int first, int second) {
    reset();
    step(&first, 1);
    assert(consumed(first));
    for (int k = 1; k <= 8; k++) {
        step(&second, 1);
        if (consumed(second)) return k;
    }
    return -1;
}

static void test_lock_matrix(void) {
    /* Live (Fact 8; 3,898 click-matched meals, DeathAudit): after a karambwan nothing is consumed before T+3, karambwan included. */
    assert(earliest_second(SLOT_KARB_A, SLOT_KARB_B) == 3);
    assert(earliest_second(SLOT_KARB_A, SLOT_HARD) == 3);
    assert(earliest_second(SLOT_KARB_A, SLOT_BREW_A) == 3);
    assert(earliest_second(SLOT_BREW_A, SLOT_BREW_B) == 3);
    assert(earliest_second(SLOT_BREW_A, SLOT_HARD) == 3);
    /* Pairs the game allows a tick apart: hard food leaves potions and karambwan free, a potion leaves karambwan free. */
    assert(earliest_second(SLOT_HARD, SLOT_BREW_A) == 1);
    assert(earliest_second(SLOT_HARD, SLOT_KARB_A) == 1);
    assert(earliest_second(SLOT_BREW_A, SLOT_KARB_A) == 1);
    /* The same-tick combos stay legal: hard food, karambwan, brew in one tick. */
    int combo[] = {SLOT_HARD, SLOT_BREW_A, SLOT_KARB_A}; /* karambwan locks potions, so the potion goes before it */
    reset();
    step(combo, 3);
    assert(consumed(SLOT_HARD) && consumed(SLOT_KARB_A) && consumed(SLOT_BREW_A));
    int brew_karb[] = {SLOT_BREW_A, SLOT_KARB_A};
    reset();
    step(brew_karb, 2);
    assert(consumed(SLOT_BREW_A) && consumed(SLOT_KARB_A));
}

static void test_karambwan_not_at_t_plus_two(void) {
    reset();
    int first = SLOT_KARB_A, second = SLOT_KARB_B;
    step(&first, 1);
    for (int k = 1; k <= 2; k++) {
        step(&second, 1);
        assert(!consumed(SLOT_KARB_B));
    }
    step(&second, 1);
    assert(consumed(SLOT_KARB_B));
}

static void test_maul_costs(void) {
    assert(osrs_spec_cost(ITEM_GRANITE_MAUL) == 50);
    assert(osrs_spec_cost(ITEM_GRANITE_MAUL_ORNATE) == 50);
}

static OsrsPvpHitEvent seen[8];
static int seen_count;
static void collect(void* user, const OsrsPvpHitEvent* event) { (void)user; seen[seen_count++] = *event; }

/* Fact 14: 16 our hit, 43 our max hit, 17 any other player's, 12/13 blocks; echoes of ours are type 16. */
static void test_hitsplat_types(void) {
    assert(osrs_hitsplat_type(1, 20, 0) == 16 && osrs_hitsplat_type(1, 47, 1) == 43 && osrs_hitsplat_type(0, 47, 1) == 17);
    assert(osrs_hitsplat_type(1, 0, 0) == 12 && osrs_hitsplat_type(0, 0, 0) == 13);
    for (int source = 0; source < 2; source++) {
        reset();
        state.env.pvp_runtime.hit_observer = collect;
        seen_count = 0;
        Player* from = &state.env.players[source];
        from->num_pending_hits = 1;
        from->pending_hits[0] = (PendingHit){.damage = 20, .hit_success = 1, .attack_type = ATTACK_STYLE_MELEE};
        state.env.pending_hit_metadata[source][0] = (PendingHitMetadata){.damage_mode = 1, .rolled_max = 1};
        step(NULL, 0);
        const OsrsPvpHitEvent* direct = NULL;
        for (int i = 0; i < seen_count; i++)
            if (seen[i].kind == OSRS_HIT_DIRECT) { assert(!direct); direct = &seen[i]; }
        assert(direct && direct->source == source && direct->rolled_max == 1);
        assert(osrs_hitsplat_type(direct->source == 0, direct->damage, direct->rolled_max) == (source == 0 ? 43 : 17));
        state.env.pvp_runtime.hit_observer = NULL;
    }
}

/* Our own meals are visible 0.91 of the time when nothing else is shown, 0.48 behind a carried animation, 0.56 with a hit landing, never on a swing
 * tick (fit_meal_visibility.py); the old rule hid exactly the swing and hit ticks and showed every other meal. */
static double visible_share(int swing, int hit, int carry) {
    enum { N = 20000 };
    int visible = 0;
    for (int i = 0; i < N; i++) {
        reset();
        Player* p = &state.env.players[1];
        p->ate_food_this_tick = 1;
        p->attack_style_this_tick = swing ? ATTACK_STYLE_MELEE : ATTACK_STYLE_NONE;
        p->hit_landed_this_tick = hit;
        state.anim_carry[1] = (uint8_t)carry;
        state.script_seed[1] = osrs_lowbias32((uint32_t)i + 1);
        visible += riskfight_meal_visible(&state, 1);
    }
    return visible / (double)N;
}

static void test_meal_visibility(void) {
    assert(fabs(visible_share(0, 0, 0) - RF_MEAL_VISIBLE_QUIET) < 0.015);
    assert(fabs(visible_share(0, 0, 1) - RF_MEAL_VISIBLE_CARRY) < 0.015);
    assert(fabs(visible_share(0, 1, 0) - RF_MEAL_VISIBLE_HIT) < 0.015);
    assert(fabs(visible_share(0, 1, 1) - RF_MEAL_VISIBLE_HIT) < 0.015);
    assert(visible_share(1, 0, 0) == 0);
    reset();
    reset_tick_flags(&state.env.players[1]);
    state.env.players[1].hit_landed_this_tick = 0;
    state.env.tick = 5; /* potion_animation_tick_plus_one == 0 means none, which equals tick 0 */
    assert(!riskfight_meal_visible(&state, 1)); /* no meal this tick */
    /* The carry flag follows the previous tick: a swing on tick T shows as carry on T+1 and not on T+2. */
    reset();
    state.env.players[1].attack_timer = state.env.players[1].attack_timer_uncapped = 0;
    state.anim_now[1] = 1;
    step(NULL, 0);
    assert(state.anim_carry[1] == 1);
    step(NULL, 0);
    assert(state.anim_carry[1] == state.anim_now[1] && state.anim_now[1] == 0);
}

static uint32_t rng = 12345;
static double uniform(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng >> 8) / 16777216.0; }
static int compare(const void* a, const void* b) { double x = *(const double*)a, y = *(const double*)b; return (x > y) - (x < y); }

static void check_quantiles(double* values, int n, double p50, double p90, double tolerance) {
    qsort(values, (size_t)n, sizeof(double), compare);
    double q50 = values[n / 2], q90 = values[(int)(0.9 * n)];
    assert(fabs(q50 - p50) <= tolerance * p50 && fabs(q90 - p90) <= tolerance * p90);
}

static void test_motor_clock(void) {
    enum { N = 200000 };
    static double commit[5][N];
    int third_before_400 = 0, fifth_before_858 = 0, first_before_405 = 0, second_before_405 = 0, three_in_one_tick = 0;
    for (int i = 0; i < N; i++) {
        double ms = 0;
        int tick[5];
        for (int k = 0; k < 5; k++) {
            ms += motor_gap_ms(uniform(), k);
            commit[k][i] = ms;
            tick[k] = motor_land_tick(0, ms);
        }
        third_before_400 += commit[2][i] < 400;
        fifth_before_858 += commit[4][i] < 858;
        first_before_405 += commit[0][i] < 405;
        second_before_405 += commit[1][i] < 405;
        three_in_one_tick += tick[0] == tick[1] && tick[1] == tick[2];
    }
    /* Fact 20: k-th click p50/p90 261/343, 437/620, 639/898, 841/1141, 1161/1417 ms after the tick start. */
    const double p50[] = {261, 437, 639, 841, 1161}, p90[] = {343, 620, 898, 1141, 1417};
    for (int k = 0; k < 5; k++) check_quantiles(commit[k], N, p50[k], p90[k], 0.07);
    assert(third_before_400 < N * 0.005);   /* live: 0 of 757 */
    assert(fifth_before_858 < N * 0.005);   /* live: 0 of 114 */
    assert(fabs(first_before_405 / (double)N - 0.96) < 0.02);
    assert(fabs(second_before_405 / (double)N - 0.37) < 0.04);
    /* Three clicks processed by one tick: 33% under the old 130+U(0,330)/80+U(0,90) clock, 0 of 757 live below 400 ms; the fitted tail between 400 and
     * the 465 ms cutoff leaves about 3% (P(third < 465)), an extrapolation the live count cannot confirm or refute. */
    assert(three_in_one_tick < N * 0.04);
    /* The server takes a click for the next tick only when it arrives before 465 ms. */
    assert(motor_land_tick(10, 464.9) == 11 && motor_land_tick(10, 465.0) == 12 && motor_land_tick(10, 465.0 + 599.9) == 12 &&
        motor_land_tick(10, 465.0 + 600.0) == 13);
}

int main(void) {
    riskfight_init_context((EncounterContext*)&context);
    riskfight_finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    test_lock_matrix();
    test_karambwan_not_at_t_plus_two();
    test_maul_costs();
    test_hitsplat_types();
    test_meal_visibility();
    test_motor_clock();
    riskfight_destroy_context((EncounterContext*)&context);
    puts("Riskfight live-fact regressions passed");
    return 0;
}
