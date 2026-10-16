#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "../encounters/encounter_riskfight.h"

/* Anglerfish overheal is blocked for a player the other player attacked or damaged (any hitsplat, 0s included) within the last 16 ticks, in the
 * PvP area every riskfight is (oldschool.runescape.wiki/w/Anglerfish, /w/Blighted_anglerfish). A hit landing at tick H blocks eats at H .. H+15. */

static RiskfightState state;
static RiskfightContext context;

enum { SLOT_FISH };

static void reset(void) {
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 7);
    state.env.pid_holder = 0;
    for (int side = 0; side < 2; side++) {
        Player* p = &state.env.players[side];
        p->current_hitpoints = 99;
        p->attack_timer = p->attack_timer_uncapped = 100;
        p->veng_active = 0;
        p->has_attack_timer = 1;
        p->num_pending_hits = 0;
        osrs_interaction_clear(&p->interaction);
        for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) p->inventory_cells[slot] = osrs_inventory_cell_empty();
        p->inventory_cells[SLOT_FISH] = osrs_inventory_cell_from_content_code(riskfight_supply_code(OSRS_CONSUMABLE_ANGLERFISH, 0));
    }
    state.env.players[1].x = state.env.players[0].x + 1;
    state.env.players[1].y = state.env.players[0].y;
}

static void step(int eat, int side) {
    HumanCommand click = {.kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK, .inventory_slot = SLOT_FISH};
    HumanCommandQueue clicks = {.items = &click, .count = eat ? 1 : 0};
    HumanCommandQueue none = {0};
    riskfight_step_queues(&state, &context, side == 0 ? &clicks : &none, side == 1 ? &clicks : &none);
}

/* A hit of `damage` from `source` lands on the other player during the next step. */
static void land_hit(int source, int damage, OsrsHitKind kind) {
    Player* from = &state.env.players[source];
    from->num_pending_hits = 1;
    from->pending_hits[0] = (PendingHit){.damage = damage, .hit_success = damage > 0, .attack_type = damage ? ATTACK_STYLE_MELEE : ATTACK_STYLE_NONE, .kind = kind};
    state.env.pending_hit_metadata[source][0] = (PendingHitMetadata){.damage_mode = 1};
}

/* Hitpoints of `victim` after eating `ticks` ticks after the hit on it landed (-1: never hit), from 99, one fish. */
static int eat_after(int victim, int ticks, int damage, OsrsHitKind kind) {
    reset();
    if (ticks >= 0) {
        land_hit(1 - victim, damage, kind);
        step(0, 0);
        assert(state.env.players[victim].hit_landed_this_tick);
        for (int k = 1; k < ticks; k++) step(0, 0);
    }
    state.env.players[victim].current_hitpoints = 99;
    step(1, victim);
    assert(osrs_inventory_cell_is_empty(&state.env.players[victim].inventory_cells[SLOT_FISH]));
    return state.env.players[victim].current_hitpoints;
}

static void test_window_edges(void) {
    for (int victim = 0; victim < 2; victim++) {
        assert(eat_after(victim, -1, 0, OSRS_HIT_DIRECT) == 121);
        for (int ticks = 1; ticks <= 15; ticks++) assert(eat_after(victim, ticks, 0, OSRS_HIT_DIRECT) == 99);
        for (int ticks = 16; ticks <= 20; ticks++) assert(eat_after(victim, ticks, 0, OSRS_HIT_DIRECT) == 121);
    }
}

/* Damage, blocks, and the Vengeance and Recoil echoes are all hitsplats from the other player; poison is nobody's attack. */
static void test_every_hitsplat_blocks(void) {
    assert(eat_after(0, 15, 25, OSRS_HIT_DIRECT) == 99 && eat_after(0, 16, 25, OSRS_HIT_DIRECT) == 121);
    assert(eat_after(0, 15, 4, OSRS_HIT_VENGEANCE) == 99);
    assert(eat_after(0, 15, 4, OSRS_HIT_RECOIL) == 99);
}

static void test_blocked_anglerfish_heals_to_max_only(void) {
    reset();
    land_hit(1, 0, OSRS_HIT_DIRECT);
    step(0, 0);
    state.env.players[0].current_hitpoints = 80;
    step(1, 0);
    assert(state.env.players[0].current_hitpoints == 99);
    assert(state.env.players[0].last_food_heal == 19 && state.env.players[0].last_food_waste == 3);
    reset();
    state.env.players[0].current_hitpoints = 80;
    step(1, 0);
    assert(state.env.players[0].current_hitpoints == 102 && state.env.players[0].last_food_waste == 0);
}

/* The lock is per victim: his hit on us does not block his own overheal. */
static void test_lock_is_per_player(void) {
    reset();
    land_hit(1, 10, OSRS_HIT_DIRECT);
    step(0, 0);
    assert(state.env.players[0].pvp_overheal_block == OSRS_PVP_OVERHEAL_BLOCK_TICKS && state.env.players[1].pvp_overheal_block == 0);
    state.env.players[1].current_hitpoints = 99;
    step(1, 1);
    assert(state.env.players[1].current_hitpoints == 121);
}

int main(void) {
    test_window_edges();
    test_every_hitsplat_blocks();
    test_blocked_anglerfish_heals_to_max_only();
    test_lock_is_per_player();
    puts("riskfight overheal lock passed");
    return 0;
}
