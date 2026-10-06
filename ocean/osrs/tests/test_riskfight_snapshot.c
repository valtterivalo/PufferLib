#include <assert.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"

static RiskfightState state, copy;
static int bow_hits;
static RiskfightContext context;

static RiskfightLoadout fight_loadout(uint8_t weapon, uint8_t ammo, uint16_t first_slot) {
    RiskfightLoadout loadout = {.hitpoints = 99, .vengeance_sacks = 5};
    for (int i = 0; i < NUM_GEAR_SLOTS; i++) loadout.equipment[i] = ITEM_NONE;
    loadout.equipment[GEAR_SLOT_WEAPON] = weapon;
    loadout.equipment[GEAR_SLOT_AMMO] = ammo;
    loadout.equipment[GEAR_SLOT_RING] = ITEM_RING_OF_RECOIL;
    loadout.inventory[0] = first_slot;
    return loadout;
}

static void test_anglerfish_overheal(void) {
    RiskfightLoadout loadout = fight_loadout(ITEM_VOIDWAKER, ITEM_NONE,
        osrs_inventory_content_code_from_consumable(OSRS_CONSUMABLE_ANGLERFISH, 0));
    for (int slot = 1; slot < 4; slot++) loadout.inventory[slot] = loadout.inventory[0];
    context.loadout[0] = context.loadout[1] = &loadout;
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 7);
    Player* p = &state.env.players[0];
    assert(p->equipped[GEAR_SLOT_WEAPON] == ITEM_VOIDWAKER && p->equipped[GEAR_SLOT_RING] == ITEM_RING_OF_RECOIL);
    assert(state.inventory_use[0].vengeance_sacks == 5 && p->current_hitpoints == 99);
    int expected[] = {121, 121, 121};
    for (int eat = 0; eat < 3; eat++) {
        p->food_timer = 0;
        OsrsInventoryUseResult result = osrs_player_use_inventory(p, &state.inventory_use[0],
            &state.env.pvp_runtime.teleport[0], eat, state.env.tick);
        assert(result == OSRS_INVENTORY_USE_CONSUMED && p->current_hitpoints == expected[eat]);
        assert(osrs_inventory_cell_is_empty(&p->inventory_cells[eat]));
    }
    p->current_hitpoints = 90;
    p->food_timer = 0;
    osrs_player_use_inventory(p, &state.inventory_use[0], &state.env.pvp_runtime.teleport[0], 3, state.env.tick);
    assert(p->current_hitpoints == 112 && p->food_timer == 3);
    context.loadout[0] = context.loadout[1] = NULL;
}

static void count_bow_hits(void* user, const OsrsPvpHitEvent* event) {
    (void)user;
    bow_hits += event->source == 1 && event->kind == OSRS_HIT_DIRECT;
}

static RiskfightSnapshot snapshot(void) {
    RiskfightSnapshot snap = {.pid_holder = 1, .seed = 99};
    snap.loadout[0] = fight_loadout(ITEM_VOIDWAKER, ITEM_NONE, osrs_inventory_content_code_from_item(ITEM_GRANITE_MAUL_ORNATE));
    snap.loadout[1] = fight_loadout(ITEM_MAGIC_SHORTBOW_I, ITEM_DRAGON_ARROWS, osrs_inventory_content_code_from_item(ITEM_VOIDWAKER));
    snap.side[0] = (RiskfightSideSnapshot){.hitpoints = 77, .prayer = 80, .attack = 118, .strength = 118, .defence = 118, .magic = 99,
        .special_energy = 60, .special_regen_phase = 12, .spec_armed = 1, .veng_active = 1, .veng_cooldown = 30, .attack_timer = 2,
        .food_timer = 1, .recoil_damage_used = 10, .offensive_prayer = OFFENSIVE_PRAYER_PIETY, .interacting = 1,
        .seen_last_attack_ago = 3, .seen_last_attack_speed = 4};
    snap.side[1] = (RiskfightSideSnapshot){.hitpoints = 55, .prayer = 90, .attack = 118, .strength = 118, .defence = 118, .magic = 99,
        .special_energy = 100, .attack_timer = -1, .offensive_prayer = OFFENSIVE_PRAYER_PIETY, .x = 3, .y = 0, .interacting = 1,
        .seen_last_attack_ago = -1};
    return snap;
}

static void test_load_state(void) {
    RiskfightSnapshot snap = snapshot();
    riskfight_load_state(&state, &context, &snap);
    const Player* us = &state.env.players[0];
    const Player* him = &state.env.players[1];
    assert(us->current_hitpoints == 77 && us->current_prayer == 80 && us->special_energy == 60 && us->spec_armed);
    assert(us->spec_regen_active && us->item_effect_state.special_regen_ticks == 12);
    assert(us->veng_active && us->veng_cooldown == 30 && us->attack_timer == 2 && us->food_timer == 1);
    assert(us->item_effect_state.recoil_damage_used == 10 && us->item_effect_state.recoil_charges == RECOIL_MAX_CHARGES - 10);
    assert(him->current_hitpoints == 55 && him->x - us->x == 3 && him->y == us->y && can_attack_now((Player*)him));
    assert(state.env.pid_holder == 1 && state.env.tick == RISKFIGHT_LOAD_TICK && !state.env.episode_over);
    assert(state.visible[1].last_attack_tick == RISKFIGHT_LOAD_TICK - 3 && state.visible[0].last_attack_tick == -1);
    assert(state.visible[0].health_bar == osrs_health_bar_ratio(55, 99, OSRS_PLAYER_HEALTH_BAR_SCALE));
    assert(context.loadout[0] == NULL);
}

static void test_clone_is_deterministic(void) {
    RiskfightSnapshot snap = snapshot();
    riskfight_load_state(&state, &context, &snap);
    copy = state;
    int hits[2] = {0, 0};
    for (int run = 0; run < 2; run++) {
        RiskfightState* s = run ? &copy : &state;
        for (int tick = 0; tick < 16 && !s->env.episode_over; tick++) {
            int actions[2 * RF_HEADS] = {0};
            actions[RF_PRIMARY] = actions[RF_HEADS + RF_PRIMARY] = RF_ATTACK;
            actions[RF_PRAYER] = actions[RF_HEADS + RF_PRAYER] = OFFENSIVE_PRAYER_PIETY;
            riskfight_step((EncounterState*)s, (EncounterContext*)&context, actions);
        }
        hits[run] = s->env.players[0].current_hitpoints * 1000 + s->env.players[1].current_hitpoints;
    }
    assert(hits[0] == hits[1]);
}

static void test_ranged_opponent_hits_from_distance(void) {
    RiskfightSnapshot snap = snapshot();
    snap.side[0].interacting = 0;
    snap.side[1].x = 4;
    riskfight_load_state(&state, &context, &snap);
    state.env.pvp_runtime.hit_observer = count_bow_hits;
    for (int tick = 0; tick < 10 && !state.env.episode_over; tick++) {
        int actions[2 * RF_HEADS] = {0};
        actions[RF_HEADS + RF_PRIMARY] = RF_ATTACK;
        riskfight_step((EncounterState*)&state, (EncounterContext*)&context, actions);
    }
    assert(bow_hits > 0);
}

int main(void) {
    riskfight_init_context((EncounterContext*)&context);
    riskfight_finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    context.self_play = 1;
    test_anglerfish_overheal();
    test_load_state();
    test_clone_is_deterministic();
    test_ranged_opponent_hits_from_distance();
    riskfight_destroy_context((EncounterContext*)&context);
    puts("Riskfight snapshot contracts passed");
}
