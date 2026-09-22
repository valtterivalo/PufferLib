#include <assert.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"

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

static HumanCommand inventory(int slot) {
    return (HumanCommand){.kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK, .inventory_slot = slot};
}

static HumanCommand special(void) {
    return (HumanCommand){.kind = HUMAN_COMMAND_SPEC_TOGGLE};
}

static HumanCommand target(void) {
    return (HumanCommand){.kind = HUMAN_COMMAND_ATTACK_NPC, .npc_slot = 1};
}

static int recorded_hits(void) {
    return (int)state.visible[1].events[(state.env.tick - 1) % RF_HISTORY_TICKS][0];
}

static void test_double_click_needs_target_or_third_click(void) {
    for (int use_target = 0; use_target < 2; use_target++) {
        reset();
        HumanCommand attack[] = {target()};
        run(attack, 1);
        assert(recorded_hits() == 1);
        int original_timer = state.env.players[0].attack_timer;
        HumanCommand prepare[] = {inventory(24), special(), special()};
        run(prepare, 3);
        assert(state.env.players[0].special_energy == 100);
        assert(recorded_hits() == 0);
        assert(state.spec_maul[0] == 0);
        assert(state.env.pvp_runtime.maul[0].prepared_hits == 2);
        {
            float obs[RF_OBS_SIZE];
            riskfight_write_observation(&state, 0, obs);
            assert(obs[23] == 1.0f);  // DESELECTED double exposes prepared_hits/2
        }
        assert(state.env.players[0].attack_timer == original_timer - 1);
        HumanCommand release[] = {use_target ? target() : special()};
        run(release, 1);
        assert(state.env.players[0].special_energy == 0);
        assert(recorded_hits() == 2);
        assert(state.spec_maul[0] == 1);
        assert(state.env.players[0].attack_timer == original_timer - 2);
        assert(state.env.pvp_runtime.maul[0].prepared_hits == 0);
    }
}

static void test_ornate_and_ordinary_energy_costs(void) {
    const uint8_t weapons[] = {ITEM_GRANITE_MAUL_ORNATE, ITEM_GRANITE_MAUL};
    const int expected_hits[] = {2, 1};
    const int expected_energy[] = {0, 40};
    for (int i = 0; i < 2; i++) {
        reset();
        state.env.players[0].inventory_cells[24] = osrs_inventory_cell_from_item(weapons[i]);
        HumanCommand commands[] = {inventory(24), special(), special(), target()};
        run(commands, 4);
        assert(recorded_hits() == expected_hits[i]);
        assert(state.env.players[0].special_energy == expected_energy[i]);
        assert(state.env.players[1].render_hit_count == expected_hits[i]);
        assert(state.env.players[0].just_attacked == 1);
        {
            int queued = 0;
            for (int h = 0; h < state.env.players[1].render_hit_count; h++)
                queued += state.env.players[1].render_hit_damage[h];
            assert(state.env.players[0].last_queued_hit_damage == queued);
        }
        assert(state.spec_maul[0] == 1);
        assert(!state.env.players[0].spec_armed);
    }
}

static void test_voidwaker_then_only_payable_maul(void) {
    reset();
    HumanCommand voidwaker[] = {inventory(23), special(), target()};
    run(voidwaker, 3);
    assert(state.env.players[0].special_energy == 50);
    assert(recorded_hits() == 1);
    assert(state.spec_maul[0] == 0 && state.spec_voidwaker[0] == 1);
    assert(state.env.players[0].attack_style_this_tick == ATTACK_STYLE_MAGIC);
    int timer = state.env.players[0].attack_timer;
    HumanCommand maul[] = {inventory(24), special(), special(), target()};
    run(maul, 4);
    assert(state.env.players[0].special_energy == 0);
    assert(recorded_hits() == 1);
    assert(state.spec_maul[0] == 1);
    assert(state.env.players[0].attack_timer == timer - 1);
}

static void test_instant_special_defers_ready_ordinary_attack(void) {
    reset();
    HumanCommand commands[] = {inventory(24), special(), special(), target()};
    run(commands, 4);
    assert(recorded_hits() == 2);
    assert(state.spec_maul[0] == 1);
    assert(state.env.players[0].used_special_this_tick);
    assert(can_attack_now(&state.env.players[0]));
    run(NULL, 0);
    assert(recorded_hits() == 1);
    assert(state.spec_maul[0] == 1);
    assert(!state.env.players[0].used_special_this_tick);
    assert(!can_attack_now(&state.env.players[0]));
}

