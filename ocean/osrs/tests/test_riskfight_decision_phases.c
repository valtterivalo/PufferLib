#include <assert.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"

static RiskfightState state;
static RiskfightContext context;

static void reset(int holder) {
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 7);
    state.env.pid_holder = holder;
    for (int side = 0; side < 2; side++) {
        Player* p = &state.env.players[side];
        p->current_hitpoints = 99;
        p->veng_active = 0;
        p->veng_cooldown = 0;
        p->equipped[GEAR_SLOT_WEAPON] = ITEM_VOIDWAKER;
        p->equipped[GEAR_SLOT_SHIELD] = ITEM_NONE;
        p->equipped[GEAR_SLOT_RING] = ITEM_NONE;
        osrs_refresh_player_equipment(p);
        p->attack_timer = p->attack_timer_uncapped = 100;
        p->has_attack_timer = 1;
        p->num_pending_hits = 0;
        osrs_interaction_clear(&p->interaction);
        for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++)
            p->inventory_cells[slot] = osrs_inventory_cell_empty();
        p->inventory_cells[1] = osrs_inventory_cell_from_content_code(
            riskfight_supply_code(OSRS_CONSUMABLE_VENGEANCE_SACK, 0));
    }
    state.env.players[1].x = state.env.players[0].x + 1;
    state.env.players[1].y = state.env.players[0].y;
}

static void step(HumanCommand* ours, int n, HumanCommand* theirs, int m) {
    HumanCommandQueue a = {.items = ours, .count = n};
    HumanCommandQueue b = {.items = theirs, .count = m};
    riskfight_step_queues(&state, &context, &a, &b);
}

/* Actual first-actor launch, not a pre-existing pending hit: those are distinct phases. */
static void test_food_and_vengeance(void) {
    for (int holder = 0; holder < 2; holder++) {
        reset(holder);
        Player* us = &state.env.players[0];
        Player* him = &state.env.players[1];
        us->current_hitpoints = 10;
        us->inventory_cells[0] = osrs_inventory_cell_from_content_code(
            osrs_inventory_content_code_from_raw_osrs_id(13441));
        him->attack_timer = him->attack_timer_uncapped = -1;
        him->spec_armed = 1;
        HumanCommand attack = {.kind = HUMAN_COMMAND_ATTACK_NPC, .npc_slot = 0};
        HumanCommand food = {.kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK, .inventory_slot = 0};
        step(&food, 1, &attack, 1);
        /* Fact 3: the meal resolves before any damage landing on its tick, either PID. */
        assert(osrs_inventory_cell_is_empty(&us->inventory_cells[0]));
        assert(state.consume_ticks[0] == 1);
        if (holder == 0) {
            assert(us->current_hitpoints == 32);
            assert(him->num_pending_hits == 1); /* Second actor's attack has not reached us yet. */
        } else {
            /* First actor's zero-delay melee lands in our incoming phase after the meal: 32 minus the spec hit. */
            assert(us->current_hitpoints < 32);
            assert(him->num_pending_hits == 0);
        }
        reset(holder);
        us = &state.env.players[0]; him = &state.env.players[1];
        us->current_hitpoints = 10;
        him->attack_timer = him->attack_timer_uncapped = -1;
        him->spec_armed = 1;
        int sacks = state.inventory_use[0].vengeance_sacks;
        HumanCommand veng = {.kind = HUMAN_COMMAND_VENGEANCE};
        step(&veng, 1, &attack, 1);
        assert(state.inventory_use[0].vengeance_sacks == sacks - 1);
        if (holder == 0) {
            assert(us->veng_active && him->current_hitpoints == 99);
            step(NULL, 0, NULL, 0);
            assert(!us->veng_active && him->current_hitpoints < 99);
        } else {
            /* The cast precedes his hit landing on us; the echo reaches him a tick later (we are processed after him). */
            assert(!us->veng_active && him->current_hitpoints == 99);
            step(NULL, 0, NULL, 0);
            assert(him->current_hitpoints < 99);
        }
    }
}

static void test_launched_hit_reactive_food_by_pid(void) {
    for (int holder = 0; holder < 2; holder++) {
        reset(holder);
        Player* us = &state.env.players[0];
        us->current_hitpoints = 10;
        us->inventory_cells[0] = osrs_inventory_cell_from_content_code(
            riskfight_supply_code(OSRS_CONSUMABLE_ANGLERFISH, 0));
        Player* source = &state.env.players[1];
        source->num_pending_hits = 1;
        source->pending_hits[0] = (PendingHit){.damage = 20, .hit_success = 1,
            .attack_type = ATTACK_STYLE_MELEE};
        state.env.pending_hit_metadata[1][0].damage_mode = 1;
        HumanCommand food = {.kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK, .inventory_slot = 0};
        step(&food, 1, NULL, 0);
        assert(us->current_hitpoints == 12 && state.consume_ticks[0] == 1);
        assert(osrs_inventory_cell_is_empty(&us->inventory_cells[0]));
    }
}

/* Captured t205 opponent animation -> local reactive cast t206 -> incoming24
 * and reflected18. Echo2/11 do not proc Vengeance; no future attack RNG is used. */
