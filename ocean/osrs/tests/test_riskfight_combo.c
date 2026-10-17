/* The scripted opponent's hit + instant maul combo (riskfight_script_combo): an axe + ornate maul fighter with our hitpoints within his combo swings the axe, and on the
 * next tick switches to the maul and lands both specials through the instant-special path (equip, two special clicks, the target click). We hold the PID, so his
 * axe hit lands on that same tick: three hits on us in one tick. Below the energy of one maul special he never does it. Scanned over script seeds: the combo is a
 * roll at the measured rate, so with the energy some seed does it and no seed does without. */
#include <assert.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"

static RiskfightState state;
static RiskfightContext context;
static const RiskfightProfile QUIET = {{0}, 0, 0, {0}, 0, 0, {{0}}};
enum { MAUL_SLOT = 5, SEEDS = 1000 };

static void setup(int energy, uint32_t seed) {
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 7);
    context.self_play = 0;
    state.opponent_profile = &QUIET;
    state.opp_lag = 0;
    state.ragger = 0;
    state.script_seed[1] = seed;
    for (int side = 0; side < 2; side++) {
        Player* p = &state.env.players[side];
        p->current_hitpoints = 99;
        p->veng_active = 0;
        p->equipped[GEAR_SLOT_RING] = ITEM_NONE;
        p->num_pending_hits = 0;
        osrs_interaction_clear(&p->interaction);
        for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++)
            p->inventory_cells[slot] = osrs_inventory_cell_empty();
    }
    Player* him = &state.env.players[1];
    him->equipped[GEAR_SLOT_WEAPON] = ITEM_DHAROKS_GREATAXE;
    him->equipped[GEAR_SLOT_SHIELD] = ITEM_NONE;
    osrs_refresh_player_equipment(him);
    him->inventory_cells[MAUL_SLOT] = osrs_inventory_cell_from_item(ITEM_GRANITE_MAUL_ORNATE);
    him->special_energy = energy;
    him->attack_timer = him->attack_timer_uncapped = 3;
    him->has_attack_timer = 1;
    state.env.players[0].attack_timer = state.env.players[0].attack_timer_uncapped = 100;
    state.env.players[1].x = state.env.players[0].x + 1;
    state.env.players[1].y = state.env.players[0].y;
}

/* One tick with us idle and holding the PID. Returns the hits that landed on us. */
static int step(void) {
    static const int idle[RF_HEADS] = {0};
    state.env.pid_holder = 0;
    riskfight_step((EncounterState*)&state, (EncounterContext*)&context, idle);
    return state.env.players[0].render_hit_count;
}

/* Runs to his first axe swing, then one tick more. Returns 1 when he did the combo on that tick (asserting its hits), 0 when he did not. */
static int run(int energy, uint32_t seed) {
    setup(energy, seed);
    Player* him = &state.env.players[1];
    int swung = 0;
    for (int n = 0; n < 8 && !swung; n++) {
        assert(step() == 0); /* nothing of his has landed yet: his first swing lands on the tick after it */
        swung = state.visible[0].events[(state.env.tick - 1) % RF_HISTORY_TICKS][0] > 0;
    }
    assert(swung && him->equipped[GEAR_SLOT_WEAPON] == ITEM_DHAROKS_GREATAXE && state.spec_maul[1] == 0);
    int landed = step();
    if (state.spec_maul[1] == 0) {
        assert(landed == 1); /* the axe hit alone */
        assert(him->equipped[GEAR_SLOT_WEAPON] == ITEM_DHAROKS_GREATAXE && him->special_energy == energy);
        return 0;
    }
    assert(him->equipped[GEAR_SLOT_WEAPON] == ITEM_GRANITE_MAUL_ORNATE);
    assert(state.visible[0].events[(state.env.tick - 1) % RF_HISTORY_TICKS][0] == 2); /* two maul specials seen this tick */
    assert(him->special_energy == energy - 2 * osrs_spec_cost(ITEM_GRANITE_MAUL_ORNATE));
    assert(landed == 3); /* the axe hit and both specials on one tick */
    return 1;
}

int main(void) {
    riskfight_init_context((EncounterContext*)&context);
    riskfight_finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    /* Our 99 is within his combo: his axe max plus two maul special maxes, at the levels the script assumes of him. */
    setup(100, 1);
    const Player* him = &state.env.players[1];
    OsrsThreatActor melee = osrs_threat_actor_assumed(ATTACK_STYLE_MELEE);
    OsrsThreatWeapon axe = osrs_threat_weapon(him->equipped, ITEM_DHAROKS_GREATAXE, &melee);
    int combo = osrs_threat_max_hit_at(&axe, 99, 99) + 2 * osrs_threat_weapon(him->equipped, ITEM_GRANITE_MAUL_ORNATE, &melee).spec_max;
    assert(99 < combo);
    int with_energy = 0, without = 0;
    for (uint32_t seed = 1; seed <= SEEDS; seed++) {
        with_energy += run(100, seed);
        without += run(osrs_spec_cost(ITEM_GRANITE_MAUL_ORNATE) - 1, seed);
    }
    printf("combo max %d over our 99: %d of %d axe swings followed by the maul combo with 100 energy, %d with 49\n", combo, with_energy, SEEDS, without);
    assert(with_energy > 0 && without == 0);
    riskfight_destroy_context((EncounterContext*)&context);
    puts("Riskfight scripted maul combo contracts passed");
}
