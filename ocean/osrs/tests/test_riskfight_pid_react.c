#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"

/* The Vengeance react on PID, end to end in the sim, and what the observation shows on the decision tick (the PID block, riskfight_model.h).
 * We hold PID and he swings a melee attack (a Dharok's greataxe, or a Voidwaker special) on tick T: his hit lands on T + 1 (the attacker without PID lands a tick
 * after the swing), and our inputs of T + 1 resolve before the hits landing on it (Fact 3), so a Vengeance cast on T + 1 is up when the hit lands and reflects it.
 * He holds PID: his hit lands on T itself, before any decision of ours sees the swing, and a cast on T + 1 is too late for it. */

static RiskfightState state;
static RiskfightContext context;

enum { SACK_SLOT = 1, HIS_HIT = 40 };

static void reset(uint16_t weapon) {
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 7);
    for (int side = 0; side < 2; side++) {
        Player* p = &state.env.players[side];
        p->current_hitpoints = 99;
        p->veng_active = 0;
        p->veng_cooldown = 0;
        p->equipped[GEAR_SLOT_WEAPON] = side == 1 ? weapon : ITEM_VOIDWAKER;
        p->equipped[GEAR_SLOT_SHIELD] = ITEM_NONE;
        p->equipped[GEAR_SLOT_RING] = ITEM_NONE;
        osrs_refresh_player_equipment(p);
        p->attack_timer = p->attack_timer_uncapped = 100;
        p->has_attack_timer = 1;
        p->num_pending_hits = 0;
        p->special_energy = 100;
        osrs_interaction_clear(&p->interaction);
        for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) p->inventory_cells[slot] = osrs_inventory_cell_empty();
        p->inventory_cells[SACK_SLOT] = osrs_inventory_cell_from_content_code(riskfight_supply_code(OSRS_CONSUMABLE_VENGEANCE_SACK, 0));
    }
    state.env.players[1].x = state.env.players[0].x + 1;
    state.env.players[1].y = state.env.players[0].y;
    riskfight_observe_visible(&state, 0, 1);
    riskfight_observe_visible(&state, 1, 1);
}

/* One tick with `holder` holding PID through it (the step reshuffles env.pid_holder at its end). */
static void step(int holder, HumanCommand* ours, int n, HumanCommand* theirs, int m) {
    state.env.pid_holder = holder;
    HumanCommandQueue a = {.items = ours, .count = n};
    HumanCommandQueue b = {.items = theirs, .count = m};
    riskfight_step_queues(&state, &context, &a, &b);
}

/* His swing's hit in the queue on us (the attacker holds it), rolled to HIS_HIT so the echo is known. */
static void fix_his_hit(void) {
    Player* him = &state.env.players[1];
    assert(him->num_pending_hits >= 1);
    for (int i = 0; i < him->num_pending_hits; i++) { him->pending_hits[i].damage = HIS_HIT; him->pending_hits[i].hit_success = 1; }
}

/* `spec`: a Voidwaker special, else a greataxe swing. `we_hold`: who holds PID on his swing's tick. A first swing of his on tick 1 (we hold PID: it pairs on
 * tick 2, naming us) gives the viewer a reading; his second swing comes when his clock frees it. */