static void test_captured_reactive_vengeance_by_pid(void) {
    for (int holder = 0; holder < 2; holder++) {
        reset(holder);
        Player* us = &state.env.players[0];
        Player* him = &state.env.players[1];
        him->num_pending_hits = 3;
        him->pending_hits[0] = (PendingHit){.damage = 2, .kind = OSRS_HIT_RECOIL};
        him->pending_hits[1] = (PendingHit){.damage = 11, .kind = OSRS_HIT_VENGEANCE};
        him->pending_hits[2] = (PendingHit){.damage = 24, .hit_success = 1,
            .attack_type = ATTACK_STYLE_MELEE};
        for (int hit = 0; hit < 3; hit++)
            state.env.pending_hit_metadata[1][hit].damage_mode = 1;
        int sacks = state.inventory_use[0].vengeance_sacks;
        HumanCommand veng = {.kind = HUMAN_COMMAND_VENGEANCE};
        step(&veng, 1, NULL, 0);
        assert(us->current_hitpoints == 62);
        assert(state.inventory_use[0].vengeance_sacks == sacks - 1);
        assert(!us->veng_active);
        if (holder == 0) assert(him->current_hitpoints == 81);
        else assert(him->current_hitpoints == 99); /* echo lands next tick: we are processed after the attacker */
    }
}

static void test_stop_preserves_global_cooldown(void) {
    for (int holder = 0; holder < 2; holder++) {
        reset(holder);
        Player* us = &state.env.players[0];
        us->attack_timer = us->attack_timer_uncapped = 3;
        osrs_interaction_set(&us->interaction, 1);
        HumanCommand stop = {.kind = HUMAN_COMMAND_STOP};
        step(&stop, 1, NULL, 0);
        assert(!osrs_interaction_active(&us->interaction));
        assert(us->attack_timer == 2 && us->attack_timer_uncapped == 2);
        assert(state.env.players[1].current_hitpoints == 99 && !us->just_attacked);
    }
}

static void test_reflection_recipient_phase(void) {
    for (int holder = 0; holder < 2; holder++) {
        reset(holder);
        Player* us = &state.env.players[0];
        Player* him = &state.env.players[1];
        us->veng_active = 1;
        him->num_pending_hits = 1;
        him->pending_hits[0] = (PendingHit){.damage = 20, .hit_success = 1,
            .attack_type = ATTACK_STYLE_MELEE};
        state.env.pending_hit_metadata[1][0].damage_mode = 1;
        step(NULL, 0, NULL, 0);
        assert(us->current_hitpoints == 79 && !us->veng_active);
        assert(him->current_hitpoints == (holder == 0 ? 84 : 99));
        if (holder == 1) {
            step(NULL, 0, NULL, 0);
            assert(him->current_hitpoints == 84);
        }
    }
}

static void test_maul_ignores_running_global_clock(void) {
    for (int holder = 0; holder < 2; holder++) {
        reset(holder);
        Player* us = &state.env.players[0];
        us->equipped[GEAR_SLOT_WEAPON] = ITEM_GRANITE_MAUL_ORNATE;
        osrs_refresh_player_equipment(us);
        us->attack_timer = us->attack_timer_uncapped = 10;
        HumanCommand commands[] = {
            {.kind = HUMAN_COMMAND_SPEC_TOGGLE},
            {.kind = HUMAN_COMMAND_ATTACK_NPC, .npc_slot = 0}
        };
        step(commands, 2, NULL, 0);
        assert(state.spec_maul[0] == 1 && us->special_energy == 50);
        assert(us->attack_timer == 9 && us->attack_timer_uncapped == 9);
    }
}

static void test_maul_input_before_first_actor_incoming(void) {
    for (int holder = 0; holder < 2; holder++) {
        reset(holder);
        Player* us = &state.env.players[0];
        us->current_hitpoints = 10;
        us->equipped[GEAR_SLOT_WEAPON] = ITEM_GRANITE_MAUL_ORNATE;
        osrs_refresh_player_equipment(us);
        Player* him = &state.env.players[1];
        him->num_pending_hits = 1;
        him->pending_hits[0] = (PendingHit){.damage = 20, .hit_success = 1,
            .attack_type = ATTACK_STYLE_MELEE};
        state.env.pending_hit_metadata[1][0].damage_mode = 1;
        HumanCommand commands[] = {
            {.kind = HUMAN_COMMAND_SPEC_TOGGLE},
            {.kind = HUMAN_COMMAND_ATTACK_NPC, .npc_slot = 0}
        };
        step(commands, 2, NULL, 0);
        assert(us->current_hitpoints == 0);
        /* The instant maul spec is an input of its tick, ahead of the lethal incoming hit, whichever actor is processed first. */
        assert(state.spec_maul[0] == 1 && us->special_energy == 50);
    }
}

int main(void) {
    riskfight_init_context((EncounterContext*)&context);
    riskfight_finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    test_food_and_vengeance();
    test_launched_hit_reactive_food_by_pid();
    test_captured_reactive_vengeance_by_pid();
    test_stop_preserves_global_cooldown();
    test_reflection_recipient_phase();
    test_maul_ignores_running_global_clock();
    test_maul_input_before_first_actor_incoming();
    riskfight_destroy_context((EncounterContext*)&context);
    puts("Riskfight recipient phases passed");
    return 0;
}
