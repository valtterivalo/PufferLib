/* The timing block (RF_TIMING_START, riskfight_write_timing_observation) at its boundaries, through the sim's own viewer (riskfight_observe_visible; the live server's
 * rt_observe calls the same riskfight_his_swing_seen / riskfight_meal_seen / riskfight_meals_resolve):
 * - after a 5-tick swing his_ready_min reads 5, 4, 3, 2, 1 (1: he can swing on the tick about to be played), then 0;
 * - a meal seen on a running timer raises his_ready_max by the most a tick's meals add (5) from its eat tick, and his_ready_min not at all (it may be a drink); once
 *   his bar shows the heal, a food meal (a pie bite, 11) raises both by its delay, 3, and a karambwan (18, from 45 hitpoints: the bar reads a heal of 15 to 19, a
 *   brew's 16 or a karambwan's) raises ready_max by its delay, 2, and ready_min by none; a meal after his clock ran out leaves him ready (the continuous clock);
 * - his_next_tick_max is the chance range (capped at 128) while ready_min <= 1; above it the normal opening is dropped and the affordable instant maul specials stay,
 *   with the echo;
 * - his hitpoints bounds from bar 30, 15 and no bar, in the threat block's damage scale.
 *   cc -std=gnu11 -O2 -I. -o /tmp/test_riskfight_timing_obs ocean/osrs/tests/test_riskfight_timing_obs.c -lm      (cwd = repo root) */
#include <assert.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"

static RiskfightState state;
static RiskfightContext context;

static Player* him(void) { return &state.env.players[1]; }
static RiskfightVisibleOpponent* seen(void) { return &state.visible[0]; }

static const float* timing(void) {
    static float obs[RF_OBS_SIZE];
    riskfight_write_observation(&state, 0, obs);
    return obs + RF_TIMING_START;
}
static int ready_min(void) { return (int)lroundf(timing()[0] * RF_OBSERVATION_ATTACK_AGE_SCALE); }
static int ready_max(void) { return (int)lroundf(timing()[1] * RF_OBSERVATION_ATTACK_AGE_SCALE); }
static int next_tick_max(void) { return (int)lroundf(timing()[2] * RF_THREAT_DAMAGE_SCALE); }

/** One tick played with what seat 0 sees of him on it (a meal animation, a hitsplat for 0 that redraws his bar), then the observation of the next tick. */
static void play(int ate_food, int ate_karambwan, int hit) {
    Player* p = him();
    p->attack_style_this_tick = ATTACK_STYLE_NONE;
    p->ate_food_this_tick = ate_food;
    p->ate_karambwan_this_tick = ate_karambwan;
    p->hit_landed_this_tick = hit;
    p->hit_damage = 0;
    p->cast_veng_this_tick = p->said_taste_vengeance_this_tick = 0;
    state.env.tick++;
    riskfight_observe_visible(&state, 0, 0);
}

/** His normal swing with a weapon of `speed` on the tick about to be played (what riskfight_record_attack notes), then that tick played. */
static void swing(int speed) {
    riskfight_his_swing_seen(seen(), state.env.tick, speed);
    play(0, 0, 0);
}

static void reset(int hitpoints) {
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 11);
    state.inventory_use[1].potion_animation_tick_plus_one = 0;
    him()->current_hitpoints = hitpoints;
    play(0, 0, 1); /* his bar shows his hitpoints */
    assert(ready_min() == 0 && ready_max() == 0); /* no swing seen: ready */
}

static void test_swing(void) {
    reset(99);
    swing(5);
    for (int left = 5; left >= 1; left--) {
        assert(ready_min() == left && ready_max() == left);
        play(0, 0, 0);
    }
    assert(ready_min() == 0 && ready_max() == 0);
    play(0, 0, 0);
    assert(ready_min() == 0 && ready_max() == 0);
    printf("swing: a 5-tick swing reads 5 4 3 2 1, then 0\n");
}

/** From `hitpoints`, a meal of `heal` on the second tick after his 5-tick swing (4 ticks left), its heal shown by a 0 hitsplat a tick later. Returns ready_min and
 * ready_max after it. */
static void meal_on_running_timer(int hitpoints, int karambwan, int heal, int* lo, int* hi) {
    reset(hitpoints);
    swing(5);
    assert(ready_min() == 5);
    him()->current_hitpoints += heal; /* the engine heals at once; his bar redraws only with a hitsplat */
    play(!karambwan, karambwan, 0);
    assert(ready_min() == 4 && ready_max() == 4 + 5); /* the eat tick: any meal of a tick, the most adding 5 (food + karambwan), the least 0 (a drink) */
    play(0, 0, 1);
    *lo = ready_min() + 1;
    *hi = ready_max() + 1; /* as of the tick before, to compare with the 4 left then */
}