static void react(int spec, int we_hold) {
    const uint16_t weapon = spec ? ITEM_VOIDWAKER : ITEM_DHAROKS_GREATAXE;
    reset(weapon);
    Player* us = &state.env.players[0];
    Player* him = &state.env.players[1];
    him->attack_timer = him->attack_timer_uncapped = -1;
    HumanCommand attack = {.kind = HUMAN_COMMAND_ATTACK_NPC, .npc_slot = 0}, toggle = {.kind = HUMAN_COMMAND_SPEC_TOGGLE};
    HumanCommand veng = {.kind = HUMAN_COMMAND_VENGEANCE};
    step(0, NULL, 0, &attack, 1);
    float obs[RF_OBS_SIZE];
    const float* pid = obs + RF_PID_START;
    riskfight_write_observation(&state, 0, obs);
    /* No pairing yet: his hit is still in the air (we hold PID), the reading is unknown. */
    assert(pid[RF_PID_OURS] == 0.5f && pid[RF_PID_IN_FLIGHT] == 0.5f && pid[RF_PID_IN_FLIGHT_MAX] > 0 && pid[RF_PID_IN_FLIGHT_SPEC] == 0);
    step(0, NULL, 0, NULL, 0);
    const int read_tick = state.env.tick - 1; /* the tick his first hit landed on and paired */
    assert(state.visible[0].pid_reading == 0 && state.visible[0].pid_seen_tick == read_tick);
    riskfight_write_observation(&state, 0, obs);
    assert(fabsf(pid[RF_PID_OURS] - (1 - 228.0f / 1916)) < 1e-6f && pid[RF_PID_IN_FLIGHT] == 0);
    /* Wait for his clock (riskfight_his_ready 1: he can swing on the tick about to be played); his next swing (with the special armed) on tick T. */
    while (riskfight_his_ready(&state.visible[0], state.env.tick, 0) != 1) {
        step(0, NULL, 0, NULL, 0);
        assert(state.env.tick < 12);
    }
    us->current_hitpoints = 99;
    const int him_before = him->current_hitpoints;
    step(we_hold ? 0 : 1, NULL, 0, spec ? &toggle : NULL, spec ? 1 : 0);
    const int T = state.env.tick - 1;
    assert(state.visible[0].last_attack_tick == T && state.visible[0].last_swing_tick == T);
    riskfight_write_observation(&state, 0, obs);
    const float age_odds = riskfight_pid_flip(T - read_tick);
    const float* his_row = obs + RF_THREAT_START; /* slot 0: the weapon he wields, the one he swung */
    assert(state.visible[0].threat.slots[0].item == weapon);
    assert(pid[RF_PID_IN_FLIGHT_SPEC] == (float)spec);
    assert(pid[RF_PID_IN_FLIGHT_MAX] == (spec ? his_row[9] : his_row[2]) && pid[RF_PID_IN_FLIGHT_MAX] > 0);
    if (spec) assert(obs[RF_SESSION_START + 1] == 0.5f); /* the spec shows in his energy estimate on the swing's tick */
    printf("%s, %s PID: decision tick %d obs pid_ours %.3f in_flight %.3f max %.0f spec %.0f | timing his_next_tick_max %.0f\n",
        spec ? "Voidwaker special" : "greataxe", we_hold ? "we hold" : "he holds", T + 1, pid[RF_PID_OURS], pid[RF_PID_IN_FLIGHT],
        pid[RF_PID_IN_FLIGHT_MAX] * RF_THREAT_DAMAGE_SCALE, pid[RF_PID_IN_FLIGHT_SPEC], obs[RF_TIMING_START + 2] * RF_THREAT_DAMAGE_SCALE);
    if (we_hold) {
        /* In the air: the reading (us, read on read_tick) is the chance it lands on T + 1. */
        assert(him->num_pending_hits >= 1 && us->current_hitpoints == 99);
        assert(fabsf(pid[RF_PID_OURS] - (1 - age_odds)) < 1e-6f && pid[RF_PID_IN_FLIGHT] == pid[RF_PID_OURS]);
        fix_his_hit();
        step(1, &veng, 1, NULL, 0); /* PID moves to him on T + 1: our cast still resolves before his hit (inputs before hits) */
        assert(state.inventory_use[0].vengeance_consumed_tick == T + 1);
        assert(!us->veng_active && us->current_hitpoints == 99 - HIS_HIT);
        /* The echo is queued on us (its source); it lands on him by the tick after. */
        step(0, NULL, 0, NULL, 0);
        assert(him->current_hitpoints == him_before - HIS_HIT * 3 / 4);
    } else {
        /* Landed on T: his swing paired at once, naming him. */
        assert(him->num_pending_hits == 0 && us->hit_landed_this_tick);
        assert(state.visible[0].pid_reading == 1 && state.visible[0].pid_seen_tick == T);
        assert(fabsf(pid[RF_PID_OURS] - 228.0f / 1916) < 1e-6f && pid[RF_PID_IN_FLIGHT] == pid[RF_PID_OURS]);
        const int after_hit = us->current_hitpoints;
        step(1, &veng, 1, NULL, 0);
        step(1, NULL, 0, NULL, 0);
        /* The cast is up, but his hit is gone: nothing to reflect. */
        assert(us->veng_active && state.inventory_use[0].vengeance_consumed_tick != T + 1);
        assert(us->current_hitpoints == after_hit && him->current_hitpoints == him_before);
    }
}

int main(void) {
    for (int spec = 0; spec < 2; spec++)
        for (int we_hold = 1; we_hold >= 0; we_hold--) react(spec, we_hold);
    printf("pid react: with PID his melee swing lands a tick later and a Vengeance cast on that tick reflects it; without it the hit lands on the swing's tick\n");
    return 0;
}