static void test_switch_clears_preparation(void) {
    reset();
    HumanCommand prepare[] = {inventory(24), special(), special()};
    run(prepare, 3);
    assert(state.env.pvp_runtime.maul[0].prepared_hits == 2);
    HumanCommand switch_back[] = {inventory(24)};
    run(switch_back, 1);
    assert(state.env.players[0].equipped[GEAR_SLOT_WEAPON] == ITEM_ABYSSAL_TENTACLE);
    assert(state.env.pvp_runtime.maul[0].prepared_hits == 0);
    assert(!state.env.players[0].spec_armed);
    HumanCommand attack[] = {inventory(24), target()};
    run(attack, 2);
    assert(state.env.players[0].special_energy == 100);
    assert(recorded_hits() == 1);
    assert(state.spec_maul[0] == 0);
    assert(!state.env.players[0].used_special_this_tick);
}

static void test_policy_and_human_command_equivalence(void) {
    reset();
    int actions[2 * RF_HEADS] = {0};
    actions[RF_WEAPON] = 25;
    actions[RF_SPECIAL] = 2;
    actions[RF_PRIMARY] = RF_ATTACK;
    riskfight_step((EncounterState*)&state, (EncounterContext*)&context, actions);
    static RiskfightState expected;
    expected = state;
    reset();
    HumanCommand commands[] = {inventory(24),
        {.kind = HUMAN_COMMAND_OFFENSIVE_PRAYER, .offensive_prayer = 0},
        {.kind = HUMAN_COMMAND_FIGHT_STYLE, .fight_style = 0},
        special(), special(), target()};
    run(commands, 6);
    assert(memcmp(&state, &expected, sizeof(state)) == 0);
}

static void test_prepared_target_fires_on_arrival(void) {
    reset();
    state.env.players[1].x = state.env.players[0].x + 4;
    state.env.players[1].dest_x = state.env.players[1].x;
    state.env.players[0].attack_timer = 6;
    state.env.players[0].attack_timer_uncapped = 6;
    state.env.players[0].has_attack_timer = 1;
    HumanCommand commands[] = {inventory(24), special(), special(), target()};
    run(commands, 4);
    assert(!is_in_melee_range(&state.env.players[0], &state.env.players[1]));
    assert(state.env.players[0].special_energy == 100);
    assert(recorded_hits() == 0);
    assert(state.spec_maul[0] == 0);
    run(NULL, 0);
    assert(is_in_melee_range(&state.env.players[0], &state.env.players[1]));
    assert(state.env.players[0].special_energy == 0);
    assert(recorded_hits() == 2);
    assert(state.spec_maul[0] == 1);
    assert(state.env.players[0].attack_timer == 4);
}

static void test_selected_without_homing_waits_for_ready_attack(void) {
    reset();
    HumanCommand attack[] = {inventory(24), target()};
    run(attack, 2);
    assert(recorded_hits() == 1);
    assert(state.spec_maul[0] == 0);
    for (int tick = 1; tick <= OSRS_GRANITE_MAUL_HOMING_TICKS; tick++) {
        run(NULL, 0);
        assert(recorded_hits() == 0);
    }
    HumanCommand select[] = {special()};
    run(select, 1);
    assert(state.env.players[0].special_energy == 100);
    assert(recorded_hits() == 0);
    assert(state.spec_maul[0] == 0);
    assert(state.env.players[0].spec_armed);
    run(NULL, 0);
    assert(state.env.players[0].special_energy == 50);
    assert(recorded_hits() == 1);
    assert(state.spec_maul[0] == 1);
    assert(state.env.players[0].used_special_this_tick);
}

static void test_debug_maul_double_reward_probe(void) {
    // DEBUG-ONLY probe: paid maul double pays maul_double_reward per hit
    // into rewards/episode_returns/debug_maul_rewards; zero coeff pays nothing.
    HumanCommand commands[] = {inventory(24), special(), special(), target()};
    reset();
    context.maul_double_reward = 0.5f;
    run(commands, 4);
    assert(state.env.players[0].special_energy == 0);
    assert(recorded_hits() == 2);
    assert(state.debug_maul_rewards[0] == 1.0f);
    // Shaping lands in episode_returns; s->rewards is zeroed/recomputed at
    // the step tail (damage/chance/teleport only), so assert the accumulate.
    assert(state.episode_returns[0] == state.debug_maul_rewards[0]);
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 12345);
    context.self_play = 1;
    context.maul_double_reward = 0;
    for (int i = 0; i < 2; i++) state.env.players[i].veng_active = 0;
    run(commands, 4);
    assert(state.debug_maul_rewards[0] == 0 && state.episode_returns[0] == 0);
}

int main(void) {
    riskfight_init_context((EncounterContext*)&context);
    riskfight_finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    test_double_click_needs_target_or_third_click();
    test_debug_maul_double_reward_probe();
    test_ornate_and_ordinary_energy_costs();
    test_voidwaker_then_only_payable_maul();
    test_instant_special_defers_ready_ordinary_attack();
    test_switch_clears_preparation();
    test_policy_and_human_command_equivalence();
    test_prepared_target_fires_on_arrival();
    test_selected_without_homing_waits_for_ready_attack();
    riskfight_destroy_context((EncounterContext*)&context);
    puts("Riskfight maul integration contracts passed");
}