static void test_meals(void) {
    int lo, hi;
    meal_on_running_timer(50, 0, osrs_food_heal_amount(FOOD_SUMMER_PIE), &lo, &hi);
    assert(lo == 4 + osrs_food_attack_delay_ticks(FOOD_SUMMER_PIE) && hi == lo);
    printf("meals: a pie bite (heal 11) on 4 ticks left: ready 4-9 on its eat tick, %d-%d once the bar shows it\n", lo, hi);
    meal_on_running_timer(45, 1, osrs_food_heal_amount(FOOD_KARAMBWAN), &lo, &hi);
    assert(lo == 4 && hi == 4 + osrs_food_attack_delay_ticks(FOOD_KARAMBWAN));
    printf("meals: a karambwan (heal 18) on 4 ticks left: ready 4-9 on its eat tick, %d-%d once the bar shows it (a brew's 16 fits it too)\n", lo, hi);
    /* His clock long run out: a meal adds its delay to the signed clock and leaves him ready. */
    reset(50);
    swing(5);
    for (int i = 0; i < 10; i++) play(0, 0, 0);
    him()->current_hitpoints += osrs_food_heal_amount(FOOD_SUMMER_PIE);
    play(1, 0, 0);
    assert(ready_min() == 0 && ready_max() == 0);
    /* A new swing forgets the meals before it. */
    swing(4);
    assert(ready_min() == 4 && ready_max() == 4);
    printf("meals: a meal 10 ticks after a 5-tick swing leaves him ready; a new swing forgets it\n");
}

static void test_next_tick(void) {
    reset(99);
    RiskfightVisibleOpponent* v = seen();
    riskfight_threat_note_swing(&v->threat, ITEM_GRANITE_MAUL_ORNATE, state.env.tick - 1, 1); /* a maul special seen: the maul is his */
    v->threat.valid = 0;
    riskfight_threat_refresh(&v->threat, v->equipment, state.env.tick);
    v->veng_up = 0;
    v->spec_est = 100;
    float obs[RF_OBS_SIZE];
    riskfight_write_observation(&state, 0, obs);
    const float* threat = obs + RF_THREAT_START;
    int maul_spec = 0;
    for (int i = 0; i < RF_THREAT_HIS_SLOTS; i++) {
        const float* o = threat + i * RF_THREAT_HIS_WIDTH;
        if (o[0] > 0 && lroundf(o[8] * 100) == 50 && lroundf(o[11] * RF_THREAT_SPEED_SCALE) == 1) maul_spec = (int)lroundf(o[9] * RF_THREAT_DAMAGE_SCALE);
    }
    assert(maul_spec > 0);
    int range = riskfight_chance_range_of(threat, 100, 0);
    assert(range >= 2 * maul_spec); /* the larger of his opening hit and the two maul specials: with only the maul, the specials */
    const int read_range = range < RF_THREAT_DAMAGE_SCALE ? range : RF_THREAT_DAMAGE_SCALE; /* the block caps at 1 */
    swing(5);
    for (int left = 5; left >= 2; left--) {
        assert(ready_min() == left && next_tick_max() == 2 * maul_spec); /* not ready: the two ornate maul specials 100 energy pays, no opening hit */
        play(0, 0, 0);
    }
    assert(ready_min() == 1 && next_tick_max() == read_range);
    play(0, 0, 0);
    assert(ready_min() == 0 && next_tick_max() == read_range);
    swing(5);
    v->spec_est = 49;
    assert(next_tick_max() == 0); /* not ready and no special affordable */
    v->spec_est = 50;
    assert(next_tick_max() == maul_spec);
    v->veng_up = 1;
    int echo = (int)lroundf(threat[RF_THREAT_OURS_OFFSET + 1] * RF_THREAT_DAMAGE_SCALE) * 3 / 4;
    assert(echo > 0 && next_tick_max() == maul_spec + echo);
    printf("next tick: chance range %d (reads %d) while ready <= 1; %d (two maul specials of %d) at ready 2-5, %d with 50 energy, 0 with 49, + echo %d with his Vengeance up\n",
        range, read_range, 2 * maul_spec, maul_spec, maul_spec, echo);
}

static void test_hitpoints(void) {
    reset(99);
    const int bars[3] = {30, 15, -1}, lower[3] = {99, 48, 99}, upper[3] = {121, 51, 121};
    for (int i = 0; i < 3; i++) {
        seen()->health_bar = bars[i];
        const float* t = timing();
        assert(lroundf(t[3] * RF_THREAT_DAMAGE_SCALE) == lower[i] && lroundf(t[4] * RF_THREAT_DAMAGE_SCALE) == upper[i]);
        assert(t[3] == (float)lower[i] / RF_THREAT_DAMAGE_SCALE && t[4] == (float)upper[i] / RF_THREAT_DAMAGE_SCALE);
    }
    printf("hitpoints: bar 30 reads 99-121, bar 15 48-51, no bar 99-121\n");
}

int main(void) {
    test_swing();
    test_meals();
    test_next_tick();
    test_hitpoints();
    puts("riskfight timing observation: all passed");
    return 0;
}
