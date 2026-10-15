/* The three upkeep macros appended after the 18 (drink prayer, drink restore, drink combat): offered only when the potion is carried, the potion timer is ready
 * and the drink changes the horizon (prayer runs dry inside it, a level the weapon swings with or defence is below base, a boost is lost, magic is too low for
 * Vengeance); the plan is [click the potion's slot, (Vengeance), attack], survives emit -> parse, and when played through the production step does what
 * the macro says (no dead click).
 *
 *   cc -std=c11 -O2 -I. -o test_gs_upkeep_macros ocean/osrs/tests/test_gs_upkeep_macros.c -lm      (cwd = repo root) */
#define main lookahead_server_main
#include "gs_lookahead_server.c"
#undef main

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

typedef struct { int prayer, attack, strength, defence, magic, veng_up, sacks, potion_timer, castable; const char* bag; } Upkeep;

/* The first warm-up request with its O, F and G lines replaced by the scenario's, up to its pending queue, then `tail`. */
static void compose(char* out, size_t capacity, const Upkeep* u, const char* tail) {
    const char* base = WARM_REQUESTS[0];
    const char* cut = strstr(base, "P KNOWN");
    CHECK(cut != NULL);
    size_t at = 0;
    for (const char* line = base; line < cut;) {
        const char* end = strchr(line, '\n') + 1;
        int n;
        if (line[0] == 'O' && line[1] == ' ')
            n = snprintf(out + at, capacity - at, "O 92 %d %d %d %d %d 100 0 0 %d 0 %d 0 3 0 %d 0 1 1 0 4 -1 4 4587 0 0 -1 OBSERVED 3 3\n", u->prayer, u->attack, u->strength, u->defence,
                u->magic, u->veng_up, u->sacks, u->potion_timer);
        else if (line[0] == 'F' && line[1] == ' ') n = snprintf(out + at, capacity - at, "F 1 1 %d 100\n", u->castable);
        else if (line[0] == 'G' && line[1] == ' ') n = snprintf(out + at, capacity - at, "G %s\n", u->bag);
        else n = snprintf(out + at, capacity - at, "%.*s", (int)(end - line), line);
        CHECK(n > 0 && at + (size_t)n < capacity);
        at += (size_t)n;
        line = end;
    }
    CHECK(at + strlen(tail) + 1 < capacity);
    strcpy(out + at, tail);
}

static const char TAIL[] = "P KNOWN 0\nR KNOWN 0\nEND\n";
/* slot: 0 prayer potion(4), 1 super restore(4) (client id), 2 super combat(2), 3 brew, 4..7 anglerfish, 8 karambwan, 9 Voidwaker, 10 Vengeance sack */
static const char BAG_ALL[] = "2434 24598 12697 6685 24592 24592 24592 24592 24595 27690 24621 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1";
static const char BAG_NONE[] = "24592 24592 24592 24592 24595 6685 27690 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1";
static const char BAG_RESTORE_ONLY[] = "24598 24592 24592 24592 24592 24595 6685 27690 24621 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1";

typedef struct { int feasible[LA_MACROS]; LaScript scripts[LA_MACROS]; Request q; } Scenario;
static Scenario g_scenario;

static Scenario* scenario(const Upkeep* u) {
    static char text[1 << 13];
    compose(text, sizeof(text), u, TAIL);
    FILE* in = fmemopen(text, strlen(text), "r");
    CHECK(in != NULL);
    read_request(in, &g_scenario.q);
    fclose(in);
    CHECK(server_unavailable(&g_scenario.q) == NULL);
    RiskfightSnapshot snap;
    snapshot_of(&g_scenario.q, &snap);
    riskfight_load_state(&g_base.state, &g_base.context, &snap);
    la_scripts(&g_base.state, g_scenario.q.tick, &g_scenario.q.feasible, &g_scenario.q.rules, g_scenario.scripts, g_scenario.feasible);
    return &g_scenario;
}

