/* The two blatant mistakes riskfight_write_action_mask removes (riskfight_threat_gates), at their boundaries, with the mask key (riskfight_mask_key) following each:
 * - a meal (food, karambwan, brew dose) is offered at hitpoints == his chance range, not at range + 1;
 * - special action k (k SPEC_TOGGLE clicks): with a granite maul k specials, offered when k x its spec max + the most our hits in flight can deal == the lower bound
 *   of his hitpoints from his bar, not one below (99 with no bar known); with any other weapon (Voidwaker) the clicks flip the armed bar: an action that leaves it
 *   armed is gated on one spec max the same way, one that leaves it disarmed (a disarm, the double click that changes nothing) is always offered;
 * - a direct hit of ours in flight counts at its max, never at its rolled damage;
 * - his Vengeance adds the echo of our best hit to the range only after a seen cast of his that has not proc'd.
 *   cc -std=gnu11 -O2 -I. -o /tmp/test_riskfight_threat_masks ocean/osrs/tests/test_riskfight_threat_masks.c -lm      (cwd = repo root) */
#include <assert.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"

static RiskfightState state;
static RiskfightContext context;
static unsigned char cached[RF_MASK_SIZE];
static RiskfightMaskKey held;

enum { SLOT_SHARK, SLOT_KARAMBWAN, SLOT_BREW };

static Player* us(void) { return &state.env.players[0]; }
static RiskfightVisibleOpponent* him(void) { return &state.visible[0]; }

/** Seat 0 rested with a shark, a karambwan and a brew in the bag, nothing in flight, his Vengeance never seen. */
static void reset(void) {
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 11);
    Player* p = us();
    p->food_timer = p->potion_timer = p->karambwan_timer = 0;
    p->pvp_overheal_block = 0;
    p->num_pending_hits = 0;
    memset(state.env.pending_hit_metadata[0], 0, sizeof(state.env.pending_hit_metadata[0]));
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++)
        if (osrs_inventory_cell_metadata(&p->inventory_cells[slot])->gear_slot != GEAR_SLOT_WEAPON) p->inventory_cells[slot] = osrs_inventory_cell_empty();
    p->inventory_cells[SLOT_SHARK] = osrs_inventory_cell_from_content_code(riskfight_supply_code(OSRS_CONSUMABLE_SHARK_FOOD, 0));
    p->inventory_cells[SLOT_KARAMBWAN] = osrs_inventory_cell_from_content_code(riskfight_supply_code(OSRS_CONSUMABLE_KARAMBWAN, 0));
    p->inventory_cells[SLOT_BREW] = osrs_inventory_cell_from_content_code(riskfight_supply_code(OSRS_CONSUMABLE_BREW, 4));
    him()->last_cast_tick = him()->last_proc_tick = -1000;
    memset(&held, 0, sizeof(held));
}

/** The fresh mask, checked against the keyed one (re-written only on a new key). */
static const unsigned char* mask(void) {
    static unsigned char fresh[RF_MASK_SIZE];
    riskfight_write_action_mask(&state, 0, fresh);
    riskfight_write_action_mask_keyed(&state, 0, &held, cached);
    assert(!memcmp(fresh, cached, RF_MASK_SIZE) && "the mask key missed an input");
    return fresh;
}

/** Whether the mask offers the meal of physical `slot` on its head. */
static int meal_offered(int slot) {
    const unsigned char* m = mask();
    const OsrsItemContentMetadata* meta = osrs_inventory_cell_metadata(&us()->inventory_cells[slot]);
    int head = meta->click_action == OSRS_CLICK_DRINK ? RF_DRINK : meta->consumable_kind == OSRS_CONSUMABLE_KARAMBWAN ? RF_COMBO : RF_FOOD;
    const RiskfightBagOrder order = riskfight_bag_order(us());
    for (int entry = 0; entry < OSRS_INVENTORY_SIZE; entry++)
        if (order.slot[entry] == slot) return m[RF_HEAD_OFFSET[head] + entry + 1];
    abort();
}

