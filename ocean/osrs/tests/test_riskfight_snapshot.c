#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"
#include <unistd.h>
#include "riskfight_tape.h"

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
    RiskfightSnapshot snap = {.pid_holder = 1, .pending_known = 1, .seed = 99};
    snap.loadout[0] = fight_loadout(ITEM_VOIDWAKER, ITEM_NONE, osrs_inventory_content_code_from_item(ITEM_GRANITE_MAUL_ORNATE));
    snap.loadout[1] = fight_loadout(ITEM_MAGIC_SHORTBOW_I, ITEM_DRAGON_ARROWS, osrs_inventory_content_code_from_item(ITEM_VOIDWAKER));
    snap.side[0] = (RiskfightSideSnapshot){.hitpoints = 77, .prayer = 80, .attack = 118, .strength = 118, .defence = 118, .magic = 99,
        .attack_history = RISKFIGHT_ATTACK_OBSERVED, .attack_timer_uncapped = 2,
        .special_energy = 60, .special_regen_phase = 12, .spec_armed = 1, .veng_active = 1, .veng_cooldown = 30, .attack_timer = 2,
        .food_timer = 1, .recoil_damage_used = 10, .offensive_prayer = OFFENSIVE_PRAYER_PIETY, .interacting = 1,
        .seen_health_bar = osrs_health_bar_ratio(77, 99, OSRS_PLAYER_HEALTH_BAR_SCALE), .seen_last_attack_ago = 3, .seen_last_attack_speed = 4};
    snap.side[1] = (RiskfightSideSnapshot){.hitpoints = 55, .prayer = 90, .attack = 118, .strength = 118, .defence = 118, .magic = 99,
        .attack_history = RISKFIGHT_ATTACK_ABSENT, .attack_timer_uncapped = -1,
        .special_energy = 100, .attack_timer = -1, .offensive_prayer = OFFENSIVE_PRAYER_PIETY, .x = 3, .y = 0, .interacting = 1,
        .seen_health_bar = osrs_health_bar_ratio(55, 99, OSRS_PLAYER_HEALTH_BAR_SCALE), .seen_last_attack_ago = -1};
    snap.side[0].poison_source = snap.side[1].poison_source = OSRS_POISON_CLEAR_PRIOR;
    return snap;
}

static void test_load_state(void) {
    RiskfightSnapshot snap = snapshot();
    riskfight_load_state(&state, &context, &snap);
    const Player* us = &state.env.players[0];
    const Player* him = &state.env.players[1];
    assert(us->current_hitpoints == 77 && us->current_prayer == 80 && us->special_energy == 60 && us->spec_armed);
    assert(us->spec_regen_active && us->item_effect_state.special_regen_ticks == 12);
    assert(us->veng_active && us->veng_cooldown == 30 && us->attack_timer == 2 && us->attack_timer_uncapped == 2 && us->has_attack_timer && us->food_timer == 1);
    assert(us->item_effect_state.recoil_damage_used == 10 && us->item_effect_state.recoil_charges == RECOIL_MAX_CHARGES - 10);
    assert(him->current_hitpoints == 55 && him->x - us->x == 3 && him->y == us->y && can_attack_now((Player*)him));
    assert(state.env.pid_holder == 1 && state.env.tick == RISKFIGHT_LOAD_TICK && !state.env.episode_over);
    assert(state.visible[1].last_attack_tick == RISKFIGHT_LOAD_TICK - 3 && state.visible[0].last_attack_tick == -1);
    assert(state.visible[0].health_bar == osrs_health_bar_ratio(55, 99, OSRS_PLAYER_HEALTH_BAR_SCALE));
    /* The overheal lock comes from the other side's last swing: we swung 3 ticks ago, so a melee hit landed on him 2 ticks ago; he never swung. */
    assert(him->pvp_overheal_block == OSRS_PVP_OVERHEAL_BLOCK_TICKS + 1 - 3 && us->pvp_overheal_block == 0);
    assert(context.loadout[0] == NULL);
}