/* The macro's script played through the production step, nothing on his side, `steps` ticks. Returns the state it ends in. */
static RiskfightState play(const LaScript* script, int tick, int steps) {
    RiskfightState s = g_base.state;
    LaScript running = *script;
    int cursor = 0, resume = tick + 1;
    HumanInput* hi = &g_base.context.policy_commands[0];
    for (int step = 0; step < steps; step++) {
        human_input_clear_pending(hi);
        human_input_clear_pending(&g_base.context.policy_commands[1]);
        la_script_step(&running, &s, hi, tick + 1 + step, &cursor, &resume, 0, 0);
        riskfight_step_queues(&s, &g_base.context, &hi->commands, &g_base.context.policy_commands[1].commands);
    }
    CHECK(cursor == running.n);
    return s;
}

static int potion_slot(const Player* p, OsrsConsumableKind kind) { return slot_of_kind(p, kind); }

static void check_plan(const LaScript* plan, int slot, int casts) {
    CHECK(plan->n == 2 + casts);
    CHECK(plan->op[0] == 0 && plan->command[0].kind == HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK && plan->command[0].inventory_slot == slot && plan->command[0].item_osrs_id == 0);
    if (casts) CHECK(plan->op[1] == 0 && plan->command[1].kind == HUMAN_COMMAND_VENGEANCE);
    CHECK(plan->op[1 + casts] == 0 && plan->command[1 + casts].kind == HUMAN_COMMAND_ATTACK_NPC);
    for (int i = 0; i < plan->n; i++) CHECK(plan->due[i] > g_scenario.q.tick && (i == 0 || plan->due[i] >= plan->due[i - 1]));
}

/* emit -> parse -> same script: the response A records echoed as an R remainder. */
static void check_round_trip(const LaScript* plan) {
    static char tail[1 << 12], text[1 << 14];
    FILE* f = fmemopen(tail, sizeof(tail), "w");
    CHECK(f != NULL);
    fprintf(f, "P KNOWN 0\nR KNOWN %d\n", plan->n);
    for (int i = 0; i < plan->n; i++) { fputc('C', f); la_script_emit_record(f, plan, i); fputc('\n', f); }
    fputs("END\n", f);
    fclose(f);
    Upkeep u = {.prayer = 72, .attack = 118, .strength = 118, .defence = 120, .magic = 99, .sacks = 22, .castable = 1, .bag = BAG_ALL};
    compose(text, sizeof(text), &u, tail);
    static Request again;
    FILE* in = fmemopen(text, strlen(text), "r");
    CHECK(in != NULL);
    read_request(in, &again);
    fclose(in);
    CHECK(again.rules.n == plan->n);
    for (int i = 0; i < plan->n; i++) {
        CHECK(again.rules.due[i] == plan->due[i] && again.rules.op[i] == plan->op[i] && again.rules.fixed[i] == (plan->production ? plan->fixed[i] : 1));
        CHECK(again.rules.command[i].kind == plan->command[i].kind);
        if (plan->command[i].kind == HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK) CHECK(again.rules.command[i].inventory_slot == plan->command[i].inventory_slot);
    }
}