static int all_meals_offered(void) {
    int shark = meal_offered(SLOT_SHARK), karambwan = meal_offered(SLOT_KARAMBWAN), brew = meal_offered(SLOT_BREW);
    assert(shark == karambwan && karambwan == brew);
    return shark;
}

static void test_meals(void) {
    reset();
    us()->current_hitpoints = 50;
    int range = riskfight_chance_range(&state, 0);
    assert(range > 1 && range < 98); /* all three meals heal there */
    us()->current_hitpoints = range;
    assert(riskfight_chance_range(&state, 0) == range);
    assert(all_meals_offered());
    us()->current_hitpoints = range + 1;
    assert(riskfight_chance_range(&state, 0) == range);
    assert(!all_meals_offered());
    printf("meals: his chance range %d; offered at hp %d, not at %d\n", range, range, range + 1);
}

static void test_vengeance(void) {
    reset();
    us()->current_hitpoints = 50;
    RiskfightVisibleOpponent* v = him();
    int unseen = riskfight_chance_range(&state, 0);
    v->last_cast_tick = v->last_proc_tick = 5;
    assert(riskfight_chance_range(&state, 0) == unseen); /* a tie is no cast seen since the proc */
    v->last_proc_tick = 4;
    int up = riskfight_chance_range(&state, 0);
    float threat[RF_THREAT_SIZE];
    RiskfightBagWeapons none = {{0}, 0};
    riskfight_threat_write(us(), &none, &v->threat, v->health_bar, threat);
    int echo = (int)lroundf(threat[RF_THREAT_OURS_OFFSET + 1] * RF_THREAT_DAMAGE_SCALE) * 3 / 4;
    assert(echo > 0 && up == unseen + echo);
    /* The meal gate follows: at hp == the range with the echo, a meal only after his cast. */
    assert(up < 98);
    us()->current_hitpoints = up;
    assert(all_meals_offered());
    v->last_proc_tick = 6; /* it proc'd: down again */
    assert(riskfight_chance_range(&state, 0) == unseen && !all_meals_offered());
    v->last_cast_tick = v->last_proc_tick = -1000; /* nothing seen, the start of a fight */
    assert(riskfight_chance_range(&state, 0) == unseen && !all_meals_offered());
    v->last_cast_tick = 7;
    assert(riskfight_chance_range(&state, 0) == up && all_meals_offered());
    printf("vengeance: range %d before a seen cast (and on a tie, after a proc), %d = %d + echo %d after it\n", unseen, up, unseen, echo);
}

/** Whether special action k is offered. */
static int spec_offered(int k) { return mask()[RF_HEAD_OFFSET[RF_SPECIAL] + k]; }

/** One recoil hit of ours in flight: its damage is seen, so it counts as it is; it tunes the in-flight reach to the point. `damage` 0: nothing in flight. */
static void seen_in_flight(int damage) {
    Player* p = us();
    p->num_pending_hits = damage > 0;
    p->pending_hits[0] = (PendingHit){.damage = damage, .ticks_until_hit = 1, .attack_type = ATTACK_STYLE_NONE, .hit_success = 1, .kind = OSRS_HIT_RECOIL};
    state.env.pending_hit_metadata[0][0] = (PendingHitMetadata){0};
}

/** One direct hit of ours in flight whose roll came out `rolled` (hidden until the hitsplat). */
static void direct_in_flight(int rolled, int special) {
    Player* p = us();
    p->num_pending_hits = 1;
    p->pending_hits[0] = (PendingHit){.damage = rolled, .ticks_until_hit = 1, .attack_type = ATTACK_STYLE_MELEE, .is_special = special, .hit_success = rolled > 0,
        .kind = OSRS_HIT_DIRECT};
    state.env.pending_hit_metadata[0][0] = (PendingHitMetadata){0};
}

