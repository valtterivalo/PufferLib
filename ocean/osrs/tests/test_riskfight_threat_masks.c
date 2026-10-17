/* The two blatant mistakes riskfight_write_action_mask removes (riskfight_threat_gates), at their boundaries, with the mask key (riskfight_mask_key) following each:
 * - a meal (food, karambwan, brew dose) is offered at hitpoints == his chance range, not at range + 1;
 * - k specials are offered when k x the spec max of the weapon they would use + our hits in flight == the lower bound of his hitpoints from his bar, not one below;
 *   with no bar known the bound is 99;
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

/** Whether k specials are offered. */
static int spec_offered(int k) { return mask()[RF_HEAD_OFFSET[RF_SPECIAL] + k]; }

/** One hit of ours in flight: `damage` as the sim carries it, or (uncertain) the top of a live range. */
static void in_flight(int damage, int uncertain) {
    Player* p = us();
    p->num_pending_hits = 1;
    p->pending_hits[0] = (PendingHit){.damage = uncertain ? 0 : damage, .ticks_until_hit = 1, .attack_type = ATTACK_STYLE_MELEE, .hit_success = 1};
    state.env.pending_hit_metadata[0][0] = uncertain ? (PendingHitMetadata){.damage_mode = 2, .damage_min = 0, .damage_max = damage, .accuracy_bp = 5000} : (PendingHitMetadata){0};
}

static void test_specials(void) {
    reset();
    Player* p = us();
    p->special_energy = 100;
    RiskfightSpecWeapon spec = riskfight_spec_weapon(p);
    /* The spec max is the threat block's OURS spec_max of the weapon the special would use: the wielded one's row, else its bag row. */
    RiskfightBagWeapons bag = {{0}, 0};
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        const OsrsItemContentMetadata* m = osrs_inventory_cell_metadata(&p->inventory_cells[slot]);
        if (m->gear_slot == GEAR_SLOT_WEAPON && m->item_idx != p->equipped[GEAR_SLOT_WEAPON]) riskfight_threat_note_bag_weapon(&bag, m->item_idx);
    }
    int row = -1;
    if (spec.item == p->equipped[GEAR_SLOT_WEAPON]) row = 0;
    for (int i = 0; i < bag.count; i++) if (bag.item[i] == spec.item) row = i + 1;
    assert(row >= 0);
    printf("specials: wielded %s, the special uses %s (cost %d, OURS row %d)\n", ITEM_DATABASE[p->equipped[GEAR_SLOT_WEAPON]].name, ITEM_DATABASE[spec.item].name, spec.cost, row);
    float threat[RF_THREAT_SIZE];
    for (int bar = -1; bar <= OSRS_PLAYER_HEALTH_BAR_SCALE; bar++) {
        if (bar == 0) continue;
        him()->health_bar = bar;
        riskfight_threat_write(p, &bag, &him()->threat, bar, threat);
        assert(riskfight_threat_our_spec_max(p, spec.item, bar) == (int)lroundf(threat[RF_THREAT_OURS_OFFSET + row * RF_THREAT_OURS_WIDTH + 5] * RF_THREAT_DAMAGE_SCALE));
    }
    int checked = 0;
    for (int bar = -1; bar <= OSRS_PLAYER_HEALTH_BAR_SCALE; bar++) {
        if (bar == 0) continue;
        him()->health_bar = bar;
        int spec_max = riskfight_threat_our_spec_max(p, spec.item, bar);
        assert(spec_max > 0);
        OsrsHealthBarRange range = osrs_health_bar_range(bar, OSRS_PLAYER_HEALTH_BAR_SCALE, RF_THREAT_BASE_HITPOINTS, RF_THREAT_OVERHEAL_CAP);
        int lower = bar < 0 ? 99 : range.lower;
        assert(range.kind == (bar < 0 ? OSRS_HEALTH_BAR_MISSING : OSRS_HEALTH_BAR_KNOWN));
        for (int k = 1; k < RF_ACTION_DIMS[RF_SPECIAL]; k++) {
            int need = lower - k * spec_max; /* in flight at the boundary */
            if (need < 1) {
                in_flight(0, 0);
                assert(spec_offered(k)); /* reachable with nothing in flight */
                continue;
            }
            for (int uncertain = 0; uncertain < 2; uncertain++) {
                in_flight(need, uncertain);
                assert(spec_offered(k));
                in_flight(need - 1, uncertain);
                assert(!spec_offered(k));
                checked++;
            }
            if (bar < 0 || bar == 15) printf("specials: bar %d (his hp >= %d), %d x spec max %d: offered with %d in flight, not with %d\n", bar, lower, k, spec_max, need, need - 1);
        }
        p->num_pending_hits = 0;
    }
    assert(checked > 0);
    /* Energy still gates: with the reach, k specials need k x the cost. */
    him()->health_bar = 1;
    p->special_energy = spec.cost;
    assert(spec_offered(1) && !spec_offered(2));
    puts("specials: offered exactly from k x spec max + in flight == his bar's lower bound (99 with no bar), energy still gating");
}

int main(void) {
    test_meals();
    test_vengeance();
    test_specials();
    puts("test_riskfight_threat_masks: ok");
    return 0;
}