int main(void) {
    g_ablation.hp = HP_UNIFORM;
    observer_register_items();
    sim_init(&g_base);
    g_la_horizon = 15;
    CHECK(LA_MACROS == 21 && !strcmp(LA_NAME[LA_DRINK_PRAYER], "drink prayer") && !strcmp(LA_NAME[LA_DRINK_RESTORE], "drink restore") && !strcmp(LA_NAME[LA_DRINK_COMBAT], "drink combat"));
    CHECK(LA_WAIT_VENG_SPEC_BURST_3 == 17 && LA_DRINK_PRAYER == 18); /* the first 18 keep their indexes */

    /* Full prayer-ish bar, full stats, boosted: nothing to drink. */
    Upkeep fresh = {.prayer = 72, .attack = 118, .strength = 118, .defence = 120, .magic = 99, .sacks = 22, .castable = 1, .bag = BAG_ALL};
    Scenario* s = scenario(&fresh);
    CHECK(!s->feasible[LA_DRINK_PRAYER] && !s->feasible[LA_DRINK_RESTORE] && !s->feasible[LA_DRINK_COMBAT]);
    CHECK(s->feasible[LA_EAT_NOW] && s->feasible[LA_CONTINUE]); /* the rest of the list is untouched */

    /* Prayer low: Piety and Smite are on and run dry inside the horizon. */
    Upkeep low = fresh;
    low.prayer = 6;
    s = scenario(&low);
    const Player* us = &g_base.state.env.players[0];
    CHECK(us->current_prayer == 6 && us->prayer == PRAYER_SMITE && us->offensive_prayer == OFFENSIVE_PRAYER_PIETY);
    CHECK(s->feasible[LA_DRINK_PRAYER] && !s->feasible[LA_DRINK_RESTORE] && !s->feasible[LA_DRINK_COMBAT]);
    check_plan(&s->scripts[LA_DRINK_PRAYER], potion_slot(us, OSRS_CONSUMABLE_PRAYER_RESTORE), 0); /* the prayer potion, not the super restore */
    check_round_trip(&s->scripts[LA_DRINK_PRAYER]);
    RiskfightState after = play(&s->scripts[LA_DRINK_PRAYER], s->q.tick, 6);
    CHECK(after.env.players[0].current_prayer > 6 + 99 / 4 - 6); /* six ticks of drain cost a few of the 31 points */
    CHECK(osrs_inventory_cell_metadata(&after.env.players[0].inventory_cells[potion_slot(us, OSRS_CONSUMABLE_PRAYER_RESTORE)])->dose_count == 3);
    CHECK(after.env.players[0].prayer == PRAYER_SMITE && after.env.players[0].offensive_prayer == OFFENSIVE_PRAYER_PIETY); /* still on: the drink kept the points from emptying */
    int base_prayer = us->base_prayer, boundary = -1;
    /* the gate is the drain: the highest prayer level at which it is offered is the one that still runs dry in 15 ticks */
    for (int prayer = 1; prayer < base_prayer; prayer++) {
        Upkeep u = fresh;
        u.prayer = prayer;
        s = scenario(&u);
        if (!s->feasible[LA_DRINK_PRAYER]) { boundary = prayer; break; }
    }
    CHECK(boundary > 6 && boundary < 20);
    for (int prayer = boundary; prayer < base_prayer; prayer++) { Upkeep u = fresh; u.prayer = prayer; CHECK(!scenario(&u)->feasible[LA_DRINK_PRAYER]); }
    /* a super restore stands in when no prayer potion is carried */
    low.bag = BAG_RESTORE_ONLY;
    s = scenario(&low);
    us = &g_base.state.env.players[0];
    CHECK(s->feasible[LA_DRINK_PRAYER] && s->feasible[LA_DRINK_RESTORE] == 0);
    check_plan(&s->scripts[LA_DRINK_PRAYER], potion_slot(us, OSRS_CONSUMABLE_SUPER_RESTORE), 0);
    /* no potion in the bag */
    low.bag = BAG_NONE;
    s = scenario(&low);
    CHECK(!s->feasible[LA_DRINK_PRAYER] && !s->feasible[LA_DRINK_RESTORE] && !s->feasible[LA_DRINK_COMBAT]);
    /* the potion lock: not offered while the timer runs */
    low.bag = BAG_ALL;
    low.potion_timer = 2;
    s = scenario(&low);
    CHECK(!s->feasible[LA_DRINK_PRAYER] && !s->feasible[LA_DRINK_RESTORE] && !s->feasible[LA_DRINK_COMBAT]);
    low.potion_timer = 0;
    /* an empty prayer: nothing is on any more, a drink changes nothing the rollout can see */
    low.prayer = 0;
    s = scenario(&low);
    CHECK(!s->feasible[LA_DRINK_PRAYER]);

    /* Drained after brews: restore and combat. Magic is too low for Vengeance, which the wire reports as not castable. */
    Upkeep drained = {.prayer = 72, .attack = 90, .strength = 85, .defence = 100, .magic = 85, .sacks = 22, .castable = 0, .bag = BAG_ALL};
    s = scenario(&drained);
    us = &g_base.state.env.players[0];
    CHECK(s->feasible[LA_DRINK_RESTORE] && s->feasible[LA_DRINK_COMBAT] && !s->feasible[LA_DRINK_PRAYER] && !s->feasible[LA_VENG_NOW]);
    check_plan(&s->scripts[LA_DRINK_RESTORE], potion_slot(us, OSRS_CONSUMABLE_SUPER_RESTORE), 1); /* the restore brings magic back: it casts too */
    check_plan(&s->scripts[LA_DRINK_COMBAT], potion_slot(us, OSRS_CONSUMABLE_SUPER_COMBAT), 0);
    check_round_trip(&s->scripts[LA_DRINK_RESTORE]);
    check_round_trip(&s->scripts[LA_DRINK_COMBAT]);
    after = play(&s->scripts[LA_DRINK_RESTORE], s->q.tick, 6);
    const Player* me = &after.env.players[0];
    CHECK(me->current_attack == 99 && me->current_strength == 99 && me->current_magic == 99 && me->veng_active && me->veng_cooldown > 0);
    after = play(&s->scripts[LA_DRINK_COMBAT], s->q.tick, 6);
    me = &after.env.players[0];
    CHECK(me->current_attack == 90 + 5 + 99 * 15 / 100 && me->current_strength == 85 + 5 + 99 * 15 / 100 && me->current_defence == 118 && me->current_magic == 85);
    /* a castable Vengeance needs no unlocking: the restore is a plain two-click plan */
    drained.castable = 1;
    s = scenario(&drained);
    check_plan(&s->scripts[LA_DRINK_RESTORE], potion_slot(&g_base.state.env.players[0], OSRS_CONSUMABLE_SUPER_RESTORE), 0);
    /* Vengeance already up, or no sack carried: the cast is not part of the plan */
    drained.castable = 0; drained.veng_up = 1;
    s = scenario(&drained);
    check_plan(&s->scripts[LA_DRINK_RESTORE], potion_slot(&g_base.state.env.players[0], OSRS_CONSUMABLE_SUPER_RESTORE), 0);
    drained.veng_up = 0; drained.sacks = 0;
    s = scenario(&drained);
    check_plan(&s->scripts[LA_DRINK_RESTORE], potion_slot(&g_base.state.env.players[0], OSRS_CONSUMABLE_SUPER_RESTORE), 0);
    /* Only magic is low: with Vengeance castable a restore changes nothing the weapon uses */
    Upkeep magic_only = {.prayer = 72, .attack = 118, .strength = 118, .defence = 120, .magic = 88, .sacks = 22, .castable = 1, .bag = BAG_ALL};
    s = scenario(&magic_only);
    CHECK(!s->feasible[LA_DRINK_RESTORE] && !s->feasible[LA_DRINK_COMBAT]);
    /* ... and with Vengeance not castable because of it, the restore is the unlock */
    magic_only.castable = 0;
    s = scenario(&magic_only);
    CHECK(s->feasible[LA_DRINK_RESTORE]);
    check_plan(&s->scripts[LA_DRINK_RESTORE], potion_slot(&g_base.state.env.players[0], OSRS_CONSUMABLE_SUPER_RESTORE), 1);
    /* Boost lost only: a combat potion, no restore (nothing is below base) */
    Upkeep boost = {.prayer = 72, .attack = 105, .strength = 105, .defence = 120, .magic = 99, .sacks = 22, .castable = 1, .bag = BAG_ALL};
    s = scenario(&boost);
    CHECK(s->feasible[LA_DRINK_COMBAT] && !s->feasible[LA_DRINK_RESTORE] && !s->feasible[LA_DRINK_PRAYER]);
    after = play(&s->scripts[LA_DRINK_COMBAT], s->q.tick, 6);
    CHECK(after.env.players[0].current_attack == 118 && after.env.players[0].current_strength == 118 && after.env.players[0].current_defence == 120);
    puts("Upkeep macros passed");
    return 0;
}