/** His hitpoints' lower bound from bar `bar` (-1: no bar, 99). */
static int lower_of(int bar) {
    OsrsHealthBarRange range = osrs_health_bar_range(bar, OSRS_PLAYER_HEALTH_BAR_SCALE, RF_THREAT_BASE_HITPOINTS, RF_THREAT_OVERHEAL_CAP);
    assert(range.kind == (bar < 0 ? OSRS_HEALTH_BAR_MISSING : OSRS_HEALTH_BAR_KNOWN));
    return bar < 0 ? 99 : range.lower;
}

/** Action k is offered exactly from `reach_needed` in flight (offered with nothing when it is below 1). */
static void expect_boundary(int k, int reach_needed) {
    if (reach_needed < 1) { seen_in_flight(0); assert(spec_offered(k)); return; }
    seen_in_flight(reach_needed);
    assert(spec_offered(k));
    seen_in_flight(reach_needed - 1);
    assert(!spec_offered(k));
}

/** The spec max is the threat block's OURS spec_max of the weapon the special would use: the wielded one's row, else its bag row. */
static void expect_ours_row(RiskfightSpecWeapon spec) {
    Player* p = us();
    RiskfightBagWeapons bag = {{0}, 0};
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        const OsrsItemContentMetadata* m = osrs_inventory_cell_metadata(&p->inventory_cells[slot]);
        if (m->gear_slot == GEAR_SLOT_WEAPON && m->item_idx != p->equipped[GEAR_SLOT_WEAPON]) riskfight_threat_note_bag_weapon(&bag, m->item_idx);
    }
    int row = spec.item == p->equipped[GEAR_SLOT_WEAPON] ? 0 : -1;
    for (int i = 0; i < bag.count; i++) if (bag.item[i] == spec.item) row = i + 1;
    assert(row >= 0);
    float threat[RF_THREAT_SIZE];
    for (int bar = -1; bar <= OSRS_PLAYER_HEALTH_BAR_SCALE; bar++) {
        if (bar == 0) continue;
        him()->health_bar = bar;
        riskfight_threat_write(p, &bag, &him()->threat, bar, threat);
        assert(riskfight_threat_our_maxes(p, spec.item, bar).spec_max == (int)lroundf(threat[RF_THREAT_OURS_OFFSET + row * RF_THREAT_OURS_WIDTH + 5] * RF_THREAT_DAMAGE_SCALE));
    }
}

/** Voidwaker (a special the clicks arm and disarm): the action's armed-after state decides. */
static void test_toggled_special(void) {
    reset();
    Player* p = us();
    p->special_energy = 100;
    RiskfightSpecWeapon spec = riskfight_spec_weapon(p);
    assert(spec.item == ITEM_VOIDWAKER && !pvp_is_maul(spec.item));
    expect_ours_row(spec);
    for (int bar = -1; bar <= OSRS_PLAYER_HEALTH_BAR_SCALE; bar++) {
        if (bar == 0) continue;
        him()->health_bar = bar;
        int spec_max = riskfight_threat_our_maxes(p, spec.item, bar).spec_max, need = lower_of(bar) - spec_max;
        assert(spec_max > 0);
        p->spec_armed = 0;
        expect_boundary(1, need); /* arms it: one spec max */
        seen_in_flight(0);
        assert(spec_offered(2)); /* two clicks leave it disarmed: a no-op, always offered */
        p->spec_armed = 1;
        assert(spec_offered(1)); /* the disarm */
        expect_boundary(2, need); /* leaves it armed: one spec max, not two */
    }
    him()->health_bar = -1;
    seen_in_flight(0);
    int spec_max = riskfight_threat_our_maxes(p, spec.item, -1).spec_max;
    assert(spec_max < 99 && 2 * spec_max >= 99); /* the case the old k x spec max let through */
    p->spec_armed = 1;
    assert(spec_offered(1) && !spec_offered(2));
    p->spec_armed = 0;
    assert(!spec_offered(1) && spec_offered(2));
    printf("toggled (%s, spec max %d, no bar: his hp >= 99, nothing in flight): armed: disarm offered, re-arm (2) not; disarmed: arm not, the no-op (2) offered\n",
        ITEM_DATABASE[spec.item].name, spec_max);
    /* Energy still gates: the double click needs the energy of two. */
    p->special_energy = spec.cost;
    him()->health_bar = 1;
    assert(spec_offered(1) && !spec_offered(2));
}

