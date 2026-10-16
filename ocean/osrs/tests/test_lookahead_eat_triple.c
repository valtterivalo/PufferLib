/* The lookahead's "eat triple" macro clicks hard food, then the brew, then the karambwan.
 *
 * Measured live (DeathAudit.LockTest, 3,898 consumptions): after a karambwan nothing is consumed before T+3. The allowed same-tick pairs are hard food ->
 * karambwan, hard food -> brew and brew -> karambwan, so the only order that lands all three is hard food -> brew -> karambwan. This test plays the macro's
 * script through the production step and asserts that all three clicks are ACCEPTED (the carried item is consumed) on the tick each click is due, for the
 * macro's own motor timing and for every spill of the later clicks into the next ticks; the old order (hard food, karambwan, brew) is the control: its
 * brew is rejected for three ticks.
 *
 *   cc -std=c11 -O2 -I. -o test_lookahead_eat_triple ocean/osrs/tests/test_lookahead_eat_triple.c -lm      (cwd = repo root) */
#define main lookahead_server_main
#include "gs_lookahead_server.c"
#undef main

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

typedef struct { int feasible[LA_MACROS]; LaScript scripts[LA_MACROS]; Request q; } Scenario;
static Scenario g_scenario;

static void scenario(void) {
    const char* request = WARM_REQUESTS[0];
    FILE* in = fmemopen((void*)request, strlen(request), "r");
    CHECK(in != NULL);
    read_request(in, &g_scenario.q);
    fclose(in);
    CHECK(server_unavailable(&g_scenario.q) == NULL);
    RiskfightSnapshot snap;
    snapshot_of(&g_scenario.q, &snap);
    riskfight_load_state(&g_base.state, &g_base.context, &snap);
    la_scripts(&g_base.state, g_scenario.q.tick, &g_scenario.q.feasible, &g_scenario.q.rules, g_scenario.scripts, g_scenario.feasible);
}

enum { HARD, BREW, KARB, MEALS };
static const OsrsConsumableKind KIND[MEALS] = {OSRS_CONSUMABLE_ANGLERFISH, OSRS_CONSUMABLE_BREW, OSRS_CONSUMABLE_KARAMBWAN};

/* Plays the script's clicks on the given due ticks (offsets from the decision) and returns the tick offset each meal was consumed on, -1 if never. */
static void play(const LaScript* macro, const int order[MEALS], const int due[MEALS], int consumed_at[MEALS]) {
    RiskfightState s = g_base.state;
    const Player* us = &s.env.players[0];
    int slot[MEALS];
    uint16_t before[MEALS];
    for (int m = 0; m < MEALS; m++) {
        slot[m] = slot_of_kind(us, KIND[m]);
        CHECK(slot[m] >= 0);
        before[m] = us->inventory_cells[slot[m]].content_code;
        consumed_at[m] = -1;
    }
    LaScript script = *macro;
    script.motor_active = 0;
    for (int i = 0; i < MEALS; i++) {
        script.command[i] = (HumanCommand){.kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK, .inventory_slot = slot[order[i]]};
        script.due[i] = g_scenario.q.tick + 1 + due[i];
        script.op[i] = 0;
        script.fixed[i] = 1;
    }
    int cursor = 0, resume = g_scenario.q.tick + 1;
    HumanInput* hi = &g_base.context.policy_commands[0];
    for (int step = 0; step < 8; step++) {
        human_input_clear_pending(hi);
        human_input_clear_pending(&g_base.context.policy_commands[1]);
        la_script_step(&script, &s, hi, g_scenario.q.tick + 1 + step, &cursor, &resume, 0, 0);
        riskfight_step_queues(&s, &g_base.context, &hi->commands, &g_base.context.policy_commands[1].commands);
        for (int m = 0; m < MEALS; m++)
            if (consumed_at[m] < 0 && s.env.players[0].inventory_cells[slot[m]].content_code != before[m]) consumed_at[m] = step;
    }
}

int main(void) {
    scenario();
    const LaScript* macro = &g_scenario.scripts[LA_EAT_TRIPLE];
    CHECK(g_scenario.feasible[LA_EAT_TRIPLE]);
    const Player* us = &g_base.state.env.players[0];
    int hard = slot_of_kind(us, OSRS_CONSUMABLE_ANGLERFISH), brew = slot_of_kind(us, OSRS_CONSUMABLE_BREW), karb = slot_of_kind(us, OSRS_CONSUMABLE_KARAMBWAN);
    CHECK(hard >= 0 && brew >= 0 && karb >= 0 && hard != brew && brew != karb && hard != karb);

    /* The emitted order: hard food, brew, karambwan, then the attack; due ticks never go backwards. */
    CHECK(macro->n == 4);
    CHECK(macro->command[0].kind == HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK && macro->command[0].inventory_slot == hard);
    CHECK(macro->command[1].kind == HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK && macro->command[1].inventory_slot == brew);
    CHECK(macro->command[2].kind == HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK && macro->command[2].inventory_slot == karb);
    CHECK(macro->command[3].kind == HUMAN_COMMAND_ATTACK_NPC);
    for (int i = 1; i < macro->n; i++) CHECK(macro->due[i] >= macro->due[i - 1]);

    const int recipe[MEALS] = {HARD, BREW, KARB}, old_recipe[MEALS] = {HARD, KARB, BREW};
    int consumed_at[MEALS];

    /* The macro as the sim receives it with its own motor timing: every meal is accepted on the tick its click is due. */
    int macro_due[MEALS];
    for (int i = 0; i < MEALS; i++) macro_due[i] = macro->due[i] - g_scenario.q.tick - 1;
    play(macro, recipe, macro_due, consumed_at);
    for (int m = 0; m < MEALS; m++) CHECK(consumed_at[m] == macro_due[m]);

    /* Every spill of the later clicks into the next two ticks (the motor model's cutoff pushes clicks over): hard food, brew and karambwan are all accepted on their due ticks. */
    for (int a = 0; a < 3; a++)
        for (int b = a; b < 3; b++)
            for (int c = b; c < 3; c++) {
                int due[MEALS] = {a, b, c};
                play(macro, recipe, due, consumed_at);
                CHECK(consumed_at[HARD] == a && consumed_at[BREW] == b && consumed_at[KARB] == c);
            }

    /* Control, the old order in one tick: the brew clicked after the karambwan is rejected (nothing is consumed before T+3 after a karambwan). */
    int same_tick[MEALS] = {0, 0, 0};
    play(macro, old_recipe, same_tick, consumed_at);
    CHECK(consumed_at[HARD] == 0 && consumed_at[KARB] == 0);
    CHECK(consumed_at[BREW] == -1);
    puts("Eat triple: hard food, brew, karambwan all accepted");
    return 0;
}
