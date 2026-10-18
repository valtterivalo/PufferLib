/* The net is offered a bag consumable only when it would change something (riskfight_consumable_useful in riskfight_write_action_mask): the boundary of every rule,
 * and the mask key (riskfight_mask_key) following each boundary, so the cached mask equals a fresh one.
 *   cc -std=gnu11 -O2 -I. -o /tmp/test_riskfight_useful_consumables ocean/osrs/tests/test_riskfight_useful_consumables.c -lm      (cwd = repo root) */
#include <assert.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"

static RiskfightState state;
static RiskfightContext context;
static unsigned char cached[RF_MASK_SIZE];
static RiskfightMaskKey held;

enum { SLOT_RESTORE, SLOT_PRAYER, SLOT_SANFEW, SLOT_COMBAT, SLOT_DIVINE, SLOT_RANGING, SLOT_BREW, SLOT_SHARK, SLOT_ANGLER, SLOT_KARAMBWAN };
static const OsrsConsumableKind KINDS[] = {
    OSRS_CONSUMABLE_SUPER_RESTORE, OSRS_CONSUMABLE_PRAYER_RESTORE, OSRS_CONSUMABLE_SANFEW, OSRS_CONSUMABLE_SUPER_COMBAT, OSRS_CONSUMABLE_DIVINE_COMBAT,
    OSRS_CONSUMABLE_RANGING, OSRS_CONSUMABLE_BREW, OSRS_CONSUMABLE_SHARK_FOOD, OSRS_CONSUMABLE_ANGLERFISH, OSRS_CONSUMABLE_KARAMBWAN,
};

static Player* us(void) { return &state.env.players[0]; }

/** Seat 0 rested: every timer at 0, no lock, hitpoints at base, prayer full, attack, strength and defence at the super combat's 118, ranged and magic at base. */
static void reset(void) {
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 11);
    Player* p = us();
    assert(p->base_prayer == RF_PRAYER_LEVEL && p->current_prayer == RF_PRAYER_LEVEL);
    assert(p->current_attack == 118 && p->current_strength == 118 && p->current_defence == 118 && p->current_ranged == 99 && p->current_magic == 99);
    p->current_hitpoints = p->base_hitpoints;
    p->food_timer = p->potion_timer = p->karambwan_timer = 0;
    p->pvp_overheal_block = 0;
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) p->inventory_cells[slot] = osrs_inventory_cell_empty();
    for (int slot = 0; slot < (int)(sizeof(KINDS) / sizeof(*KINDS)); slot++)
        p->inventory_cells[slot] = osrs_inventory_cell_from_content_code(riskfight_supply_code(KINDS[slot], slot < SLOT_SHARK ? 4 : 0));
    memset(&held, 0, sizeof(held));
}

/** Whether the item of physical `slot` is offered on its head; the cached mask (re-written only on a new key) must equal the fresh one. A meal (food, karambwan, brew) is
 * also closed above his chance range (riskfight_threat_gates, test_riskfight_threat_masks.c), which these hitpoints mostly are: for a meal the mask must offer it exactly
 * when it is useful and that gate is open, and the answer is the useful bit. */
static int offered(int slot) {
    static unsigned char fresh[RF_MASK_SIZE];
    riskfight_write_action_mask(&state, 0, fresh);
    riskfight_write_action_mask_keyed(&state, 0, &held, cached);
    assert(!memcmp(fresh, cached, RF_MASK_SIZE) && "the mask key missed an input");
    const OsrsItemContentMetadata* m = osrs_inventory_cell_metadata(&us()->inventory_cells[slot]);
    int head = m->click_action == OSRS_CLICK_DRINK ? RF_DRINK :
        m->consumable_kind == OSRS_CONSUMABLE_KARAMBWAN || m->consumable_kind == OSRS_CONSUMABLE_HALIBUT ? RF_COMBO : RF_FOOD;
    const RiskfightBagOrder order = riskfight_bag_order(us());
    for (int entry = 0; entry < OSRS_INVENTORY_SIZE; entry++) {
        if (order.slot[entry] != slot) continue;
        int bit = fresh[RF_HEAD_OFFSET[head] + entry + 1];
        if (m->click_action != OSRS_CLICK_EAT && m->consumable_kind != OSRS_CONSUMABLE_BREW) return bit;
        int useful = riskfight_consumable_useful(us(), (OsrsConsumableKind)m->consumable_kind);
        assert(bit == (useful && riskfight_threat_gates(&state, 0, riskfight_spec_weapon(us())).meals));
        return useful;
    }
    abort();
}