/** The ornate granite maul (an instant special per click): k clicks are k specials. */
static void test_instant_special(void) {
    reset();
    Player* p = us();
    p->special_energy = 100;
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++)
        if (osrs_inventory_cell_metadata(&p->inventory_cells[slot])->gear_slot == GEAR_SLOT_WEAPON) p->inventory_cells[slot] = osrs_inventory_cell_empty();
    p->inventory_cells[OSRS_INVENTORY_SIZE - 1] = osrs_inventory_cell_from_item(ITEM_GRANITE_MAUL_ORNATE);
    RiskfightSpecWeapon spec = riskfight_spec_weapon(p);
    assert(spec.item == ITEM_GRANITE_MAUL_ORNATE && 2 * spec.cost <= 100);
    expect_ours_row(spec);
    for (int armed = 0; armed < 2; armed++) {
        p->spec_armed = armed; /* the armed bar plays no part */
        for (int bar = -1; bar <= OSRS_PLAYER_HEALTH_BAR_SCALE; bar++) {
            if (bar == 0) continue;
            him()->health_bar = bar;
            int spec_max = riskfight_threat_our_maxes(p, spec.item, bar).spec_max;
            assert(spec_max > 0);
            for (int k = 1; k < RF_ACTION_DIMS[RF_SPECIAL]; k++) expect_boundary(k, lower_of(bar) - k * spec_max);
        }
    }
    him()->health_bar = -1;
    int spec_max = riskfight_threat_our_maxes(p, spec.item, -1).spec_max;
    printf("instant (%s, spec max %d, no bar): k specials offered from k x %d + in flight == 99: k=1 from %d in flight, k=2 from %d\n",
        ITEM_DATABASE[spec.item].name, spec_max, spec_max, 99 - spec_max, 99 - 2 * spec_max);
}

/** A direct hit in flight counts at its max whatever it rolled: a miss and a high roll give the same reach and the same mask. */
static void test_in_flight_is_the_max(void) {
    reset();
    Player* p = us();
    p->special_energy = 100;
    p->spec_armed = 0;
    him()->health_bar = -1;
    RiskfightOurMaxes most = riskfight_threat_our_maxes(p, p->equipped[GEAR_SLOT_WEAPON], -1);
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        const OsrsItemContentMetadata* m = osrs_inventory_cell_metadata(&p->inventory_cells[slot]);
        if (m->gear_slot != GEAR_SLOT_WEAPON) continue;
        RiskfightOurMaxes w = riskfight_threat_our_maxes(p, m->item_idx, -1);
        if (w.max > most.max) most.max = w.max;
        if (w.spec_max > most.spec_max) most.spec_max = w.spec_max;
    }
    int spec_max = riskfight_threat_our_maxes(p, riskfight_spec_weapon(p).item, -1).spec_max;
    assert(spec_max < 99 && spec_max + most.max >= 99); /* arming reaches only through the hit in flight */
    int offered = -1;
    for (int rolled = 0; rolled <= most.max; rolled += most.max) {
        direct_in_flight(rolled, 0);
        assert(riskfight_in_flight_max(&state, 0) == most.max);
        int now = spec_offered(1);
        assert(offered < 0 || now == offered);
        offered = now;
        direct_in_flight(rolled, 1);
        assert(riskfight_in_flight_max(&state, 0) == most.spec_max);
    }
    assert(offered == 1);
    printf("in flight: a direct hit counts %d (our largest max), a special hit %d, rolled 0 or %d alike; arming at no bar offered either way\n", most.max, most.spec_max, most.max);
}

int main(void) {
    test_meals();
    test_vengeance();
    test_toggled_special();
    test_instant_special();
    test_in_flight_is_the_max();
    puts("test_riskfight_threat_masks: ok");
    return 0;
}
