/* Run normally and with -DNDEBUG: desired spec state must survive queue settlement. */
#include <stdio.h>
#include <stdlib.h>
#include "../encounters/encounter_riskfight.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static RiskfightState state;
static RiskfightContext context;

static void reset(void) {
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 12345);
    context.self_play = 1;
    for (int i = 0; i < 2; i++) state.env.players[i].veng_active = 0;
}

static void run(HumanCommand* commands, int count) {
    HumanCommandQueue queue = {.items = commands, .count = count};
    HumanCommandQueue empty = {0};
    riskfight_step_queues(&state, &context, &queue, &empty);
}

static HumanCommand equip_maul(void) {
    return (HumanCommand){.kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK, .inventory_slot = 24};
}

static HumanCommand spec(HumanSpecMode mode) {
    return (HumanCommand){.kind = HUMAN_COMMAND_SPEC_TOGGLE, .spec_mode = mode};
}

static HumanCommand target(void) {
    return (HumanCommand){.kind = HUMAN_COMMAND_ATTACK_NPC, .npc_slot = 1};
}

static int recorded_hits(void) {
    return (int)state.visible[1].events[(state.env.tick - 1) % RF_HISTORY_TICKS][0];
}

static void check_disarmed(void) {
    const OsrsGraniteMaulState* maul = &state.env.pvp_runtime.maul[0];
    CHECK(!state.env.players[0].spec_armed);
    CHECK(maul->preparation == OSRS_GRANITE_MAUL_IDLE);
    CHECK(maul->prepared_hits == 0);
    CHECK(maul->preparation_expires_tick == 0);
    CHECK(maul->homing_click_tick == -1);
    CHECK(maul->pending_target == -1);
}

static void check_normal_attack(void) {
    CHECK(state.env.players[0].special_energy == 100);
    CHECK(state.spec_maul[0] == 0);
    CHECK(!state.env.players[0].used_special_this_tick);
    CHECK(recorded_hits() == 1);
    CHECK(osrs_interaction_active(&state.env.players[0].interaction));
}

static void test_enable_then_disarm_in_one_queue(void) {
    reset();
    HumanCommand commands[] = {equip_maul(), spec(HUMAN_SPEC_ENABLE), spec(HUMAN_SPEC_DISARM)};
    run(commands, 3);
    check_disarmed();
    CHECK(state.env.players[0].special_energy == 100);
    CHECK(recorded_hits() == 0);
    CHECK(state.spec_maul[0] == 0);
    HumanCommand attack[] = {target()};
    run(attack, 1);
    check_disarmed();
    check_normal_attack();
}

static void test_repeated_desired_modes_are_idempotent(void) {
    reset();
    HumanCommand enable[] = {equip_maul(), spec(HUMAN_SPEC_ENABLE), spec(HUMAN_SPEC_ENABLE)};
    run(enable, 3);
    CHECK(state.env.players[0].spec_armed);
    CHECK(state.env.pvp_runtime.maul[0].preparation == OSRS_GRANITE_MAUL_SELECTED);
    CHECK(state.env.pvp_runtime.maul[0].prepared_hits == 1);
    HumanCommand enable_again[] = {spec(HUMAN_SPEC_ENABLE)};
    run(enable_again, 1);
    CHECK(state.env.players[0].spec_armed);
    CHECK(state.env.pvp_runtime.maul[0].prepared_hits == 1);
    HumanCommand disarm[] = {spec(HUMAN_SPEC_DISARM), spec(HUMAN_SPEC_DISARM)};
    run(disarm, 2);
    check_disarmed();
    run(disarm, 2);
    check_disarmed();
    HumanCommand attack[] = {target()};
    run(attack, 1);
    check_normal_attack();
}

static void test_disarm_cancels_deselected_prepared_ticket(void) {
    reset();
    HumanCommand prepare[] = {equip_maul(), spec(HUMAN_SPEC_TOGGLE), spec(HUMAN_SPEC_TOGGLE)};
    run(prepare, 3);
    CHECK(!state.env.players[0].spec_armed);
    CHECK(state.env.pvp_runtime.maul[0].preparation == OSRS_GRANITE_MAUL_DESELECTED);
    CHECK(state.env.pvp_runtime.maul[0].prepared_hits == 2);
    HumanCommand disarm[] = {spec(HUMAN_SPEC_DISARM)};
    run(disarm, 1);
    check_disarmed();
    HumanCommand attack[] = {target()};
    run(attack, 1);
    check_normal_attack();
}

static void test_disarm_preserves_queued_normal_attack(void) {
    reset();
    state.env.players[1].x = state.env.players[0].x + 4;
    state.env.players[1].dest_x = state.env.players[1].x;
    HumanCommand commands[] = {equip_maul(), spec(HUMAN_SPEC_ENABLE), target(), spec(HUMAN_SPEC_DISARM)};
    run(commands, 4);
    check_disarmed();
    CHECK(osrs_interaction_active(&state.env.players[0].interaction));
    CHECK(recorded_hits() == 0);
    run(NULL, 0);
    check_disarmed();
    check_normal_attack();
}

static void test_disarm_preserves_committed_instant_attack(void) {
    reset();
    HumanCommand commands[] = {equip_maul(), spec(HUMAN_SPEC_ENABLE), target(), spec(HUMAN_SPEC_DISARM)};
    run(commands, 4);
    check_disarmed();
    CHECK(state.env.players[0].special_energy == 50);
    CHECK(recorded_hits() == 1);
    CHECK(state.spec_maul[0] == 1);
    CHECK(state.env.players[0].used_special_this_tick);
    CHECK(state.env.pvp_runtime.maul[0].last_special_tick == state.env.tick - 1);
    CHECK(state.env.pvp_runtime.maul[0].last_attack_target == 1);
    CHECK(osrs_interaction_active(&state.env.players[0].interaction));
    run(NULL, 0);
    check_disarmed();
    CHECK(state.env.players[0].special_energy == 50);
    CHECK(recorded_hits() == 1);
    CHECK(state.spec_maul[0] == 1);
    CHECK(!state.env.players[0].used_special_this_tick);
}

static void test_enable_after_disarm_still_fires_valid_spec(void) {
    reset();
    HumanCommand commands[] = {equip_maul(), spec(HUMAN_SPEC_ENABLE), spec(HUMAN_SPEC_DISARM),
        spec(HUMAN_SPEC_ENABLE), target()};
    run(commands, 5);
    check_disarmed();
    CHECK(state.env.players[0].special_energy == 50);
    CHECK(recorded_hits() == 1);
    CHECK(state.spec_maul[0] == 1);
    CHECK(state.env.players[0].used_special_this_tick);
}

int main(void) {
    riskfight_init_context((EncounterContext*)&context);
    riskfight_finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    test_enable_then_disarm_in_one_queue();
    test_repeated_desired_modes_are_idempotent();
    test_disarm_cancels_deselected_prepared_ticket();
    test_disarm_preserves_queued_normal_attack();
    test_disarm_preserves_committed_instant_attack();
    test_enable_after_disarm_still_fires_valid_spec();
    riskfight_destroy_context((EncounterContext*)&context);
    puts("Lookahead desired spec mode consumer contracts passed");
    return 0;
}