static void test_restores(void) {
    const struct { int slot; OsrsConsumableKind kind; int stats; } restores[] = {
        {SLOT_RESTORE, OSRS_CONSUMABLE_SUPER_RESTORE, 1}, {SLOT_SANFEW, OSRS_CONSUMABLE_SANFEW, 1}, {SLOT_PRAYER, OSRS_CONSUMABLE_PRAYER_RESTORE, 0},
    };
    for (size_t i = 0; i < sizeof(restores) / sizeof(*restores); i++) {
        reset();
        Player* p = us();
        const int amount = osrs_consumable_prayer_restore_amount(restores[i].kind, p->base_prayer);
        assert(!offered(restores[i].slot)); /* full prayer, nothing drained: the live case */
        p->current_prayer = p->base_prayer - (amount - 1);
        assert(!offered(restores[i].slot));
        p->current_prayer = p->base_prayer - amount;
        assert(offered(restores[i].slot));
        /* A drained level: the restores that restore it are offered at full prayer, a prayer potion is not. */
        p->current_prayer = p->base_prayer;
        p->current_attack = p->base_attack;
        assert(!offered(restores[i].slot));
        p->current_attack = p->base_attack - 1;
        assert(offered(restores[i].slot) == restores[i].stats);
        p->current_attack = 118;
        p->current_magic = p->base_magic - 1;
        assert(offered(restores[i].slot) == restores[i].stats);
    }
    puts("restores: offered from prayer missing = the dose's restore (not one less), or a level they restore one below base");
}

static void test_boosts(void) {
    reset();
    Player* p = us();
    const int target = p->base_attack + osrs_super_combat_boost_amount(p->base_attack);
    assert(target == 118);
    assert(!offered(SLOT_COMBAT) && !offered(SLOT_DIVINE));
    p->current_attack = target - 1;
    assert(offered(SLOT_COMBAT) && offered(SLOT_DIVINE));
    p->current_attack = target;
    p->current_defence = target - 1;
    assert(offered(SLOT_COMBAT) && offered(SLOT_DIVINE));
    p->current_hitpoints = OSRS_DIVINE_DAMAGE; /* the divine's damage would kill: still not offered */
    assert(offered(SLOT_COMBAT) && !offered(SLOT_DIVINE));
    const int ranged_target = p->base_ranged + osrs_ranging_boost_amount(p->base_ranged);
    p->current_ranged = ranged_target;
    assert(!offered(SLOT_RANGING));
    p->current_ranged = ranged_target - 1;
    assert(offered(SLOT_RANGING));
    puts("boosts: offered one below the potion's target from base, not at it");
}

static void test_brew(void) {
    reset();
    Player* p = us();
    const int hp_cap = p->base_hitpoints + osrs_brew_heal_amount(p->base_hitpoints);
    const int def_cap = p->base_defence + osrs_brew_defence_boost_amount(p->base_defence);
    p->current_hitpoints = hp_cap;
    p->current_defence = def_cap;
    assert(!offered(SLOT_BREW));
    p->current_attack = p->current_strength = 50; /* drained levels are no reason to brew */
    assert(!offered(SLOT_BREW));
    p->current_hitpoints = hp_cap - 1;
    assert(offered(SLOT_BREW));
    p->current_hitpoints = hp_cap;
    p->current_defence = def_cap - 1;
    assert(!offered(SLOT_BREW)); /* defence below its cap alone is no reason: it heals nothing there */
    puts("brew: not offered at its hitpoints cap, drained levels or defence below its cap notwithstanding; offered one hitpoint below");
}

static void test_meals(void) {
    reset();
    Player* p = us();
    assert(!offered(SLOT_SHARK) && !offered(SLOT_KARAMBWAN)); /* at base: heals nothing */
    p->current_hitpoints = p->base_hitpoints - 1;
    assert(offered(SLOT_SHARK) && offered(SLOT_KARAMBWAN)); /* heals 1 of 20: partial waste stays allowed */
    p->current_hitpoints = p->base_hitpoints;
    assert(offered(SLOT_ANGLER)); /* outside the lock it overheals */
    p->pvp_overheal_block = OSRS_PVP_OVERHEAL_BLOCK_TICKS;
    assert(!offered(SLOT_ANGLER)); /* inside it heals nothing at base */
    p->current_hitpoints = p->base_hitpoints - 1;
    assert(offered(SLOT_ANGLER));
    p->pvp_overheal_block = 0;
    const int overheal_cap = p->base_hitpoints + osrs_food_heal_amount(FOOD_ANGLERFISH);
    p->current_hitpoints = overheal_cap;
    assert(!offered(SLOT_ANGLER));
    p->current_hitpoints = overheal_cap - 1;
    assert(offered(SLOT_ANGLER));
    puts("meals: shark and karambwan offered below base only; anglerfish at base offered outside the overheal lock, not inside, and not at its overheal cap");
}

int main(void) {
    test_restores();
    test_boosts();
    test_brew();
    test_meals();
    puts("test_riskfight_useful_consumables: ok");
    return 0;
}