static void test_continuous_clock_load(void) {
    RiskfightSnapshot snap = snapshot();
    snap.side[0].attack_timer = -1;
    snap.side[0].attack_timer_uncapped = -2;
    riskfight_load_state(&state, &context, &snap);
    Player* us = &state.env.players[0];
    assert(us->has_attack_timer && us->attack_timer == -1 && us->attack_timer_uncapped == -2);
    assert(!state.env.players[1].has_attack_timer);
    us->karambwan_timer = 0;
    OsrsPlayerEatResult eaten = osrs_player_eat_food_effects_with_clock(us, FOOD_KARAMBWAN, OSRS_FOOD_CONTINUOUS_CLOCK);
    assert(eaten.consumed && us->attack_timer == 0 && us->attack_timer_uncapped == 0);
}

static void test_carried_health_bar(void) {
    const struct { int hp, bar; } cases[] = {{55, 7}, {90, 7}, {55, -1}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
        RiskfightSnapshot snap = snapshot();
        snap.side[0].interacting = snap.side[1].interacting = 0;
        snap.side[0].veng_active = 0;
        snap.side[1].hitpoints = cases[i].hp;
        snap.side[1].seen_health_bar = cases[i].bar;
        snap.loadout[0].equipment[GEAR_SLOT_RING] = snap.loadout[1].equipment[GEAR_SLOT_RING] = ITEM_NONE;
        snap.loadout[1].inventory[0] = osrs_inventory_content_code_from_consumable(OSRS_CONSUMABLE_ANGLERFISH, 0);
        riskfight_load_state(&state, &context, &snap);
        Player* him = &state.env.players[1];
        assert(him->current_hitpoints == cases[i].hp && state.visible[0].health_bar == cases[i].bar);
        HumanCommand eat = {.kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK, .inventory_slot = 0};
        HumanCommandQueue food = {.items = &eat, .count = 1}, idle = {0};
        riskfight_step_queues(&state, &context, &idle, &food);
        assert(osrs_inventory_cell_is_empty(&him->inventory_cells[0]) && him->current_hitpoints > cases[i].hp);
        assert(!him->hit_landed_this_tick && state.visible[0].health_bar == cases[i].bar);
        queue_hit(&state.env, 0, 1, &state.env.players[0], him, 1, ATTACK_STYLE_MELEE, 0, 0, 1, 0, 0, 0, 0, 0);
        riskfight_step_queues(&state, &context, &idle, &idle);
        assert(him->hit_landed_this_tick && state.visible[0].health_bar == osrs_health_bar_ratio(him->current_hitpoints, 99, OSRS_PLAYER_HEALTH_BAR_SCALE));
    }
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

static void test_pending_damage(void) {
    RiskfightSnapshot snap = snapshot();
    snap.side[0].interacting = snap.side[1].interacting = 0;
    snap.side[0].veng_active = snap.side[1].veng_active = 0;
    snap.loadout[0].equipment[GEAR_SLOT_RING] = snap.loadout[1].equipment[GEAR_SLOT_RING] = ITEM_NONE;
    snap.pending_count[1] = 1;
    snap.pending[1][0] = (PendingHit){.damage = 24,
        .ticks_until_hit = 1, .attack_type = ATTACK_STYLE_MAGIC,
        .defender_prayer_at_attack = PRAYER_PROTECT_MAGIC, .hit_success = 1};
    /* Old fixed snapshots omit metadata: retain the raw roll and ordinary mitigation. */
    riskfight_load_state(&state, &context, &snap);
    state.env.players[0].prayer = PRAYER_PROTECT_MAGIC;
    uint32_t raw_rng = 123;
    riskfight_sample_pending(&state, &raw_rng);
    assert(raw_rng == 123 && state.env.players[1].pending_hits[0].damage == 24);
    assert(state.env.pending_hit_metadata[1][0].damage_mode == 0);
    HumanCommandQueue raw_idle = {0};
    riskfight_step_queues(&state, &context, &raw_idle, &raw_idle);
    riskfight_step_queues(&state, &context, &raw_idle, &raw_idle);
    assert(state.env.players[0].current_hitpoints > 53 && state.env.players[0].current_hitpoints < 77);
    snap.pending_metadata[1][0] = (PendingHitMetadata){.damage_mode = 1, .smite_snapshot = 2};
    riskfight_load_state(&state, &context, &snap);
    assert(!memcmp(&state.env.pending_hit_metadata[1][0], &snap.pending_metadata[1][0], sizeof(PendingHitMetadata)));
    state.env.players[0].prayer = PRAYER_PROTECT_MAGIC;
    copy = state;
    HumanCommandQueue idle = {0};
    for (int run = 0; run < 2; run++) {
        RiskfightState* s = run ? &copy : &state;
        riskfight_step_queues(s, &context, &idle, &idle);
        assert(s->env.players[0].current_hitpoints == 77);
        riskfight_step_queues(s, &context, &idle, &idle);
        assert(s->env.players[0].current_hitpoints == 53);
        assert(s->env.players[1].num_pending_hits == 0);
        assert(s->env.pending_hit_metadata[1][0].damage_mode == 0);
        assert(s->env.players[0].current_prayer <= 74);
        riskfight_step_queues(s, &context, &idle, &idle);
        assert(s->env.players[0].current_hitpoints == 53);
    }
    /* Echoes have already consumed the original reflection; neither side reflects an echo. */
    snap.side[0].veng_active = snap.side[1].veng_active = 1;
    snap.loadout[0].equipment[GEAR_SLOT_RING] = snap.loadout[1].equipment[GEAR_SLOT_RING] = ITEM_RING_OF_RECOIL;
    snap.pending[1][0].kind = OSRS_HIT_VENGEANCE;
    snap.pending[1][0].ticks_until_hit = 0;
    riskfight_load_state(&state, &context, &snap);
    riskfight_step_queues(&state, &context, &idle, &idle);
    assert(state.env.players[0].current_hitpoints == 53 && state.env.players[1].current_hitpoints == 55);
    assert(state.env.players[0].num_pending_hits == 0 && state.env.players[1].num_pending_hits == 0);
    assert(state.env.players[0].veng_active);
    /* A final direct hit still legitimately produces future vengeance and recoil. */
    snap.pending[1][0].kind = OSRS_HIT_DIRECT;
    riskfight_load_state(&state, &context, &snap);
    riskfight_step_queues(&state, &context, &idle, &idle);
    riskfight_step_queues(&state, &context, &idle, &idle);
    assert(state.env.players[1].current_hitpoints == 34); /* 18 vengeance + 3 recoil */
    assert(!state.env.players[0].veng_active);
    /* Draw once, then clone the resolved world: both plans inherit the same launched roll. */
    snap.pending_metadata[1][0].damage_mode = 2;
    snap.pending_metadata[1][0].damage_min = 20;
    snap.pending_metadata[1][0].damage_max = 40;
    snap.pending_metadata[1][0].accuracy_bp = 10000;
    riskfight_load_state(&state, &context, &snap);
    uint32_t rng = 123;
    riskfight_sample_pending(&state, &rng);
    copy = state;
    assert(state.env.pending_hit_metadata[1][0].damage_mode == 1);
    assert(state.env.players[1].pending_hits[0].damage >= 20 && state.env.players[1].pending_hits[0].damage <= 40);
    uint32_t after = rng;
    riskfight_sample_pending(&copy, &rng);
    assert(rng == after && !memcmp(copy.env.players[1].pending_hits, state.env.players[1].pending_hits, sizeof(copy.env.players[1].pending_hits)));
    assert(!memcmp(copy.env.pending_hit_metadata, state.env.pending_hit_metadata, sizeof(copy.env.pending_hit_metadata)));
    riskfight_step_queues(&state, &context, &idle, &idle);
    riskfight_step_queues(&copy, &context, &idle, &idle);
    assert(copy.env.players[0].current_hitpoints == state.env.players[0].current_hitpoints);
}

static void test_pending_pid_resolution(void) {
    RiskfightSnapshot snap = snapshot();
    snap.pending_pid_active = 1;
    snap.pending_pid_count[0][1] = 1;
    snap.pending_pid[0][1][0] = (PendingHit){.damage = 20,
        .hit_success = 1, .ticks_until_hit = 0};
    snap.pending_pid_metadata[0][1][0] = (PendingHitMetadata){.damage_mode = 1};
    snap.side[0].interacting = snap.side[1].interacting = 0;
    snap.side[0].veng_active = snap.side[1].veng_active = 0;
    snap.loadout[0].equipment[GEAR_SLOT_RING] = snap.loadout[1].equipment[GEAR_SLOT_RING] = ITEM_NONE;
    uint32_t rng = 123, initial = rng;
    riskfight_load_state(&state, &context, &snap);
    assert(state.pending_pid_active && state.env.players[1].num_pending_hits == 0);
    riskfight_sample_pending(&state, &rng);
    assert(!state.pending_pid_active && state.env.players[1].num_pending_hits == 0 && rng == initial);
    snap.pid_holder = 0;
    riskfight_load_state(&state, &context, &snap);
    assert(state.env.players[1].num_pending_hits == 1);
    assert(state.env.pending_hit_metadata[1][0].damage_mode == 1);
    riskfight_sample_pending(&state, &rng);
    HumanCommandQueue idle = {0};
    riskfight_step_queues(&state, &context, &idle, &idle);
    assert(state.env.players[0].current_hitpoints == 57 && state.env.players[1].num_pending_hits == 0);
    state.env.pid_holder = 1;
    riskfight_sample_pending(&state, &rng);
    state.env.pid_holder = 0;
    riskfight_sample_pending(&state, &rng);
    assert(state.env.players[1].num_pending_hits == 0 && rng == initial);
    assert(state.env.pending_hit_metadata[1][0].damage_mode == 0);
}

static void test_tape_poison_and_observed_gear(void) {
    char path[] = "/tmp/riskfight-snapshot-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE* file = fdopen(fd, "w");
    assert(file);
    fputs("FIGHT 0 source actor 198 199\nP 198 1 -54 198\nE 198 1 12 -1 -1 -1 27867 -1 22322 -1 -1 -1 -1 -1 -1\nQ 198 1\n", file);
    for (int tick = 198; tick <= 199; tick++) {
        fprintf(file, "T %d", tick);
        for (int i = 1; i < 32; i++) fputs(" 0", file);
        fputs(" L NONE 0", file);
        for (int i = 0; i < TAPE_WORN + TAPE_BAG; i++) fputs(" -1", file);
        fputc('\n', file);
    }
    assert(fclose(file) == 0);
    Tape tape;
    assert(tape_load(&tape, path) == 2);
    assert(unlink(path) == 0);
    const TapeTick* row = tape_tick(&tape, 198);
    assert(row->pending_known == 1 && row->opponent_gear_source == 1);
    assert(row->opponent_gear_count == 12 && row->opponent_gear[3] == 27867 && row->opponent_gear[5] == 22322);
    OsrsPoisonState poison;
    const TapeTick* missing = tape_tick(&tape, 199);
    assert(!missing->poison_source && !missing->opponent_gear_source && !missing->pending_known);
    assert(tape_local_poison(missing, &poison) == OSRS_POISON_UNKNOWN_STATE);
    assert(tape_local_poison(row, &poison) == OSRS_POISON_HORIZON_IMMUNITY && poison.immunity_ticks == 1590);
    TapeTick aged = *row;
    aged.tick = 228;
    assert(tape_local_poison(&aged, &poison) == OSRS_POISON_HORIZON_IMMUNITY && poison.immunity_ticks == 1560);
    aged.tick = 1788;
    assert(tape_local_poison(&aged, &poison) == OSRS_POISON_HORIZON_IMMUNITY && poison.immunity_ticks == 0);
    aged.tick++;
    assert(tape_local_poison(&aged, &poison) == OSRS_POISON_UNKNOWN_STATE);
    aged = *row;
    aged.poison_value = -1;
    assert(tape_local_poison(&aged, &poison) == OSRS_POISON_HORIZON_IMMUNITY && poison.immunity_ticks == 0);
    aged.poison_value = 0;
    assert(tape_local_poison(&aged, &poison) == OSRS_POISON_OBSERVED_STATE && poison.severity == 0);
    aged.tick++;
    assert(tape_local_poison(&aged, &poison) == OSRS_POISON_UNKNOWN_STATE);
    aged.tick--;
    aged.poison_value = 30;
    assert(tape_local_poison(&aged, &poison) == OSRS_POISON_UNKNOWN_STATE);
    aged.poison_source = 0;
    assert(tape_local_poison(&aged, &poison) == OSRS_POISON_UNKNOWN_STATE);
    tape_free(&tape);
}

static void test_poison_snapshot_clone(void) {
    RiskfightSnapshot snap = snapshot();
    snap.side[0].interacting = snap.side[1].interacting = 0;
    snap.side[0].poison = (OsrsPoisonState){.immunity_ticks = 2};
    snap.side[0].poison_source = OSRS_POISON_HORIZON_IMMUNITY;
    snap.side[1].poison = (OsrsPoisonState){.severity = 30, .ticks_until_pulse = 2};
    snap.side[1].poison_source = OSRS_POISON_OBSERVED_STATE;
    snap.pending_count[1] = 1;
    snap.pending[1][0] = (PendingHit){.damage = 7, .hit_success = 1, .ticks_until_hit = 3};
    snap.pending_metadata[1][0] = (PendingHitMetadata){.damage_mode = 1};
    riskfight_load_state(&state, &context, &snap);
    copy = state;
    HumanCommandQueue idle = {0};
    for (int run = 0; run < 2; run++) {
        RiskfightState* s = run ? &copy : &state;
        assert(s->poison_source[0] == OSRS_POISON_HORIZON_IMMUNITY);
        assert(s->poison_source[1] == OSRS_POISON_OBSERVED_STATE);
        riskfight_step_queues(s, &context, &idle, &idle);
        assert(s->poison[0].immunity_ticks == 1);
        assert(s->env.players[1].current_hitpoints == 55 && s->poison[1].ticks_until_pulse == 1);
        assert(s->env.players[1].num_pending_hits == 1 && s->env.players[1].pending_hits[0].damage == 7 &&
            s->env.pending_hit_metadata[1][0].damage_mode == 1);
        riskfight_step_queues(s, &context, &idle, &idle);
        assert(s->poison[0].immunity_ticks == 0);
        assert(s->env.players[1].current_hitpoints == 49 && s->poison[1].severity == 29);
    }
    assert(!memcmp(copy.poison, state.poison, sizeof(state.poison)));
    assert(!memcmp(copy.env.pending_hit_metadata, state.env.pending_hit_metadata, sizeof(copy.env.pending_hit_metadata)));
}

int main(void) {
    riskfight_init_context((EncounterContext*)&context);
    riskfight_finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    context.self_play = 1;
    test_anglerfish_overheal();
    test_load_state();
    test_continuous_clock_load();
    test_carried_health_bar();
    test_pending_damage();
    test_pending_pid_resolution();
    test_tape_poison_and_observed_gear();
    test_poison_snapshot_clone();
    RiskfightSnapshot snap = snapshot();
    snap.side[1].x = 1;
    snap.side[1].interacting = 0;
    riskfight_load_state(&state, &context, &snap);
    HumanCommand commands[] = {
        {.kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK, .inventory_slot = 0},
        {.kind = HUMAN_COMMAND_SPEC_TOGGLE},
    };
    HumanCommandQueue queue = {.items = commands, .count = 2};
    HumanCommandQueue idle = {0};
    riskfight_step_queues(&state, &context, &queue, &idle);
    assert(state.spec_maul[0] == 1 && state.env.players[0].special_energy == 10);
    test_clone_is_deterministic();
    test_ranged_opponent_hits_from_distance();
    riskfight_destroy_context((EncounterContext*)&context);
    puts("Riskfight snapshot contracts passed");
}
